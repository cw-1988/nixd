/// \file
/// \brief Implementation of [Hover Request].
/// [Hover Request]:
/// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_hover

#include "AST.h"
#include "CheckReturn.h"
#include "Convert.h"

#include "nixd/Controller/Controller.h"
#include "Option/FlakeSchema.h"
#include "Option/Navigation.h"
#include "nixd/Protocol/AttrSet.h"

#include <boost/asio/post.hpp>

#include <llvm/Support/Error.h>

#include <semaphore>
#include <set>
#include <sstream>
#include <vector>

using namespace nixd;
using namespace llvm::json;
using namespace nixf;
using namespace lspserver;

namespace {

class OptionsHoverProvider {
  AttrSetClient &Client;

public:
  OptionsHoverProvider(AttrSetClient &Client) : Client(Client) {}
  std::optional<OptionDescription>
  resolveHover(const std::vector<std::string> &Scope) {
    std::binary_semaphore Ready(0);
    std::optional<OptionDescription> Desc;
    auto OnReply = [&Ready, &Desc](llvm::Expected<OptionInfoResponse> Resp) {
      if (Resp)
        Desc = *Resp;
      else
        elog("options hover: {0}", Resp.takeError());
      Ready.release();
    };

    Client.optionInfo(Scope, std::move(OnReply));
    Ready.acquire();

    return Desc;
  }
};

std::string renderOptionTypeInline(const OptionType &Type) {
  std::ostringstream OS;
  if (Type.Name)
    OS << "`" << *Type.Name << "`";
  if (Type.Description) {
    if (OS.tellp() > 0)
      OS << " - ";
    OS << *Type.Description;
  }
  return OS.str();
}

bool shouldHideSubOption(std::string_view Name) {
  return Name.starts_with("_");
}

void appendSubOptionList(std::ostringstream &OS, const OptionType &Type,
                         std::string_view Heading) {
  bool WroteHeading = false;
  for (const auto &[Name, Summary] : Type.KnownSubOptions) {
    if (shouldHideSubOption(Name))
      continue;

    if (!WroteHeading) {
      OS << "\n\n## " << Heading << "\n\n";
      WroteHeading = true;
    }

    OS << "- `" << Name << "`";
    if (auto It = Type.NestedTypes.find(Name); It != Type.NestedTypes.end()) {
      const std::string Rendered = renderOptionTypeInline(It->second);
      if (!Rendered.empty())
        OS << ": " << Rendered;
    }

    std::vector<std::string_view> Flags;
    if (Summary.Required)
      Flags.emplace_back("required");
    if (Summary.HasDefault)
      Flags.emplace_back("default");
    if (Summary.HasEmptyValue)
      Flags.emplace_back("empty value");
    if (!Flags.empty()) {
      OS << " (";
      for (size_t I = 0; I < Flags.size(); ++I) {
        if (I)
          OS << ", ";
        OS << Flags[I];
      }
      OS << ")";
    }
    OS << "\n";
  }

  if (!Type.KnownSubOptionsComplete) {
    if (!WroteHeading) {
      OS << "\n\n## " << Heading << "\n\n";
      WroteHeading = true;
    }
    OS << "- ...\n";
  }
}

void appendNestedSubOptions(std::ostringstream &OS, const OptionType &Type,
                            std::string_view Heading, unsigned Depth,
                            std::set<std::string> &Seen) {
  if (Depth > 4)
    return;

  const std::string Fingerprint =
      std::string(Heading) + "\n" + Type.Name.value_or("") + "\n" +
      Type.Description.value_or("");
  std::string RichFingerprint = Fingerprint;
  for (const auto &[Name, Summary] : Type.KnownSubOptions) {
    (void)Summary;
    RichFingerprint += "\nsub:" + Name;
  }
  for (const auto &[Name, Child] : Type.NestedTypes) {
    (void)Child;
    RichFingerprint += "\ntype:" + Name;
  }
  if (!Seen.insert(RichFingerprint).second)
    return;

  if (!Type.KnownSubOptions.empty() || !Type.KnownSubOptionsComplete)
    appendSubOptionList(OS, Type, Heading);

  const std::string LowerName = option_navigation::lowerTypeName(Type);

  if (LowerName == "nullor" || LowerName == "unique") {
    std::optional<OptionType> Elem =
        LowerName == "nullor"
            ? option_navigation::nullOrTypeFor(Type)
            : option_navigation::elemTypeFor(Type, LowerName);
    if (Elem)
      appendNestedSubOptions(OS, *Elem, Heading, Depth + 1, Seen);
    return;
  }

  if (LowerName == "listof" || option_navigation::isNonEmptyListType(Type)) {
    if (std::optional<OptionType> Elem =
            option_navigation::elemTypeFor(Type, LowerName))
      appendNestedSubOptions(OS, *Elem, "Element Options", Depth + 1, Seen);
    return;
  }

  if (LowerName == "loaof") {
    if (std::optional<OptionType> Elem =
            option_navigation::elemTypeFor(Type, LowerName))
      appendNestedSubOptions(OS, *Elem, "Element Options", Depth + 1, Seen);
    return;
  }

  if (LowerName == "functionto") {
    if (std::optional<OptionType> Result =
            option_navigation::functionResultTypeFor(Type))
      appendNestedSubOptions(OS, *Result, "Result Options", Depth + 1, Seen);
    return;
  }

  if (LowerName == "attrsof" || LowerName == "lazyattrsof" ||
      LowerName == "attrswith") {
    if (std::optional<OptionType> Elem =
            option_navigation::elemTypeFor(Type, LowerName))
      appendNestedSubOptions(OS, *Elem, "Attribute Options", Depth + 1, Seen);
    return;
  }

  for (const OptionType &Alternative :
       option_navigation::alternativeTypesFor(Type, LowerName))
    appendNestedSubOptions(OS, Alternative, "Alternative Options", Depth + 1,
                           Seen);
}

std::string mkOptionMarkdown(const OptionDescription &Desc) {
  std::ostringstream OS;

  OS << "## Type\n\n";
  if (Desc.Type) {
    const std::string Rendered = renderOptionTypeInline(*Desc.Type);
    OS << (Rendered.empty() ? "? (missing type)" : Rendered);
  } else {
    OS << "? (missing type)";
  }

  if (Desc.Description)
    OS << "\n\n## Description\n\n" << *Desc.Description;

  if (Desc.Type) {
    std::set<std::string> Seen;
    appendNestedSubOptions(OS, *Desc.Type, "Options", 0, Seen);
  }

  return OS.str();
}

/// \brief Provide package information, library information ... , from nixpkgs.
class NixpkgsHoverProvider {
  AttrSetClient &NixpkgsClient;

  /// \brief Make markdown documentation by package description
  ///
  /// FIXME: there are many markdown generation in language server.
  /// Maybe we can add structured generating first?
  static std::string mkMarkdown(const PackageDescription &Package) {
    std::ostringstream OS;
    // Make each field a new section

    if (Package.Name) {
      OS << "`" << *Package.Name << "`";
      OS << "\n";
    }

    // Make links to homepage.
    if (Package.Homepage) {
      OS << "[homepage](" << *Package.Homepage << ")";
      OS << "\n";
    }

    if (Package.Description) {
      OS << "## Description"
         << "\n\n";
      OS << *Package.Description;
      OS << "\n\n";

      if (Package.LongDescription) {
        OS << "\n\n";
        OS << *Package.LongDescription;
        OS << "\n\n";
      }
    }

    return OS.str();
  }

  /// \brief Make markdown including both package and value description
  static std::string mkMarkdown(const AttrPathInfoResponse &Info) {
    std::ostringstream OS;
    // Package section (if available)
    OS << mkMarkdown(Info.PackageDesc);

    // Value description section
    if (Info.ValueDesc) {
      const auto &VD = *Info.ValueDesc;
      if (!OS.str().empty())
        OS << "\n";
      if (!VD.Doc.empty()) {
        OS << VD.Doc << "\n\n";
      }
      if (VD.Arity != 0) {
        OS << "**Arity:** " << VD.Arity << "\n";
      }
      if (!VD.Args.empty()) {
        OS << "**Args:** ";
        for (size_t Idx = 0; Idx < VD.Args.size(); ++Idx) {
          OS << "`" << VD.Args[Idx] << "`";
          if (Idx + 1 < VD.Args.size())
            OS << ", ";
        }
        OS << "\n";
      }
    }

    return OS.str();
  }

public:
  NixpkgsHoverProvider(AttrSetClient &NixpkgsClient)
      : NixpkgsClient(NixpkgsClient) {}

  std::optional<std::string> resolveSelector(const nixd::Selector &Sel) {
    std::binary_semaphore Ready(0);
    std::optional<AttrPathInfoResponse> Info;
    auto OnReply = [&Ready, &Info](llvm::Expected<AttrPathInfoResponse> Resp) {
      if (Resp)
        Info = *Resp;
      else
        elog("nixpkgs provider: {0}", Resp.takeError());
      Ready.release();
    };
    NixpkgsClient.attrpathInfo(Sel, std::move(OnReply));
    Ready.acquire();

    if (!Info)
      return std::nullopt;

    return mkMarkdown(*Info);
  }
};

/// \brief Get nixpkgs hover info from a selector.
std::optional<Hover> hoverNixpkgsSelector(const Selector &Sel,
                                          const nixf::Node &N,
                                          const VariableLookupAnalysis &VLA,
                                          const ParentMapAnalysis &PM,
                                          AttrSetClient &NixpkgsClient,
                                          llvm::StringRef Src) {
  try {
    // Ask nixpkgs provider information about this selector.
    NixpkgsHoverProvider NHP(NixpkgsClient);
    if (std::optional<std::string> Doc = NHP.resolveSelector(Sel)) {
      return Hover{
          .contents =
              MarkupContent{
                  .kind = MarkupKind::Markdown,
                  .value = std::move(*Doc),
              },
          .range = toLSPRange(Src, N.range()),
      };
    }
  } catch (std::exception &E) {
    elog("hover/idiom: {0}", E.what());
  }
  return std::nullopt;
}

/// \brief Get hover info for ExprVar.
std::optional<Hover> hoverVar(const ExprVar &Var,
                              const VariableLookupAnalysis &VLA,
                              const ParentMapAnalysis &PM,
                              AttrSetClient &NixpkgsClient,
                              llvm::StringRef Src) {
  try {
    Selector Sel = idioms::mkVarSelector(Var, VLA, PM);
    return hoverNixpkgsSelector(Sel, Var, VLA, PM, NixpkgsClient, Src);
  } catch (std::exception &E) {
    elog("hover/idiom/selector: {0}", E.what());
  }
  return std::nullopt;
}

/// \brief Get hover info for ExprSelect.
std::optional<Hover> hoverSelect(const ExprSelect &Sel,
                                 const VariableLookupAnalysis &VLA,
                                 const ParentMapAnalysis &PM,
                                 AttrSetClient &NixpkgsClient,
                                 llvm::StringRef Src) {
  try {
    Selector S = idioms::mkSelector(Sel, VLA, PM);
    return hoverNixpkgsSelector(S, Sel, VLA, PM, NixpkgsClient, Src);
  } catch (std::exception &E) {
    elog("hover/idiom/selector: {0}", E.what());
  }
  return std::nullopt;
}

} // namespace

void Controller::onHover(const TextDocumentPositionParams &Params,
                         Callback<std::optional<Hover>> Reply) {
  using CheckTy = std::optional<Hover>;
  auto Action = [Reply = std::move(Reply),
                 File = std::string(Params.textDocument.uri.file()),
                 RawPos = Params.position, this]() mutable {
    return Reply([&]() -> llvm::Expected<CheckTy> {
      const auto TU = CheckDefault(getTU(File));
      const auto AST = CheckDefault(getAST(*TU));
      const auto Pos = nixf::Position{RawPos.line, RawPos.character};
      const auto &N = *CheckDefault(AST->descend({Pos, Pos}));

      const auto Name = std::string(N.name());
      const auto &VLA = *TU->variableLookup();
      const auto &PM = *TU->parentMap();

      const auto &UpExpr = *CheckDefault(PM.upExpr(N));

      if (UpExpr.kind() == Node::NK_ExprAttrs) {
        auto Scope = std::vector<std::string>();
        const auto R = findAttrPathForOptions(N, PM, Scope);
        if (R == FindAttrPathResult::OK) {
          if (flake_schema::isFlakeFile(File) &&
              flake_schema::isInsideOutputsBody(N, PM))
            Scope = flake_schema::outputsBodyScope(Scope);
          for (const ResolvedOptionInfo &Info :
               resolveDerivedOptionInfosForFile(File, Scope)) {
            const OptionDescription &Desc = Info.Description;
            std::string Docs = mkOptionMarkdown(Desc);
            return Hover{
                .contents =
                    MarkupContent{
                        .kind = MarkupKind::Markdown,
                        .value = std::move(Docs),
                    },
                .range = toLSPRange(TU->src(), N.range()),
            };
          }
        }
      }

      // Try to get hover info from nixpkgs.
      if (auto *Client = nixpkgsClient(); Client) {
        switch (UpExpr.kind()) {
        case Node::NK_ExprVar: {
          const auto &Var = static_cast<const ExprVar &>(UpExpr);
          if (auto H = hoverVar(Var, VLA, PM, *Client, TU->src()))
            return *H;
          break;
        }
        case Node::NK_ExprSelect: {
          const auto &Sel = static_cast<const ExprSelect &>(UpExpr);
          if (auto H = hoverSelect(Sel, VLA, PM, *Client, TU->src()))
            return *H;
          break;
        }
        default:
          break;
        }
      }

      return std::nullopt;
    }());
  };
  postToPool(std::move(Action));
}
