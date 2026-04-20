#include "nixd-config.h"

#include "nixd/Eval/AttrSetClient.h"

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>

#include <signal.h> // NOLINT(modernize-deprecated-headers)

using namespace nixd;
using namespace lspserver;

namespace {

std::string MainExecutablePath;
std::string AttrSetEvalExecutablePath;

} // namespace

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

  if (!MainExecutablePath.empty()) {
    llvm::SmallString<256> Candidate(MainExecutablePath);
    llvm::sys::path::remove_filename(Candidate);
    llvm::sys::path::append(Candidate, "nixd-attrset-eval");
    if (llvm::sys::fs::can_execute(Candidate)) {
      AttrSetEvalExecutablePath = Candidate.str().str();
      return AttrSetEvalExecutablePath.c_str();
    }
  }

  return NIXD_LIBEXEC "/nixd-attrset-eval";
}

void AttrSetClient::setMainExecutablePath(const char *Argv0, void *MainAddr) {
  MainExecutablePath = llvm::sys::fs::getMainExecutable(Argv0, MainAddr);
}

AttrSetClientProc::AttrSetClientProc(const std::function<int()> &Action)
    : Proc(Action), Client(Proc.mkIn(), Proc.mkOut()),
      Input([this]() { Client.run(); }) {}

AttrSetClient *AttrSetClientProc::client() {
  if (!kill(Proc.proc().PID, 0))
    return &Client;
  return nullptr;
}
