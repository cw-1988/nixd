#include "lspserver/LSPServer.h"
#include "lspserver/Connection.h"
#include "lspserver/Function.h"

#include <llvm/ADT/FunctionExtras.h>
#include <llvm/Support/Compiler.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/JSON.h>

#include <mutex>
#include <stdexcept>

namespace lspserver {

void LSPServer::run() {
  In->loop(*this);
  failPendingCalls("connection closed before receiving a reply");
}

bool LSPServer::onNotify(llvm::StringRef Method, llvm::json::Value Params) {
  log("<-- {0}", Method);
  if (Method == "exit")
    return false;
  auto Handler = Registry.NotificationHandlers.find(Method);
  if (Handler != Registry.NotificationHandlers.end()) {
    Handler->second(std::move(Params));
  } else {
    log("unhandled notification {0}", Method);
  }
  return true;
}

bool LSPServer::onCall(llvm::StringRef Method, llvm::json::Value Params,
                       llvm::json::Value ID) {
  log("<-- {0}({1})", Method, ID);
  auto Handler = Registry.MethodHandlers.find(Method);
  if (Handler != Registry.MethodHandlers.end())
    Handler->second(std::move(Params),
                    [=, Method = std::string(Method),
                     this](llvm::Expected<llvm::json::Value> Response) mutable {
                      if (Response) {
                        log("--> reply:{0}({1})", Method, ID);
                        Out->reply(std::move(ID), std::move(Response));
                      } else {
                        llvm::Error Err = Response.takeError();
                        log("--> reply:{0}({1}) {2:ms}, error: {3}", Method, ID,
                            Err);
                        Out->reply(std::move(ID), std::move(Err));
                      }
                    });
  else
    return false;
  return true;
}

bool LSPServer::onReply(llvm::json::Value ID,
                        llvm::Expected<llvm::json::Value> Result) {
  log("<-- reply({0})", ID);
  std::optional<Callback<llvm::json::Value>> CB;

  if (auto OptI = ID.getAsInteger()) {
    if (LLVM_UNLIKELY(*OptI > INT_MAX))
      throw std::logic_error("jsonrpc: id is too large (> INT_MAX)");
    std::lock_guard<std::mutex> Guard(PendingCallsLock);
    auto I = static_cast<int>(*OptI);
    if (PendingCalls.contains(I)) {
      CB = std::move(PendingCalls[I]);
      PendingCalls.erase(I);
    }
  } else {
    throw std::logic_error("jsonrpc: not an integer message ID");
  }
  if (LLVM_UNLIKELY(!CB)) {
    elog("received a reply with ID {0}, but there was no such call", ID);
    // Ignore this error
    return true;
  }
  // Invoke the callback outside of the critical zone, because we just do not
  // need to lock PendingCalls.
  (*CB)(std::move(Result));
  return true;
}

std::optional<int> LSPServer::bindReply(Callback<llvm::json::Value> CB) {
  std::optional<Callback<llvm::json::Value>> FailedCallback;
  std::optional<int> Ret;
  std::optional<std::tuple<int, Callback<llvm::json::Value>>> OldestCall;
  {
    std::lock_guard<std::mutex> _(PendingCallsLock);
    if (ConnectionClosed) {
      FailedCallback = std::move(CB);
    } else {
      Ret = TopID++;
      PendingCalls[*Ret] = std::move(CB);
    }

    // Check the limit
    if (PendingCalls.size() > MaxPendingCalls) {
      auto Begin = PendingCalls.begin();
      OldestCall = std::tuple{Begin->first, std::move(Begin->second)};
      PendingCalls.erase(Begin);
    }
  }

  if (FailedCallback) {
    (*FailedCallback)(
        error("failed to receive a client reply: connection is closed"));
    return std::nullopt;
  }

  if (OldestCall) {
    auto &[ID, OldestCallback] = *OldestCall;
    OldestCallback(
        error("failed to receive a client reply for request ({0})", ID));
    elog("more than {0} outstanding LSP calls, forgetting about {1}",
         MaxPendingCalls, ID);
  }
  return Ret;
}

void LSPServer::failPendingCalls(llvm::StringRef Reason) {
  std::map<int, Callback<llvm::json::Value>> Calls;
  {
    std::lock_guard<std::mutex> _(PendingCallsLock);
    ConnectionClosed = true;
    Calls.swap(PendingCalls);
  }

  for (auto &[ID, CB] : Calls)
    CB(error("failed to receive a client reply for request ({0}): {1}", ID,
             Reason));
}

} // namespace lspserver
