#include "nixd/Controller/Controller.h"

#include <boost/asio/post.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace nixd;

void Controller::scheduleOptionDiagnostics(lspserver::PathRef File,
                                           std::optional<int64_t> Version,
                                           std::shared_ptr<NixTU> TU) {
  boost::asio::post(
      Pool, [this, File = File.str(), Version, TU = std::move(TU)]() mutable {
        std::vector<NixdDiagnostic> Diagnostics = collectOptionDiagnostics(*TU);
        {
          std::lock_guard _(TUsLock);
          auto It = TUs.find(File);
          if (It == TUs.end() || It->second != TU)
            return;
          TU->setNixdDiagnostics(std::move(Diagnostics));
        }
        publishDiagnostics(File, Version, TU->src(), TU->diagnostics(),
                           TU->nixdDiagnostics());
      });
}

void Controller::refreshDiagnostics() {
  std::vector<std::pair<std::string, std::shared_ptr<NixTU>>> Snapshot;
  {
    std::lock_guard _(TUsLock);
    Snapshot.reserve(TUs.size());
    for (const auto &[File, TU] : TUs)
      Snapshot.emplace_back(File.str(), TU);
  }

  for (const auto &[File, TU] : Snapshot) {
    std::vector<NixdDiagnostic> Diagnostics = collectOptionDiagnostics(*TU);
    {
      std::lock_guard _(TUsLock);
      auto It = TUs.find(File);
      if (It == TUs.end() || It->second != TU)
        continue;
      TU->setNixdDiagnostics(std::move(Diagnostics));
    }
    publishDiagnostics(File, std::nullopt, TU->src(), TU->diagnostics(),
                       TU->nixdDiagnostics());
  }
}
