#include "nixd/Support/PipedProc.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <sys/wait.h>
#include <thread>

using namespace std::chrono_literals;

namespace nixd::util {

namespace {

bool reapChild(pid_t &PID, int Options) {
  int Status = 0;
  for (;;) {
    pid_t Result = waitpid(PID, &Status, Options);
    if (Result == PID) {
      PID = -1;
      return true;
    }
    if (Result == 0)
      return false;
    if (errno == EINTR)
      continue;
    if (errno == ECHILD) {
      PID = -1;
      return true;
    }
    return false;
  }
}

bool waitForChildExit(pid_t &PID, std::chrono::milliseconds Delay,
                      unsigned Attempts) {
  for (unsigned I = 0; I < Attempts; ++I) {
    if (reapChild(PID, WNOHANG))
      return true;
    std::this_thread::sleep_for(Delay);
  }
  return reapChild(PID, WNOHANG);
}

} // namespace

bool PipedProc::running() {
  if (PID <= 0)
    return false;
  return !reapChild(PID, WNOHANG);
}

PipedProc::~PipedProc() {
  if (PID <= 0)
    return;

  Stdin.close();
  Stdout.close();
  Stderr.close();

  if (waitForChildExit(PID, 10ms, 10))
    return;

  kill(PID, SIGTERM);
  if (waitForChildExit(PID, 10ms, 20))
    return;

  kill(PID, SIGKILL);
  reapChild(PID, 0);
}

} // namespace nixd::util
