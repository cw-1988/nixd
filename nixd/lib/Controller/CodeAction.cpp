/// \file
/// \brief Implementation of [Code Action].
/// [Code Action]:
/// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_codeAction

#include "CheckReturn.h"
#include "Convert.h"

#include "CodeActions/AddToFormals.h"
#include "CodeActions/AttrName.h"
#include "CodeActions/ConvertToInherit.h"
#include "CodeActions/ExtractToFile.h"
#include "CodeActions/FlattenAttrs.h"
#include "CodeActions/InheritToBinding.h"
#include "CodeActions/JsonToNix.h"
#include "CodeActions/NoogleDoc.h"
#include "CodeActions/PackAttrs.h"
#include "CodeActions/RewriteString.h"
#include "CodeActions/WithToLet.h"

#include "nixd/Controller/Controller.h"
#include "nixd/Controller/ModuleInputInspect.h"

#include <boost/asio/post.hpp>
#include <llvm/Support/JSON.h>

#include <functional>
#include <semaphore>

namespace nixd {

using namespace llvm::json;
using namespace lspserver;

namespace {

void addInspectModuleInputAction(
    const nixf::Node &N, const nixf::ParentMapAnalysis &PM,
    const nixf::VariableLookupAnalysis &VLA, std::string_view File,
    const std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)> &Resolve,
    std::vector<lspserver::CodeAction> &Actions) {
  std::optional<ModuleInputInspectContext> Context =
      findModuleInputInspectContext(N, VLA, PM, Resolve);
  if (!Context)
    return;

  Actions.emplace_back(lspserver::CodeAction{
      .title = "Inspect module input `" + Context->Input + "`",
      .kind = std::string(lspserver::CodeAction::REFACTOR_KIND),
      .data = llvm::json::Object{
          {"inspectModuleInput", true},
          {"file", std::string(File)},
          {"input", Context->Input},
          {"scope", moduleInputInspectScopeToJSON(Context->Scope)},
          {"sources", moduleInputInspectSourcesToJSON(Context->Sources)},
      },
  });
}

std::optional<std::vector<std::string>>
nixpkgsScopeForModuleInput(std::string_view Input) {
  if (Input == "pkgs")
    return std::vector<std::string>{};
  if (Input == "lib")
    return std::vector<std::string>{"lib"};
  return std::nullopt;
}

} // namespace

void Controller::onCodeAction(const lspserver::CodeActionParams &Params,
                              Callback<std::vector<CodeAction>> Reply) {
  using CheckTy = std::vector<CodeAction>;
  std::string File(Params.textDocument.uri.file());
  Range Range = Params.range;
  auto Action = [Reply = std::move(Reply), File, Range, this]() mutable {
    return Reply([&]() -> llvm::Expected<CheckTy> {
      const auto TU = CheckDefault(getTU(File));

      const auto &Diagnostics = TU->diagnostics();
      auto Actions = std::vector<CodeAction>();
      Actions.reserve(Diagnostics.size());
      std::string FileURI = URIForFile::canonicalize(File, File).uri();

      for (const nixf::Diagnostic &D : Diagnostics) {
        auto DRange = toLSPRange(TU->src(), D.range());
        if (!Range.overlap(DRange))
          continue;

        // Determine if this diagnostic's fixes should be preferred
        bool IsPreferred = false;
        switch (D.kind()) {
        case nixf::Diagnostic::DK_UnusedDefLet:
          IsPreferred = true;
          break;
        default:
          break;
        }

        // Add fixes.
        for (const nixf::Fix &F : D.fixes()) {
          std::vector<TextEdit> Edits;
          Edits.reserve(F.edits().size());
          for (const nixf::TextEdit &TE : F.edits()) {
            Edits.emplace_back(TextEdit{
                .range = toLSPRange(TU->src(), TE.oldRange()),
                .newText = std::string(TE.newText()),
            });
          }
          using Changes = std::map<std::string, std::vector<TextEdit>>;
          WorkspaceEdit WE{.changes = Changes{
                               {FileURI, std::move(Edits)},
                           }};
          Actions.emplace_back(CodeAction{
              .title = F.message(),
              .kind = std::string(CodeAction::QUICKFIX_KIND),
              .isPreferred = IsPreferred,
              .edit = std::move(WE),
          });
        }
      }

      // Add refactoring code actions based on cursor position
      if (TU->ast() && TU->parentMap()) {
        nixf::PositionRange NixfRange = toNixfRange(Range);
        if (const nixf::Node *N = TU->ast()->descend(NixfRange)) {
          addAttrNameActions(*N, *TU->parentMap(), FileURI, TU->src(), Actions);
          addConvertToInheritAction(*N, *TU->parentMap(), FileURI, TU->src(),
                                    Actions);
          addFlattenAttrsAction(*N, *TU->parentMap(), FileURI, TU->src(),
                                Actions);
          addPackAttrsAction(*N, *TU->parentMap(), FileURI, TU->src(), Actions);
          addInheritToBindingAction(*N, *TU->parentMap(), FileURI, TU->src(),
                                    Actions);
          addNoogleDocAction(*N, *TU->parentMap(), Actions);
          addRewriteStringAction(*N, *TU->parentMap(), FileURI, TU->src(),
                                 Actions);

          // Extract to file requires variable lookup analysis
          if (TU->variableLookup()) {
            addExtractToFileAction(*N, *TU->parentMap(), *TU->variableLookup(),
                                   FileURI, TU->src(), Actions);
            auto Resolve = [&](const std::vector<std::string> &Scope) {
              return resolveDerivedOptionInfosForFile(File, Scope);
            };
            addInspectModuleInputAction(*N, *TU->parentMap(),
                                        *TU->variableLookup(), File, Resolve,
                                        Actions);
          }
          // Add with-to-let action (requires VLA for variable tracking)
          if (TU->variableLookup()) {
            addWithToLetAction(*N, *TU->parentMap(), *TU->variableLookup(),
                               FileURI, TU->src(), Actions);
          }
          // Add undefined variable to formals action (requires VLA)
          if (TU->variableLookup()) {
            addToFormalsAction(*N, *TU->parentMap(), *TU->variableLookup(),
                               FileURI, TU->src(), Actions);
          }
        }
      }

      // Selection-based actions (work on arbitrary text, not AST nodes)
      addJsonToNixAction(TU->src(), Range, FileURI, Actions);

      return Actions;
    }());
  };
  postToPool(std::move(Action));
}

void Controller::onCodeActionResolve(const lspserver::CodeAction &Params,
                                     Callback<CodeAction> Reply) {
  auto Action = [Reply = std::move(Reply), Params, this]() mutable {
    // Check if this is a Noogle documentation action
    if (Params.data) {
      const auto *DataObj = Params.data->getAsObject();
      if (DataObj) {
        auto NoogleUrl = DataObj->getString("noogleUrl");
        if (NoogleUrl) {
          // Call window/showDocument to open the URL in external browser
          ShowDocumentParams ShowParams;
          ShowParams.externalUri = NoogleUrl->str();
          ShowParams.external = true;

          ShowDocument(
              ShowParams, [](llvm::Expected<ShowDocumentResult> Result) {
                if (!Result) {
                  lspserver::elog("Failed to open Noogle documentation: {0}",
                                  Result.takeError());
                }
              });
        }

        auto Inspect = DataObj->getBoolean("inspectModuleInput");
        if (Inspect && *Inspect) {
          auto File = DataObj->getString("file");
          auto Input = DataObj->getString("input");
          const llvm::json::Value *ScopeValue = DataObj->get("scope");
          const llvm::json::Value *SourcesValue = DataObj->get("sources");
          if (File && Input && ScopeValue && SourcesValue) {
            std::optional<std::vector<std::string>> Scope =
                moduleInputInspectScopeFromJSON(*ScopeValue);
            std::optional<std::vector<std::string>> Sources =
                moduleInputInspectScopeFromJSON(*SourcesValue);
            if (Scope && Sources) {
              auto Complete = [&](const std::vector<std::string> &S,
                                  const std::string &Prefix) {
                return completeDerivedOptionsForFile(*File, S, Prefix);
              };
              ModuleInputAttrCompleter CompleteAttrs;
              if (std::optional<std::vector<std::string>> NixpkgsScope =
                      nixpkgsScopeForModuleInput(*Input)) {
                CompleteAttrs =
                    [this, Root = std::move(*NixpkgsScope)](
                        const std::vector<std::string> &Scope,
                        const std::string &Prefix) mutable {
                      std::vector<std::string> FullScope = Root;
                      FullScope.insert(FullScope.end(), Scope.begin(),
                                       Scope.end());
                      std::binary_semaphore Ready(0);
                      std::vector<std::string> Names;
                      auto OnReply =
                          [&Ready, &Names](
                              llvm::Expected<AttrPathCompleteResponse> Resp) {
                            if (Resp)
                              Names = std::move(*Resp);
                            else
                              llvm::consumeError(Resp.takeError());
                            Ready.release();
                          };
                      nixpkgsClient()->attrpathComplete(
                          AttrPathCompleteParams{
                              .Scope = std::move(FullScope), .Prefix = Prefix},
                          std::move(OnReply));
                      Ready.acquire();
                      return Names;
                    };
              }
              std::string Content =
                  renderModuleInputInspectionDocument(*Input, *Scope, *Sources,
                                                      *File, Complete,
                                                      CompleteAttrs);
              std::filesystem::path Path =
                  writeModuleInputInspectionFile(*Input, *File,
                                                 std::move(Content));

              ShowDocumentParams ShowParams;
              const std::string PathStr = Path.string();
              ShowParams.uri = URIForFile::canonicalize(PathStr, PathStr);
              ShowParams.takeFocus = true;
              ShowDocument(
                  ShowParams, [](llvm::Expected<ShowDocumentResult> Result) {
                    if (!Result) {
                      lspserver::elog(
                          "Failed to open module input inspection: {0}",
                          Result.takeError());
                    }
                  });
            }
          }
        }
      }
    }

    // Return the resolved code action (unchanged for Noogle actions since
    // the work is done via showDocument)
    Reply(Params);
  };
  postToPool(std::move(Action));
}

} // namespace nixd
