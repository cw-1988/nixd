#include "nixd-config.h"

#include "nixd/Eval/AttrSetClient.h"

#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>

#include <signal.h> // NOLINT(modernize-deprecated-headers)

using namespace nixd;
using namespace lspserver;

AttrSetClient::AttrSetClient(std::unique_ptr<lspserver::InboundPort> In,
                             std::unique_ptr<lspserver::OutboundPort> Out)
    : LSPServer(std::move(In), std::move(Out)) {
  EvalExpr = mkOutMethod<EvalExprParams, EvalExprResponse>(rpcMethod::EvalExpr);
  AttrPathInfo = mkOutMethod<AttrPathInfoParams, AttrPathInfoResponse>(
      rpcMethod::AttrPathInfo);
  AttrPathComplete =
      mkOutMethod<AttrPathCompleteParams, AttrPathCompleteResponse>(
          rpcMethod::AttrPathComplete);
  OptionInfo = mkOutMethod<AttrPathInfoParams, OptionInfoResponse>(
      rpcMethod::OptionInfo);
  OptionComplete = mkOutMethod<AttrPathCompleteParams, OptionCompleteResponse>(
      rpcMethod::OptionComplete);
  Exit = mkOutNotifiction<std::nullptr_t>(rpcMethod::Exit);
}

const char *AttrSetClient::getExe() {
  if (const char *Env = std::getenv("NIXD_ATTRSET_EVAL"))
    return Env;

  static std::string SiblingExe = []() {
    llvm::SmallString<256> Path(llvm::sys::fs::getMainExecutable(
        "nixd", reinterpret_cast<void *>(&AttrSetClient::getExe)));
    llvm::sys::path::remove_filename(Path);
    llvm::sys::path::append(Path, "nixd-attrset-eval");
    if (llvm::sys::fs::can_execute(Path))
      return Path.str().str();
    return std::string();
  }();
  if (!SiblingExe.empty())
    return SiblingExe.c_str();

  return NIXD_LIBEXEC "/nixd-attrset-eval";
}

AttrSetClientProc::AttrSetClientProc(const std::function<int()> &Action)
    : Proc(Action), Client(Proc.mkIn(), Proc.mkOut()),
      Input([this]() { Client.run(); }) {}

AttrSetClient *AttrSetClientProc::client() {
  if (Client.isClosed())
    return nullptr;
  if (!kill(Proc.proc().PID, 0))
    return &Client;
  return nullptr;
}
