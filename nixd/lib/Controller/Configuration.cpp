#include "nixd/Controller/Controller.h"
#include "nixd/CommandLine/Options.h"
#include "nixd/Eval/Launch.h"

#include <boost/asio/post.hpp>

#include <atomic>
#include <memory>

using namespace nixd;
using namespace lspserver;
using llvm::json::ObjectMapper;
using llvm::json::Value;

namespace {

constexpr int NixFileWatcherKinds = 7; // create | change | delete
constexpr auto DuplicateSaveWatcherSuppression = std::chrono::seconds(1);

bool shouldReevaluateOptionProvidersForFile(lspserver::PathRef File) {
  return std::string_view(File).ends_with(".nix");
}

} // namespace

bool nixd::fromJSON(const Value &Params, Configuration::Diagnostic &R,
                    llvm::json::Path P) {
  ObjectMapper O(Params, P);
  return O && O.mapOptional("suppress", R.suppress);
}

bool nixd::fromJSON(const Value &Params, Configuration::Formatting &R,
                    llvm::json::Path P) {
  // If it is a single string, treat it as a single vector
  if (auto Str = Params.getAsString()) {
    R.command = {Str->str()};
    return true;
  }
  ObjectMapper O(Params, P);
  return O && O.mapOptional("command", R.command);
}

bool nixd::fromJSON(const Value &Params, Configuration::OptionProvider &R,
                    llvm::json::Path P) {
  ObjectMapper O(Params, P);
  return O && O.mapOptional("expr", R.expr);
}

bool nixd::fromJSON(const Value &Params, Configuration::NixpkgsProvider &R,
                    llvm::json::Path P) {
  ObjectMapper O(Params, P);
  return O && O.mapOptional("expr", R.expr);
}

bool nixd::fromJSON(const Value &Params, Configuration &R, llvm::json::Path P) {
  ObjectMapper O(Params, P);
  return O                                            //
         && O.mapOptional("formatting", R.formatting) //
         && O.mapOptional("options", R.options)       //
         && O.mapOptional("nixpkgs", R.nixpkgs)       //
         && O.mapOptional("diagnostic", R.diagnostic) //
      ;
}

void Controller::onDidChangeConfiguration(
    const DidChangeConfigurationParams &Params) {
  // FIXME: incrementally change?
  fetchConfig();
}

void Controller::registerNixFileWatchers() {
  if (!ClientCaps.WorkspaceDidChangeWatchedFilesDynamicRegistration)
    return;

  llvm::json::Array Registrations;
  Registrations.emplace_back(llvm::json::Object{
      {"id", "nixd-watch-nix-files"},
      {"method", "workspace/didChangeWatchedFiles"},
      {"registerOptions",
       llvm::json::Object{
           {"watchers",
            llvm::json::Array{
                llvm::json::Object{
                    {"globPattern", "**/*.nix"},
                    {"kind", NixFileWatcherKinds},
                },
            }},
       }},
  });

  RegisterCapability(
      llvm::json::Object{{"registrations", std::move(Registrations)}},
      [](llvm::Expected<std::nullptr_t> Resp) {
        if (!Resp)
          lspserver::elog("client/registerCapability: {0}",
                          Resp.takeError());
      });
}

void Controller::enqueueOptionProviderReevaluation(bool RestartWorkers) {
  bool ShouldPost = false;
  {
    std::lock_guard _(OptionReevaluationLock);
    OptionReevaluationQueued = true;
    OptionReevaluationRestartWorkers |= RestartWorkers;
    if (!OptionReevaluationRunning) {
      OptionReevaluationRunning = true;
      ShouldPost = true;
    }
  }

  if (!ShouldPost)
    return;

  postToPool([this]() { processOptionProviderReevaluationQueue(); });
}

void Controller::processOptionProviderReevaluationQueue() {
  bool RestartWorkers = false;
  {
    std::lock_guard _(OptionReevaluationLock);
    if (!OptionReevaluationQueued) {
      OptionReevaluationRunning = false;
      return;
    }
    OptionReevaluationQueued = false;
    RestartWorkers = OptionReevaluationRestartWorkers;
    OptionReevaluationRestartWorkers = false;
  }

  reevaluateOptionProviders(RestartWorkers, [this]() {
    postToPool([this]() { processOptionProviderReevaluationQueue(); });
  });
}

void Controller::reevaluateOptionProviders(bool RestartWorkers,
                                           llvm::unique_function<void()> OnDone) {
  Configuration ActiveConfig;
  {
    std::lock_guard G(ConfigLock);
    ActiveConfig = Config;
  }

  std::map<std::string, std::string> ProviderExprs;
  for (const auto &[Name, Opt] : ActiveConfig.options)
    ProviderExprs.insert_or_assign(Name, Opt.expr);

  {
    std::lock_guard _(OptionsLock);
    if (Options.contains("nixos") && !ProviderExprs.contains("nixos"))
      ProviderExprs.emplace("nixos", getDefaultNixOSOptionsExpr());
  }

  bool NeedDiagnosticRefresh = false;
  std::vector<std::string> RemovedProviders;
  {
    std::lock_guard _(OptionsLock);
    for (auto It = Options.begin(); It != Options.end();) {
      if (ProviderExprs.contains(It->first)) {
        ++It;
        continue;
      }

      std::string Name = It->first;
      NeedDiagnosticRefresh = true;
      NeedDiagnosticRefresh |= OptionProviderErrors.erase(Name) > 0;
      ReadyOptions.erase(Name);
      SettledOptions.erase(Name);
      OptionGenerations.erase(Name);
      OptionEvalGenerations.erase(Name);
      It = Options.erase(It);
      RemovedProviders.push_back(std::move(Name));
    }
  }
  for (const std::string &Name : RemovedProviders)
    OptService.invalidateProvider(Name);

  if (ProviderExprs.empty()) {
    if (NeedDiagnosticRefresh && !ShuttingDown)
      postToDiagnosticsPool([this]() { refreshDiagnostics(); });
    if (OnDone)
      OnDone();
    return;
  }

  auto RemainingProviders =
      std::make_shared<std::atomic<size_t>>(ProviderExprs.size());
  auto BatchNeedsRefresh =
      std::make_shared<std::atomic<bool>>(NeedDiagnosticRefresh ||
                                          !ProviderExprs.empty());
  auto ReevaluationDone =
      std::make_shared<llvm::unique_function<void()>>(std::move(OnDone));
  auto finishProvider = [this, RemainingProviders, BatchNeedsRefresh,
                         ReevaluationDone]() {
    if (RemainingProviders->fetch_sub(1, std::memory_order_acq_rel) != 1)
      return;
    if (BatchNeedsRefresh->load(std::memory_order_acquire) && !ShuttingDown)
      postToDiagnosticsPool([this]() { refreshDiagnostics(); });
    if (*ReevaluationDone)
      (*ReevaluationDone)();
  };

  for (const auto &[Name, Expr] : ProviderExprs) {
    AttrSetClient *Client = nullptr;
    std::uint64_t EvalGeneration = 0;
    {
      std::lock_guard _(OptionsLock);
      auto &Proc = Options[Name];
      if (RestartWorkers && Proc)
        Proc.reset();
      if (!Proc) {
        // If it does not exist. Launch a new client.
        startOption(Name, Proc);
      }
      if (OptionProviderErrors.erase(Name) > 0)
        BatchNeedsRefresh->store(true, std::memory_order_release);
      ReadyOptions.erase(Name);
      SettledOptions.erase(Name);
      EvalGeneration = NextOptionEvalGeneration++;
      OptionEvalGenerations[Name] = EvalGeneration;
      assert(Proc);
      Client = Proc->client();
    }
    OptService.invalidateProvider(Name);
    if (Client) {
      evalExprWithProgress(
          *Client, Expr, Name,
          [this, Name, EvalGeneration, BatchNeedsRefresh]() {
            if (noteOptionProviderChanged(Name, EvalGeneration))
              BatchNeedsRefresh->store(true, std::memory_order_release);
          },
          [this, Name, EvalGeneration, BatchNeedsRefresh,
           finishProvider](bool Success, std::optional<EvalExprError> Error) {
            if (!Success) {
              if (noteOptionProviderSettled(Name, EvalGeneration,
                                            std::move(Error)))
                BatchNeedsRefresh->store(true, std::memory_order_release);
            }
            finishProvider();
          });
      continue;
    }
    finishProvider();
  }
}

void Controller::noteOptionProviderFileSaved(PathRef File) {
  if (!shouldReevaluateOptionProvidersForFile(File))
    return;

  const auto Now = std::chrono::steady_clock::now();
  std::lock_guard _(OptionProviderSaveLock);
  std::erase_if(RecentOptionProviderSaves, [&](const auto &Entry) {
    return Now - Entry.second > DuplicateSaveWatcherSuppression;
  });
  RecentOptionProviderSaves.insert_or_assign(File.str(), Now);
}

void Controller::reevaluateOptionProvidersForFileChange(PathRef File) {
  if (!shouldReevaluateOptionProvidersForFile(File))
    return;
  enqueueOptionProviderReevaluation(/*RestartWorkers=*/true);
}

void Controller::updateConfig(Configuration NewConfig) {
  Configuration ActiveConfig;
  {
    std::lock_guard G(ConfigLock);
    Config = std::move(NewConfig);
    ActiveConfig = Config;
  }

  if (!ActiveConfig.nixpkgs.expr.empty()) {
    /// Evaluate nixpkgs and options, using user-provided config.
    if (nixpkgsClient()) {
      evalExprWithProgress(*nixpkgsClient(), ActiveConfig.nixpkgs.expr,
                           "nixpkgs entries");
    }
  }
  enqueueOptionProviderReevaluation();

  // Update the diagnostic part.
  updateSuppressed(ActiveConfig.diagnostic.suppress);

  // After all, notify all AST modules the diagnostic set has been updated.
  std::vector<std::pair<std::string, std::shared_ptr<NixTU>>> Snapshot;
  {
    std::lock_guard TUsGuard(TUsLock);
    Snapshot.reserve(TUs.size());
    for (const auto &[File, TU] : TUs)
      Snapshot.emplace_back(File.str(), TU);
  }
  for (const auto &[File, TU] : Snapshot) {
    publishDiagnostics(File, std::nullopt, TU->src(), TU->diagnostics(),
                       TU->nixdDiagnostics());
  }
}

void Controller::fetchConfig() {
  auto Action = [this](llvm::Expected<llvm::json::Value> Resp) mutable {
    if (!Resp) {
      elog("workspace/configuration: {0}", Resp.takeError());
      return;
    }

    // LSP response is a json array, just take the first.
    if (Resp->kind() != llvm::json::Value::Array) {
      lspserver::elog("workspace/configuration response is not an array: {0}",
                      *Resp);
      return;
    }
    const Value &FirstConfig = Resp->getAsArray()->front();

    // Run this job in the thread pool. Don't block input thread.
    auto ConfigAction = [this, FirstConfig]() mutable {
      // Parse the config
      Configuration NewConfig;
      llvm::json::Path::Root P;
      if (!fromJSON(FirstConfig, NewConfig, P)) {
        elog("workspace/configuration: parse error {0}", P.getError());
        return;
      }

      // OK, update the config
      updateConfig(std::move(NewConfig));
    };

    postToPool(std::move(ConfigAction));
  };
  workspaceConfiguration({.items = {ConfigurationItem{.section = "nixd"}}},
                         std::move(Action));
}

void Controller::workspaceConfiguration(
    const lspserver::ConfigurationParams &Params,
    lspserver::Callback<llvm::json::Value> Reply) {
  if (ClientCaps.WorkspaceConfiguration) {
    WorkspaceConfiguration(Params, std::move(Reply));
  } else {
    Reply(lspserver::error("client does not support workspace configuration"));
  }
}

void Controller::onDidChangeWatchedFiles(
    const lspserver::DidChangeWatchedFilesParams &Params) {
  const auto Now = std::chrono::steady_clock::now();
  for (const lspserver::FileEvent &Change : Params.changes) {
    if (!shouldReevaluateOptionProvidersForFile(Change.uri.file()))
      continue;

    bool Suppress = false;
    {
      std::lock_guard _(OptionProviderSaveLock);
      std::erase_if(RecentOptionProviderSaves, [&](const auto &Entry) {
        return Now - Entry.second > DuplicateSaveWatcherSuppression;
      });
      auto It = RecentOptionProviderSaves.find(Change.uri.file().str());
      if (It != RecentOptionProviderSaves.end()) {
        Suppress = true;
        RecentOptionProviderSaves.erase(It);
      }
    }
    if (!Suppress) {
      enqueueOptionProviderReevaluation(/*RestartWorkers=*/true);
      return;
    }
  }
}
