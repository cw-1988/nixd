#pragma once

#include "Configuration.h"
#include "EvalClient.h"
#include "NixTU.h"
#include "Option.h"

#include "lspserver/DraftStore.h"
#include "lspserver/LSPServer.h"
#include "lspserver/Protocol.h"
#include "nixd/Eval/AttrSetClient.h"
#include "nixf/Basic/Diagnostic.h"

#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace nixd {

class Controller : public lspserver::LSPServer {
public:
  using OptionMapTy = std::map<std::string, std::shared_ptr<AttrSetClientProc>>;

private:
  std::unique_ptr<OwnedEvalClient> Eval;

  // Use this worker for evaluating nixpkgs.
  std::shared_ptr<AttrSetClientProc> NixpkgsEval;
  std::atomic<bool> ShuttingDown = false;

  std::mutex OptionsLock;
  std::condition_variable OptionsReadyCV;
  std::mutex OptionReevaluationLock;
  bool OptionReevaluationRunning = false;       // GUARDED_BY(OptionReevaluationLock)
  bool OptionReevaluationQueued = false;        // GUARDED_BY(OptionReevaluationLock)
  bool OptionReevaluationRestartWorkers = false; // GUARDED_BY(OptionReevaluationLock)
  std::mutex OptionProviderSaveLock;
  std::map<std::string, std::chrono::steady_clock::time_point>
      RecentOptionProviderSaves; // GUARDED_BY(OptionProviderSaveLock)
  OptionService OptService;
  std::map<std::string, std::uint64_t>
      OptionGenerations;                  // GUARDED_BY(OptionsLock)
  std::uint64_t NextOptionGeneration = 1; // GUARDED_BY(OptionsLock)
  std::map<std::string, std::uint64_t>
      OptionEvalGenerations;                   // GUARDED_BY(OptionsLock)
  std::uint64_t NextOptionEvalGeneration = 1; // GUARDED_BY(OptionsLock)
  std::map<std::string, EvalExprError> OptionProviderErrors; // GUARDED_BY(OptionsLock)
  std::set<std::string> ReadyOptions;     // GUARDED_BY(OptionsLock)
  std::set<std::string> SettledOptions;   // GUARDED_BY(OptionsLock)
  // Map of option providers.
  //
  // e.g. "nixos" -> nixos worker
  //      "home-manager" -> home-manager worker
  OptionMapTy Options; // GUARDED_BY(OptionsLock)

  AttrSetClientProc &nixpkgsEval() {
    assert(NixpkgsEval);
    return *NixpkgsEval;
  }

  AttrSetClient *nixpkgsClient() { return nixpkgsEval().client(); }

  void evalExprWithProgress(AttrSetClient &Client, const EvalExprParams &Params,
                            std::string_view Description,
                            llvm::unique_function<void()> OnSuccess = nullptr,
                            llvm::unique_function<void(
                                bool, std::optional<EvalExprError>)> OnDone =
                                nullptr);

  lspserver::DraftStore Store;

  lspserver::ClientCapabilities ClientCaps;

  std::mutex ConfigLock;
  Configuration Config; // GUARDED_BY(ConfigLock)

  llvm::unique_function<void(const lspserver::ConfigurationParams &,
                             lspserver::Callback<llvm::json::Value>)>
      WorkspaceConfiguration;
  llvm::unique_function<void(const llvm::json::Value &,
                             lspserver::Callback<std::nullptr_t>)>
      RegisterCapability;

  void workspaceConfiguration(const lspserver::ConfigurationParams &Params,
                              lspserver::Callback<llvm::json::Value> Reply);
  void registerNixFileWatchers();

  /// \brief Update the configuration, do necessary adjusting for updates.
  ///
  /// \example If asked to change eval settings, send eval requests to workers.
  void updateConfig(Configuration NewConfig);

  /// \brief Get configuration from LSP client. Update the config.
  void fetchConfig();
  void enqueueOptionProviderReevaluation(bool RestartWorkers = false);
  void processOptionProviderReevaluationQueue();
  void reevaluateOptionProviders(bool RestartWorkers = false,
                                 llvm::unique_function<void()> OnDone =
                                     nullptr);
  void noteOptionProviderFileSaved(lspserver::PathRef File);
  void reevaluateOptionProvidersForFileChange(lspserver::PathRef File);

  llvm::unique_function<void(const lspserver::PublishDiagnosticsParams &)>
      PublishDiagnostic;
  llvm::unique_function<void(const lspserver::WorkDoneProgressCreateParams &,
                             lspserver::Callback<std::nullptr_t>)>
      CreateWorkDoneProgress;

  void
  createWorkDoneProgress(const lspserver::WorkDoneProgressCreateParams &Params);

  /// Request the client to show a document (file or URL).
  /// @since LSP 3.16.0
  llvm::unique_function<void(
      const lspserver::ShowDocumentParams &,
      lspserver::Callback<lspserver::ShowDocumentResult>)>
      ShowDocument;

  llvm::unique_function<void(
      const lspserver::ProgressParams<lspserver::WorkDoneProgressBegin> &)>
      BeginWorkDoneProgress;

  void beginWorkDoneProgress(
      const lspserver::ProgressParams<lspserver::WorkDoneProgressBegin>
          &Params) {
    if (ClientCaps.WorkDoneProgress)
      BeginWorkDoneProgress(Params);
  }

  llvm::unique_function<void(
      const lspserver::ProgressParams<lspserver::WorkDoneProgressReport> &)>
      ReportWorkDoneProgress;

  void reportWorkDoneProgress(
      const lspserver::ProgressParams<lspserver::WorkDoneProgressReport>
          &Params) {
    if (ClientCaps.WorkDoneProgress)
      ReportWorkDoneProgress(Params);
  }

  llvm::unique_function<void(
      lspserver::ProgressParams<lspserver::WorkDoneProgressEnd>)>
      EndWorkDoneProgress;

  void endWorkDoneProgress(
      const lspserver::ProgressParams<lspserver::WorkDoneProgressEnd> &Params) {
    if (ClientCaps.WorkDoneProgress)
      EndWorkDoneProgress(Params);
  }

  mutable std::mutex TUsLock;
  llvm::StringMap<std::shared_ptr<NixTU>> TUs;

  std::shared_ptr<const NixTU> getTU(std::string_view File) const {
    using lspserver::error;
    std::lock_guard G(TUsLock);
    if (!TUs.count(File)) [[unlikely]] {
      lspserver::elog("cannot get translation unit: {0}", File);
      return nullptr;
    }
    return TUs.lookup(File);
  }

  static std::shared_ptr<nixf::Node> getAST(const NixTU &TU) {
    using lspserver::error;
    if (!TU.ast()) {
      lspserver::elog("AST is null on this unit");
      return nullptr;
    }
    return TU.ast();
  }

  std::shared_ptr<const nixf::Node> getAST(std::string_view File) const {
    auto TU = getTU(File);
    return TU ? getAST(*TU) : nullptr;
  }

  static std::size_t threadPoolSize();
  boost::asio::thread_pool Pool{threadPoolSize()};
  boost::asio::thread_pool DiagnosticsPool{1};
  std::mutex PoolTasksLock;
  std::condition_variable PoolTasksCV;
  std::size_t PendingPoolTasks = 0; // GUARDED_BY(PoolTasksLock)

  void notePoolTaskStarted();
  void notePoolTaskFinished();
  void waitForPoolTasks();
  static bool useTrackedTestPool();

  template <typename Fn> void postToPool(Fn &&Action) {
    notePoolTaskStarted();
    boost::asio::post(
        Pool, [this, Action = std::forward<Fn>(Action)]() mutable {
          struct FinishGuard {
            Controller *Ctrl;
            ~FinishGuard() { Ctrl->notePoolTaskFinished(); }
          } Guard{this};
          Action();
        });
  }

  template <typename Fn> void postToDiagnosticsPool(Fn &&Action) {
    if (useTrackedTestPool()) {
      postToPool(std::forward<Fn>(Action));
      return;
    }
    boost::asio::post(DiagnosticsPool, std::forward<Fn>(Action));
  }

  /// Action right after a document is added (including updates).
  void actOnDocumentAdd(lspserver::PathRef File,
                        std::optional<int64_t> Version);

  void removeDocument(lspserver::PathRef File);

  void onInitialize(const lspserver::InitializeParams &Params,
                    lspserver::Callback<llvm::json::Value> Reply);

  void onInitialized(const lspserver::InitializedParams &Params);

  bool ReceivedShutdown = false;

  void onShutdown(const lspserver::NoParams &,
                  lspserver::Callback<std::nullptr_t> Reply);

  void onDocumentDidOpen(const lspserver::DidOpenTextDocumentParams &Params);

  void
  onDocumentDidChange(const lspserver::DidChangeTextDocumentParams &Params);

  void onDocumentDidClose(const lspserver::DidCloseTextDocumentParams &Params);
  void onDocumentDidSave(const lspserver::DidSaveTextDocumentParams &Params);

  void
  onCodeAction(const lspserver::CodeActionParams &Params,
               lspserver::Callback<std::vector<lspserver::CodeAction>> Reply);

  void onCodeActionResolve(const lspserver::CodeAction &Params,
                           lspserver::Callback<lspserver::CodeAction> Reply);

  void onHover(const lspserver::TextDocumentPositionParams &Params,
               lspserver::Callback<std::optional<lspserver::Hover>> Reply);

  void onDocumentSymbol(
      const lspserver::DocumentSymbolParams &Params,
      lspserver::Callback<std::vector<lspserver::DocumentSymbol>> Reply);

  void onFoldingRange(
      const lspserver::FoldingRangeParams &Params,
      lspserver::Callback<std::vector<lspserver::FoldingRange>> Reply);

  void onSemanticTokens(const lspserver::SemanticTokensParams &Params,
                        lspserver::Callback<lspserver::SemanticTokens> Reply);

  void
  onInlayHint(const lspserver::InlayHintsParams &Params,
              lspserver::Callback<std::vector<lspserver::InlayHint>> Reply);

  void onCompletion(const lspserver::CompletionParams &Params,
                    lspserver::Callback<lspserver::CompletionList> Reply);

  void
  onCompletionItemResolve(const lspserver::CompletionItem &Params,
                          lspserver::Callback<lspserver::CompletionItem> Reply);

  void onDefinition(const lspserver::TextDocumentPositionParams &Params,
                    lspserver::Callback<llvm::json::Value> Reply);

  void
  onReferences(const lspserver::TextDocumentPositionParams &Params,
               lspserver::Callback<std::vector<lspserver::Location>> Reply);

  void onDocumentHighlight(
      const lspserver::TextDocumentPositionParams &Params,
      lspserver::Callback<std::vector<lspserver::DocumentHighlight>> Reply);

  void onDocumentLink(
      const lspserver::DocumentLinkParams &Params,
      lspserver::Callback<std::vector<lspserver::DocumentLink>> Reply);

  std::set<nixf::Diagnostic::DiagnosticKind>
      SuppressedDiagnostics; // GUARDED_BY(SuppressedDiagnosticsLock)

  std::mutex SuppressedDiagnosticsLock;
  std::set<std::string>
      SuppressedNixdDiagnostics; // GUARDED_BY(SuppressedDiagnosticsLock)

  /// Update the suppressing set. There might be some invalid names, should be
  /// logged then.
  void updateSuppressed(const std::vector<std::string> &Sup);

  /// Determine whether or not this diagnostic is suppressed.
  bool isSuppressed(nixf::Diagnostic::DiagnosticKind Kind);
  bool isNixdDiagnosticSuppressed(std::string_view Code);
  bool noteOptionProviderChanged(std::string_view Name,
                                 std::uint64_t EvalGeneration);
  bool noteOptionProviderSettled(
      std::string_view Name, std::uint64_t EvalGeneration,
      std::optional<EvalExprError> Error = std::nullopt);
  bool allOptionProvidersReadyLocked() const;
  bool allOptionProvidersSettledLocked() const;
  bool waitForOptionProvidersReadyForTests();
  bool optionProvidersReadyForDiagnostics();
  bool optionProvidersSettledForDiagnostics();
  std::vector<OptionProviderRef> optionProviderSnapshot();
  std::vector<OptionProviderFailure> optionProviderFailureSnapshot();
  std::vector<ResolvedOptionField>
  completeOptions(const std::vector<std::string> &Scope,
                  const std::string &Prefix);
  std::vector<ResolvedOptionField>
  completeDerivedOptionsForFile(std::string_view File,
                                const std::vector<std::string> &Scope,
                                const std::string &Prefix);
  std::vector<ResolvedOptionField>
  completeDerivedOptions(const std::vector<std::string> &Scope,
                         const std::string &Prefix);
  std::vector<ResolvedOptionInfo>
  resolveOptionInfosForFile(std::string_view File,
                            const std::vector<std::string> &Scope);
  std::vector<ResolvedOptionInfo>
  resolveOptionInfos(const std::vector<std::string> &Scope);
  std::vector<ResolvedOptionInfo>
  resolveDerivedOptionInfosForFile(std::string_view File,
                                   const std::vector<std::string> &Scope);
  std::vector<ResolvedOptionInfo>
  resolveDerivedOptionInfos(const std::vector<std::string> &Scope);
  std::vector<lspserver::Location>
  optionDefinitionLocationsForFile(std::string_view File,
                                   const std::vector<std::string> &Scope,
                                   const std::vector<std::string> &FullScope);
  std::vector<lspserver::Location>
  optionDeclarationLocations(const std::vector<std::string> &Scope);
  std::vector<NixdDiagnostic> collectOptionDiagnostics(const NixTU &TU,
                                                       std::string_view File);
  void scheduleOptionDiagnostics(lspserver::PathRef File,
                                 std::optional<int64_t> Version,
                                 std::shared_ptr<NixTU> TU);
  void refreshDiagnostics();
  void onWaitForOptionsSettled(const llvm::json::Value &Params,
                               lspserver::Callback<llvm::json::Value> Reply);
  void
  publishDiagnostics(lspserver::PathRef File, std::optional<int64_t> Version,
                     std::string_view Src,
                     const std::vector<nixf::Diagnostic> &Diagnostics,
                     const std::vector<NixdDiagnostic> &NixdDiagnostics = {});

  void onRename(const lspserver::RenameParams &Params,
                lspserver::Callback<lspserver::WorkspaceEdit> Reply);

  void
  onPrepareRename(const lspserver::TextDocumentPositionParams &Params,
                  lspserver::Callback<std::optional<lspserver::Range>> Reply);

  void onFormat(const lspserver::DocumentFormattingParams &Params,
                lspserver::Callback<std::vector<lspserver::TextEdit>> Reply);

  //---------------------------------------------------------------------------/
  // Workspace features
  //---------------------------------------------------------------------------/
  void onDidChangeConfiguration(
      const lspserver::DidChangeConfigurationParams &Params);
  void onDidChangeWatchedFiles(
      const lspserver::DidChangeWatchedFilesParams &Params);

public:
  Controller(std::unique_ptr<lspserver::InboundPort> In,
             std::unique_ptr<lspserver::OutboundPort> Out);

  ~Controller() override;

  bool isReadyToEval() { return Eval && Eval->ready(); }
};

} // namespace nixd
