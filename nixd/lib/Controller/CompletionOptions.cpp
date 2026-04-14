#include "CompletionOptions.h"
#include "AST.h"
#include "Convert.h"
#include "OptionDiagnosticsSupport.h"
#include "OptionTypeNavigation.h"

#include "lspserver/Protocol.h"

#include "nixd/Controller/Option.h"
#include "nixd/Eval/AttrSetClient.h"
#include "nixd/Protocol/AttrSet.h"

#include <llvm/Support/Error.h>
#include <llvm/Support/FormatVariadic.h>

#include <cctype>
#include <memory>
#include <optional>
#include <semaphore>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace nixd;
using namespace lspserver;
using namespace nixf;

namespace nixd::completion {
namespace {

constexpr CompletionItemKind OptionKind = CompletionItemKind::Constructor;
constexpr CompletionItemKind OptionAttrKind = CompletionItemKind::Class;

bool isBooleanOption(const OptionDescription &Desc) {
  if (!Desc.Type)
    return false;
  const std::optional<ParsedOptionType> Parsed = parseOptionType(*Desc.Type);
  return Parsed && Parsed->acceptsBoolean();
}

std::string escapeNixString(std::string_view Origin) {
  std::string Ret;
  Ret.reserve(Origin.size());
  for (size_t I = 0; I < Origin.size(); ++I) {
    char Ch = Origin[I];
    switch (Ch) {
    case '\\':
      Ret += "\\\\";
      break;
    case '"':
      Ret += "\\\"";
      break;
    case '\n':
      Ret += "\\n";
      break;
    case '\r':
      Ret += "\\r";
      break;
    case '\t':
      Ret += "\\t";
      break;
    case '$':
      if (I + 1 < Origin.size() && Origin[I + 1] == '{')
        Ret += "\\$";
      else
        Ret += Ch;
      break;
    default:
      Ret += Ch;
      break;
    }
  }
  return Ret;
}

std::string toLowerCopy(std::string_view S) {
  std::string Lower;
  Lower.reserve(S.size());
  for (char C : S)
    Lower.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(C))));
  return Lower;
}

struct EnumCompletionValue {
  std::string Label;
  std::string FilterText;
};

std::optional<EnumCompletionValue>
enumCompletionValue(const OptionType::EnumValue &Value) {
  if (Value.String) {
    std::string Quoted = "\"" + escapeNixString(*Value.String) + "\"";
    return EnumCompletionValue{.Label = std::move(Quoted),
                               .FilterText = *Value.String};
  }
  if (Value.Integer) {
    std::string Text = std::to_string(*Value.Integer);
    return EnumCompletionValue{.Label = Text, .FilterText = std::move(Text)};
  }
  if (Value.Boolean) {
    std::string Text = *Value.Boolean ? "true" : "false";
    return EnumCompletionValue{.Label = Text, .FilterText = std::move(Text)};
  }
  if (Value.IsNull)
    return EnumCompletionValue{.Label = "null", .FilterText = "null"};
  return std::nullopt;
}

/// \brief Provide completion list by nixpkgs module system (options).
class OptionCompletionProvider {
  // Whether the client supports code snippets.
  bool ClientSupportSnippet;

  static std::string escapeCharacters(const std::set<char> &Charset,
                                      const std::string &Origin) {
    // Escape characters listed in charset.
    std::string Ret;
    Ret.reserve(Origin.size());
    for (const auto Ch : Origin) {
      if (Charset.contains(Ch)) {
        Ret += "\\";
        Ret += Ch;
      } else {
        Ret += Ch;
      }
    }
    return Ret;
  }

  void fillInsertText(CompletionItem &Item, const std::string &Name,
                      const OptionDescription &Desc) const {
    std::string Example = Desc.Example.value_or("");
    if (Example.empty() && isBooleanOption(Desc))
      Example = "true";

    if (!ClientSupportSnippet) {
      Item.insertTextFormat = InsertTextFormat::PlainText;
      Item.insertText = Name + " = " + Example + ";";
      return;
    }
    Item.insertTextFormat = InsertTextFormat::Snippet;
    Item.insertText = Name + " = " +
                      "${1:" + escapeCharacters({'\\', '$', '}'}, Example) +
                      "}" + ";";
  }

public:
  OptionCompletionProvider(bool ClientSupportSnippet)
      : ClientSupportSnippet(ClientSupportSnippet) {}

  void completeOptions(const std::vector<ResolvedOptionField> &Fields,
                       std::vector<CompletionItem> &Items) {
    for (const ResolvedOptionField &Resolved : Fields) {
      const nixd::OptionField &Field = Resolved.Field;
      CompletionItem Item;

      Item.label = Field.Name;
      Item.detail = Resolved.ProviderName;

      if (Field.Description) {
        const OptionDescription &Desc = *Field.Description;
        Item.kind = OptionKind;
        fillInsertText(Item, Field.Name, Desc);
        Item.documentation = MarkupContent{
            .kind = MarkupKind::Markdown,
            .value = Desc.Description.value_or(""),
        };
        Item.detail += " | "; // separator between origin and type desc.
        if (Desc.Type) {
          std::string TypeName = Desc.Type->Name.value_or("");
          std::string TypeDesc = Desc.Type->Description.value_or("");
          Item.detail += llvm::formatv("{0} ({1})", TypeName, TypeDesc);
        } else {
          Item.detail += "? (missing type)";
        }
        addItem(Items, std::move(Item));
      } else {
        Item.kind = OptionAttrKind;
        addItem(Items, std::move(Item));
      }
    }
  }
};

option_navigation::ChildKind toNavigationKind(OptionValueChildKind Kind) {
  switch (Kind) {
  case OptionValueChildKind::ListElement:
    return option_navigation::ChildKind::ListElement;
  case OptionValueChildKind::AttrValue:
    return option_navigation::ChildKind::AttrValue;
  case OptionValueChildKind::FunctionBody:
    return option_navigation::ChildKind::FunctionBody;
  }
  return option_navigation::ChildKind::AttrValue;
}

std::vector<option_navigation::ChildStep>
toNavigationPath(const OptionValueContext &Context) {
  std::vector<option_navigation::ChildStep> Path;
  Path.reserve(Context.ValuePath.size());
  for (const OptionValueChildStep &Step : Context.ValuePath)
    Path.push_back(option_navigation::ChildStep{
        .Kind = toNavigationKind(Step.Kind), .Name = Step.Name});
  return Path;
}

std::string optionValuePrefix(const OptionValueContext &Context) {
  const Expr *Value = Context.CompletionExpr;
  if (!Value && Context.Binding)
    Value = Context.Binding->value().get();
  if (!Value)
    return "";

  if (Value->kind() == Node::NK_ExprVar)
    return static_cast<const ExprVar *>(Value)->id().name();

  if (Value->kind() == Node::NK_ExprString) {
    const auto *String = static_cast<const ExprString *>(Value);
    if (String->isLiteral())
      return String->literal();
  }

  if (std::optional<std::string> Path =
          option_diagnostics::pathLiteralText(*Value))
    return *Path;

  return "";
}

lspserver::Range cursorRange(const OptionValueContext &Context) {
  lspserver::Position Pos{.line = Context.Pos.line(),
                          .character = Context.Pos.column()};
  return lspserver::Range{.start = Pos, .end = Pos};
}

void addOptionValueInsertEdits(const OptionValueContext &Context,
                               llvm::StringRef Src, std::string_view Value,
                               CompletionItem &Item) {
  const Expr *Expr = Context.CompletionExpr;
  if (!Expr)
    return;

  if (Expr->kind() != Node::NK_ExprString)
    return;
  Item.textEdit = lspserver::TextEdit{
      .range = toLSPRange(Src, Expr->range()),
      .newText = std::string(Value),
  };
}

bool isSelectCompletionContext(const OptionValueContext &Context) {
  return Context.CompletionExpr &&
         Context.CompletionExpr->kind() == Node::NK_ExprSelect;
}

std::optional<OptionType> elemTypeFor(const OptionType &Type) {
  return option_navigation::elemTypeFor(Type,
                                        option_navigation::lowerTypeName(Type));
}

std::vector<OptionType> alternativeTypesFor(const OptionType &Type,
                                            std::string_view LowerName) {
  return option_navigation::alternativeTypesFor(Type, LowerName);
}

bool completeEnumOptionValue(const OptionValueContext &Context,
                             const OptionType &Type, const std::string &Prefix,
                             llvm::StringRef Src,
                             std::set<std::string> &SeenLabels,
                             std::vector<CompletionItem> &Items) {
  if (Type.EnumValues.empty())
    return false;

  for (const OptionType::EnumValue &Value : Type.EnumValues) {
    std::optional<EnumCompletionValue> Completion = enumCompletionValue(Value);
    if (!Completion || !Completion->FilterText.starts_with(Prefix) ||
        SeenLabels.contains(Completion->Label))
      continue;

    CompletionItem Item{
        .label = Completion->Label,
        .kind = CompletionItemKind::EnumMember,
        .detail = "enum option value",
        .filterText = Completion->FilterText,
    };
    addOptionValueInsertEdits(Context, Src, Completion->Label, Item);
    SeenLabels.insert(Completion->Label);
    addItem(Items, std::move(Item));
  }

  return true;
}

bool completeBooleanOptionValue(const OptionValueContext &Context,
                                const OptionType &Type,
                                const std::string &Prefix, llvm::StringRef Src,
                                std::set<std::string> &SeenLabels,
                                std::vector<CompletionItem> &Items) {
  const std::optional<ParsedOptionType> Parsed = parseOptionType(Type);
  if (!Parsed || !Parsed->acceptsBoolean())
    return false;

  for (std::string_view Value : {"true", "false"}) {
    if (!Value.starts_with(Prefix) || SeenLabels.contains(std::string(Value)))
      continue;
    CompletionItem Item{
        .label = std::string(Value),
        .kind = CompletionItemKind::Keyword,
        .detail = "boolean option value",
    };
    addOptionValueInsertEdits(Context, Src, Value, Item);
    SeenLabels.insert(std::string(Value));
    addItem(Items, std::move(Item));
  }
  return true;
}

bool completeNullOptionValue(const OptionValueContext &Context,
                             const std::string &Prefix, llvm::StringRef Src,
                             std::set<std::string> &SeenLabels,
                             std::vector<CompletionItem> &Items) {
  if (!std::string_view("null").starts_with(Prefix) ||
      SeenLabels.contains("null"))
    return false;

  CompletionItem Item{
      .label = "null",
      .kind = CompletionItemKind::Keyword,
      .detail = "null option value",
  };
  addOptionValueInsertEdits(Context, Src, "null", Item);
  SeenLabels.insert("null");
  addItem(Items, std::move(Item));
  return true;
}

struct PackageCompletionReplyState {
  std::binary_semaphore Ready{0};
  std::vector<std::string> Names;
};

struct PathCompletionInfo {
  bool Accepts = false;
  bool Absolute = false;
  bool InStore = false;
};

PathCompletionInfo pathCompletionInfoFor(const OptionType &Type) {
  const std::string LowerName = option_navigation::lowerTypeName(Type);
  PathCompletionInfo Info;
  if (Type.Path) {
    Info.Accepts = true;
    Info.Absolute = Type.Path->Absolute || Type.Path->InStore;
    Info.InStore = Type.Path->InStore;
    return Info;
  }
  if (LowerName == "path" || LowerName == "pathinstore" ||
      LowerName == "absolutepath") {
    Info.Accepts = true;
    Info.Absolute = LowerName == "pathinstore" || LowerName == "absolutepath";
    Info.InStore = LowerName == "pathinstore";
    return Info;
  }
  if (Type.Description) {
    const std::string Lower = toLowerCopy(*Type.Description);
    if (Lower == "path") {
      Info.Accepts = true;
      return Info;
    }
    if (Lower == "absolute path" || Lower == "store path") {
      Info.Accepts = true;
      Info.Absolute = true;
      Info.InStore = Lower == "store path";
      return Info;
    }
  }
  for (const OptionType &Alternative : alternativeTypesFor(Type, LowerName)) {
    PathCompletionInfo AlternativeInfo = pathCompletionInfoFor(Alternative);
    Info.Accepts |= AlternativeInfo.Accepts;
    Info.Absolute |= AlternativeInfo.Absolute;
    Info.InStore |= AlternativeInfo.InStore;
  }
  if (Info.Accepts)
    return Info;
  if (LowerName == "nullor" || LowerName == "unique") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type))
      return pathCompletionInfoFor(*Elem);
  }
  return Info;
}

bool typeAcceptsPath(const OptionType &Type) {
  return pathCompletionInfoFor(Type).Accepts;
}

bool typeAcceptsPackage(const OptionType &Type) {
  const std::string LowerName = Type.Name ? toLowerCopy(*Type.Name) : "";
  if (LowerName == "package")
    return true;
  if (Type.Description &&
      toLowerCopy(*Type.Description).find("package") != std::string::npos)
    return true;
  for (const OptionType &Alternative : alternativeTypesFor(Type, LowerName))
    if (typeAcceptsPackage(Alternative))
      return true;
  if (LowerName == "nullor" || LowerName == "unique") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type))
      return typeAcceptsPackage(*Elem);
  }
  return false;
}

bool completePathOptionValue(const OptionValueContext &Context,
                             const OptionType &Type, const std::string &Prefix,
                             llvm::StringRef Src,
                             std::set<std::string> &SeenLabels,
                             std::vector<CompletionItem> &Items) {
  const PathCompletionInfo Info = pathCompletionInfoFor(Type);
  if (!Info.Accepts)
    return false;

  std::vector<std::string_view> Values;
  if (Info.InStore)
    Values = {"/nix/store/"};
  else if (Info.Absolute)
    Values = {"/"};
  else
    Values = {"./"};

  bool Added = false;
  for (std::string_view Value : Values) {
    if (!Value.starts_with(Prefix) || SeenLabels.contains(std::string(Value)))
      continue;
    CompletionItem Item{
        .label = std::string(Value),
        .kind = Value == "./" ? CompletionItemKind::Folder
                              : CompletionItemKind::File,
        .detail = "path option value",
    };
    addOptionValueInsertEdits(Context, Src, Value, Item);
    SeenLabels.insert(std::string(Value));
    addItem(Items, std::move(Item));
    Added = true;
  }
  return Added;
}

bool completePackageOptionValue(const OptionValueContext &Context,
                                const OptionType &Type,
                                const std::string &Prefix, llvm::StringRef Src,
                                AttrSetClient *NixpkgsClient,
                                std::set<std::string> &SeenLabels,
                                std::vector<CompletionItem> &Items) {
  if (!NixpkgsClient || !typeAcceptsPackage(Type) ||
      isSelectCompletionContext(Context) || Prefix.starts_with("pkgs."))
    return false;

  auto State = std::make_shared<PackageCompletionReplyState>();
  auto OnReply = [State](llvm::Expected<AttrPathCompleteResponse> Resp) {
    if (Resp)
      State->Names = *Resp;
    else
      llvm::consumeError(Resp.takeError());
    State->Ready.release();
  };
  NixpkgsClient->attrpathComplete(
      AttrPathCompleteParams{.Scope = {}, .Prefix = Prefix},
      std::move(OnReply));
  State->Ready.acquire();

  bool Added = false;
  for (const std::string &Name : State->Names) {
    if (!Name.starts_with(Prefix))
      continue;
    const std::string Label = "pkgs." + Name;
    if (SeenLabels.contains(Label))
      continue;
    CompletionItem Item{
        .label = Label,
        .kind = CompletionItemKind::Field,
        .detail = "package option value",
        .filterText = Name,
        .insertText = Label,
    };
    addOptionValueInsertEdits(Context, Src, Label, Item);
    SeenLabels.insert(Label);
    addItem(Items, std::move(Item));
    Added = true;
  }
  return Added;
}

std::string snippetPlaceholderForType(const OptionType &Type) {
  const std::string LowerName = Type.Name ? toLowerCopy(*Type.Name) : "";
  if (LowerName == "bool")
    return "true";
  if (LowerName == "int" || LowerName == "integer" || LowerName == "port")
    return "0";
  if (LowerName == "float")
    return "0.0";
  if (LowerName == "listof" || LowerName == "list")
    return "[ ]";
  if (LowerName == "functionto")
    return "args: value";
  if (typeAcceptsPath(Type))
    return "./";
  if (typeAcceptsPackage(Type))
    return "pkgs.hello";
  return "\"\"";
}

std::string snippetEscape(std::string_view Text) {
  std::string Out;
  Out.reserve(Text.size());
  for (char C : Text) {
    if (C == '\\' || C == '$' || C == '}')
      Out.push_back('\\');
    Out.push_back(C);
  }
  return Out;
}

std::optional<std::string> submoduleValueSnippet(const OptionType &Type,
                                                 bool Snippets) {
  if (!option_navigation::isSubmoduleLike(Type))
    return std::nullopt;

  std::vector<std::pair<std::string, OptionType>> Required;
  for (const auto &[Name, Summary] : Type.KnownSubOptions) {
    if (!Summary.Required)
      continue;
    if (std::optional<OptionType> Child =
            option_navigation::nestedType(Type, Name))
      Required.emplace_back(Name, std::move(*Child));
  }

  if (Required.empty())
    return std::string("{ }");

  std::string Text = "{\n";
  int Index = 1;
  for (const auto &[Name, Child] : Required) {
    std::string Placeholder = snippetPlaceholderForType(Child);
    Text += "  " + Name + " = ";
    if (Snippets)
      Text += "${" + std::to_string(Index++) + ":" +
              snippetEscape(Placeholder) + "}";
    else
      Text += Placeholder;
    Text += ";\n";
  }
  Text += "}";
  return Text;
}

bool completeSnippetOptionValue(const OptionValueContext &Context,
                                const OptionType &Type,
                                const std::string &Prefix, llvm::StringRef Src,
                                bool ClientSupportSnippet,
                                std::set<std::string> &SeenLabels,
                                std::vector<CompletionItem> &Items) {
  if (!Prefix.empty() || isSelectCompletionContext(Context))
    return false;

  std::optional<std::string> InsertText =
      submoduleValueSnippet(Type, ClientSupportSnippet);
  std::string Label = "attrset";
  CompletionItemKind Kind = CompletionItemKind::Snippet;
  if (!InsertText) {
    const std::optional<ParsedOptionType> Parsed = parseOptionType(Type);
    if (!Parsed)
      return false;
    if (Parsed->Accepted.contains(OptionLiteralKind::String)) {
      Label = "\"\"";
      InsertText = ClientSupportSnippet ? "\"${1:value}\"" : "\"\"";
    } else if (Parsed->Accepted.contains(OptionLiteralKind::Int)) {
      Label = "0";
      InsertText = ClientSupportSnippet ? "${1:0}" : "0";
    } else if (Parsed->Accepted.contains(OptionLiteralKind::Float)) {
      Label = "0.0";
      InsertText = ClientSupportSnippet ? "${1:0.0}" : "0.0";
    } else if (Parsed->Accepted.contains(OptionLiteralKind::List)) {
      Label = "[ ]";
      InsertText = ClientSupportSnippet ? "[ ${1} ]" : "[ ]";
    } else if (Parsed->Accepted.contains(OptionLiteralKind::AttrSet)) {
      Label = "{ }";
      InsertText = ClientSupportSnippet ? "{\n  ${1}\n}" : "{ }";
    } else if (Parsed->Accepted.contains(OptionLiteralKind::Function)) {
      Label = "args: value";
      InsertText =
          ClientSupportSnippet ? "${1:args}: ${2:value}" : "args: value";
    } else {
      return false;
    }
    Kind = CompletionItemKind::Snippet;
  } else {
    Label = *InsertText == "{ }" ? "{ }" : "required fields";
  }

  if (SeenLabels.contains(Label))
    return false;

  CompletionItem Item{
      .label = Label,
      .kind = Kind,
      .detail = "option value snippet",
      .insertText = *InsertText,
      .insertTextFormat = ClientSupportSnippet ? InsertTextFormat::Snippet
                                               : InsertTextFormat::PlainText,
  };
  if (Context.CompletionExpr &&
      Context.CompletionExpr->kind() == Node::NK_ExprString)
    addOptionValueInsertEdits(Context, Src, *InsertText, Item);
  else
    Item.textEdit = lspserver::TextEdit{.range = cursorRange(Context),
                                        .newText = *InsertText};
  SeenLabels.insert(Label);
  addItem(Items, std::move(Item));
  return true;
}

bool completeOptionValueForType(const OptionValueContext &Context,
                                const OptionType &Type,
                                const std::string &Prefix, llvm::StringRef Src,
                                bool ClientSupportSnippet,
                                AttrSetClient *NixpkgsClient,
                                std::set<std::string> &SeenLabels,
                                std::vector<CompletionItem> &Items) {
  bool Handled = false;
  Handled |=
      completeEnumOptionValue(Context, Type, Prefix, Src, SeenLabels, Items);
  Handled |=
      completeBooleanOptionValue(Context, Type, Prefix, Src, SeenLabels, Items);
  Handled |=
      completePathOptionValue(Context, Type, Prefix, Src, SeenLabels, Items);
  Handled |= completePackageOptionValue(Context, Type, Prefix, Src,
                                        NixpkgsClient, SeenLabels, Items);

  const std::string LowerName = Type.Name ? toLowerCopy(*Type.Name) : "";
  if (LowerName == "nullor") {
    Handled |= completeNullOptionValue(Context, Prefix, Src, SeenLabels, Items);
    if (std::optional<OptionType> Elem = elemTypeFor(Type))
      Handled |= completeOptionValueForType(Context, *Elem, Prefix, Src,
                                            ClientSupportSnippet, NixpkgsClient,
                                            SeenLabels, Items);
  } else if (LowerName == "unique") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type))
      Handled |= completeOptionValueForType(Context, *Elem, Prefix, Src,
                                            ClientSupportSnippet, NixpkgsClient,
                                            SeenLabels, Items);
  }

  for (const OptionType &Alternative : alternativeTypesFor(Type, LowerName))
    Handled |= completeOptionValueForType(Context, Alternative, Prefix, Src,
                                          ClientSupportSnippet, NixpkgsClient,
                                          SeenLabels, Items);

  if (!Handled)
    Handled |= completeSnippetOptionValue(Context, Type, Prefix, Src,
                                          ClientSupportSnippet, SeenLabels,
                                          Items);

  return Handled;
}

} // namespace

void completeOptionNames(const std::vector<ResolvedOptionField> &Fields,
                         bool CompletionSnippets,
                         std::vector<CompletionItem> &List) {
  OptionCompletionProvider OCP(CompletionSnippets);
  OCP.completeOptions(Fields, List);
}

std::optional<AttrPathCompleteParams>
optionAttrPathCompletionParams(const Node &N, const ParentMapAnalysis &PM) {
  std::vector<std::string> Scope;
  using PathResult = FindAttrPathResult;
  auto R = findAttrPathForOptions(N, PM, Scope);
  if (R != PathResult::OK || Scope.empty())
    return std::nullopt;

  std::string Prefix = Scope.back();
  Scope.pop_back();
  return AttrPathCompleteParams{.Scope = std::move(Scope),
                                .Prefix = std::move(Prefix)};
}

void completeOptionValue(const OptionValueContext &Context,
                         const std::vector<ResolvedOptionInfo> &Infos,
                         bool CompletionSnippets, AttrSetClient *NixpkgsClient,
                         llvm::StringRef Src,
                         std::vector<CompletionItem> &Items) {
  const std::string Prefix = optionValuePrefix(Context);
  std::set<std::string> SeenLabels;
  const std::vector<option_navigation::ChildStep> ValuePath =
      toNavigationPath(Context);
  for (const ResolvedOptionInfo &Info : Infos) {
    if (!Info.Description.Type)
      continue;

    std::vector<OptionType> Expected =
        option_navigation::descendValuePath(*Info.Description.Type, ValuePath);
    for (const OptionType &Type : Expected) {
      if (completeOptionValueForType(Context, Type, Prefix, Src,
                                     CompletionSnippets, NixpkgsClient,
                                     SeenLabels, Items))
        return;
    }
  }
}

} // namespace nixd::completion
