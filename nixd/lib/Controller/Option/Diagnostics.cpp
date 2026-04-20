#include "DiagnosticsSupport.h"
#include "FlakeSchema.h"
#include "Validation.h"
#include "Navigation.h"
#include "nixd/Controller/Controller.h"
#include "nixd/Controller/FlakeInputInspect.h"
#include "nixd/Controller/Option.h"
#include "lspserver/SourceCode.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#include <unordered_set>

#include <nixf/Basic/Nodes/Attrs.h>
#include <nixf/Basic/Nodes/Lambda.h>

#include <llvm/Support/Error.h>

using namespace nixd;
using namespace nixd::option_diagnostics;
using namespace nixf;

namespace {

constexpr size_t MaxOptionDiagnosticQueries = 128;

struct OptionDiagnosticContext {
  struct FieldCacheKey {
    std::vector<std::string> Scope;
    std::string Prefix;

    bool operator<(const FieldCacheKey &Other) const {
      return std::tie(Scope, Prefix) < std::tie(Other.Scope, Other.Prefix);
    }
  };

  std::map<std::vector<std::string>, std::vector<ResolvedOptionInfo>> InfoCache;
  std::map<FieldCacheKey, std::vector<ResolvedOptionField>> FieldCache;
  size_t Queries = 0;
  bool QueryLimitReached = false;
};

std::vector<ResolvedOptionInfo> resolveCached(
    OptionDiagnosticContext &Context, const std::vector<std::string> &Scope,
    const std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)> &Resolve) {
  auto [It, Inserted] = Context.InfoCache.try_emplace(Scope);
  if (Inserted) {
    if (Context.Queries >= MaxOptionDiagnosticQueries) {
      Context.QueryLimitReached = true;
      return {};
    }
    ++Context.Queries;
    It->second = Resolve(Scope);
  }
  return It->second;
}

std::vector<ResolvedOptionField> completeCached(
    OptionDiagnosticContext &Context, const std::vector<std::string> &Scope,
    const std::string &Prefix,
    const std::function<std::vector<ResolvedOptionField>(
        const std::vector<std::string> &, const std::string &)> &Complete) {
  OptionDiagnosticContext::FieldCacheKey Key{.Scope = Scope, .Prefix = Prefix};
  auto [It, Inserted] = Context.FieldCache.try_emplace(Key);
  if (Inserted) {
    if (Context.Queries >= MaxOptionDiagnosticQueries) {
      Context.QueryLimitReached = true;
      return {};
    }
    ++Context.Queries;
    It->second = Complete(Scope, Prefix);
  }
  return It->second;
}

bool hasUnsafeAncestorAttrs(const Binding &Binding,
                            const ParentMapAnalysis &PM) {
  const Node *Current = &Binding;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return false;
    if (Parent->kind() == Node::NK_ExprAttrs) {
      const auto &Attrs = static_cast<const ExprAttrs &>(*Parent);
      if (!Attrs.sema().dynamicAttrs().empty() || hasInheritBinding(Attrs))
        return true;
    }
    Current = Parent;
  }
  return false;
}

bool isNestedInList(const Binding &Binding, const ParentMapAnalysis &PM) {
  const Node *Current = &Binding;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return false;
    if (Parent->kind() == Node::NK_ExprList)
      return true;
    if (Parent->kind() == Node::NK_Binding)
      return false;
    Current = Parent;
  }
  return false;
}

bool bindingPathStartsWith(const Binding &Bind, std::string_view Name) {
  const auto &Names = Bind.path().names();
  if (Names.empty() || !Names.front() || !Names.front()->isStatic())
    return false;
  return Names.front()->staticName() == Name;
}

bool hasEnclosingBinding(const Binding &Bind, const ParentMapAnalysis &PM) {
  const Node *Current = &Bind;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return false;
    if (Parent->kind() == Node::NK_Binding)
      return true;
    Current = Parent;
  }
  return false;
}

bool isNestedInConfigWrapper(const Binding &Bind,
                             const ParentMapAnalysis &PM) {
  const Node *Current = &Bind;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    if (Current->kind() == Node::NK_Binding) {
      const auto &CurrentBinding = static_cast<const nixf::Binding &>(*Current);
      if (bindingPathStartsWith(CurrentBinding, "config") &&
          hasEnclosingBinding(CurrentBinding, PM))
        return true;
    }

    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return false;
    Current = Parent;
  }
  return false;
}

bool isDirectBindingValue(const Node &Value, const ParentMapAnalysis &PM) {
  const Node *Current = &Value;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return false;

    if (Parent->kind() == Node::NK_Binding) {
      const auto &Bind = static_cast<const nixf::Binding &>(*Parent);
      return Bind.value().get() == Current;
    }

    if (Parent->kind() != Node::NK_ExprParen)
      return false;
    Current = Parent;
  }
  return false;
}

bool isNestedInOptionValueLambda(const Binding &Bind,
                                 const ParentMapAnalysis &PM) {
  const Node *Current = &Bind;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return false;

    if (Parent->kind() == Node::NK_ExprLambda) {
      const auto &Lambda = static_cast<const ExprLambda &>(*Parent);
      if (Lambda.body() == Current && isDirectBindingValue(*Parent, PM))
        return true;
    }

    Current = Parent;
  }
  return false;
}

std::optional<std::string> staticCalleeName(const Expr &Fn) {
  if (Fn.kind() == Node::NK_ExprParen) {
    const auto &Paren = static_cast<const ExprParen &>(Fn);
    if (Paren.expr())
      return staticCalleeName(*Paren.expr());
    return std::nullopt;
  }

  if (Fn.kind() == Node::NK_ExprVar)
    return static_cast<const ExprVar &>(Fn).id().name();

  if (Fn.kind() != Node::NK_ExprSelect)
    return std::nullopt;

  const auto &Select = static_cast<const ExprSelect &>(Fn);
  const AttrPath *Path = Select.path();
  if (!Path || Path->names().empty())
    return std::nullopt;

  const AttrName *Last = Path->names().back().get();
  if (!Last || !Last->isStatic())
    return std::nullopt;
  return Last->staticName();
}

bool isOptionDeclarationHelper(std::string_view Name) {
  return Name == "mkOption" || Name == "mkEnableOption" ||
         Name == "mkPackageOption";
}

bool isNestedInOptionDeclarationCall(const Binding &Bind,
                                     const ParentMapAnalysis &PM) {
  const Node *Current = &Bind;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return false;

    if (Current->kind() == Node::NK_ExprAttrs &&
        Parent->kind() == Node::NK_ExprCall) {
      const auto &Call = static_cast<const ExprCall &>(*Parent);
      const bool IsArgument = std::any_of(
          Call.args().begin(), Call.args().end(),
          [&](const std::shared_ptr<Expr> &Arg) { return Arg.get() == Current; });
      if (IsArgument) {
        if (std::optional<std::string> Name = staticCalleeName(Call.fn());
            Name && isOptionDeclarationHelper(*Name))
          return true;
      }
    }

    Current = Parent;
  }
  return false;
}

bool isOutputsBinding(const Binding &Bind) {
  const auto &Names = Bind.path().names();
  return !Names.empty() && Names.front() && Names.front()->isStatic() &&
         Names.front()->staticName() == "outputs";
}

bool isOutputsLambda(const ExprLambda &Lambda, const ParentMapAnalysis &PM) {
  const Node *Current = &Lambda;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return false;

    if (Parent->kind() == Node::NK_ExprParen) {
      const auto &Paren = static_cast<const ExprParen &>(*Parent);
      if (Paren.expr() && Paren.expr() == Current) {
        Current = Parent;
        continue;
      }
      return false;
    }

    if (Parent->kind() != Node::NK_Binding)
      return false;

    const auto &Bind = static_cast<const Binding &>(*Parent);
    return Bind.value().get() == Current && isOutputsBinding(Bind);
  }
  return false;
}

std::string renderQuotedList(const std::vector<FlakeOutputInput> &Inputs) {
  std::ostringstream OS;
  for (size_t I = 0; I < Inputs.size(); ++I) {
    if (I)
      OS << ", ";
    OS << "`" << Inputs[I].Name << "`";
  }
  return OS.str();
}

std::vector<NixdDiagnostic>
validateFlakeOutputInputs(const ExprLambda &Lambda,
                          const ParentMapAnalysis &PM) {
  if (!isOutputsLambda(Lambda, PM) || !Lambda.arg() ||
      !Lambda.arg()->formals())
    return {};

  std::vector<FlakeOutputInput> Inputs = collectFlakeOutputInputs(Lambda, PM);
  std::set<std::string> InputNames;
  for (const FlakeOutputInput &Input : Inputs)
    InputNames.insert(Input.Name);

  std::vector<NixdDiagnostic> Diagnostics;
  const std::string Available = renderQuotedList(Inputs);
  for (const std::shared_ptr<Formal> &Formal :
       Lambda.arg()->formals()->members()) {
    if (!Formal || Formal->isEllipsis() || !Formal->id() ||
        Formal->defaultExpr())
      continue;

    const std::string &Name = Formal->id()->name();
    if (InputNames.contains(Name))
      continue;

    Diagnostics.emplace_back(NixdDiagnostic{
        .Range = Formal->id()->range(),
        .Severity = NixdDiagnosticSeverity::Error,
        .Code = "flake-output-input-unknown",
        .Source = "nixd",
        .Message = "flake output input `" + Name +
                   "` is not provided by `self` or `inputs`; available inputs: " +
                   Available,
    });
  }
  return Diagnostics;
}

std::optional<std::vector<std::string>>
enclosingBindingScope(const Binding &Bind, const ParentMapAnalysis &PM,
                     const OptionInfoResolver &Resolve) {
  const Node *Current = &Bind;
  std::unordered_set<const Node *> Seen;
  while (Current && Seen.insert(Current).second) {
    const Node *Parent = PM.query(*Current);
    if (!Parent)
      return std::nullopt;
    if (Parent->kind() == Node::NK_Binding)
      if (std::optional<SemanticOptionBinding> Semantic =
              findSemanticOptionBinding(static_cast<const nixf::Binding &>(*Parent),
                                        PM, Resolve))
        return std::move(Semantic->Scope);
    Current = Parent;
  }
  return std::nullopt;
}

bool isProperPrefix(const std::vector<std::string> &Prefix,
                    const std::vector<std::string> &Scope) {
  return Prefix.size() < Scope.size() &&
         std::equal(Prefix.begin(), Prefix.end(), Scope.begin());
}

std::optional<NixdDiagnostic> validateKnownOptionPath(
    const Binding &Binding, const ParentMapAnalysis &PM,
    const std::vector<std::string> &Scope, OptionDiagnosticContext &Context,
    const std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)> &Resolve,
    const std::function<std::vector<ResolvedOptionField>(
        const std::vector<std::string> &, const std::string &)> &Complete) {
  if (Scope.empty() || Scope.back() == "imports" ||
      hasUnsafeAncestorAttrs(Binding, PM))
    return std::nullopt;

  std::vector<std::string> ParentScope(Scope.begin(), Scope.end() - 1);
  const std::string &Leaf = Scope.back();

  if (std::optional<std::vector<std::string>> Enclosing =
          enclosingBindingScope(Binding, PM, Resolve)) {
    if (isProperPrefix(*Enclosing, Scope))
      return std::nullopt;
  }

  for (size_t PrefixLen = 1; PrefixLen < Scope.size(); ++PrefixLen) {
    std::vector<std::string> Prefix(Scope.begin(), Scope.begin() + PrefixLen);
    for (const ResolvedOptionInfo &Info :
         resolveCached(Context, Prefix, Resolve)) {
      if (Info.Description.Type &&
          option_navigation::hasDynamicAttrCoverage(*Info.Description.Type))
        return std::nullopt;
    }
    if (Context.QueryLimitReached)
      return std::nullopt;
  }

  bool HasClosedParentType = false;
  for (const ResolvedOptionInfo &Info :
       resolveCached(Context, ParentScope, Resolve)) {
    if (!Info.Description.Type)
      continue;
    const OptionType &Type = *Info.Description.Type;
    if (!Type.KnownSubOptionsComplete ||
        option_navigation::hasDynamicAttrCoverage(Type))
      return std::nullopt;
    HasClosedParentType = true;
  }
  if (Context.QueryLimitReached)
    return std::nullopt;

  std::vector<ResolvedOptionField> ExactFields =
      completeCached(Context, ParentScope, Leaf, Complete);
  if (Context.QueryLimitReached)
    return std::nullopt;
  for (const ResolvedOptionField &Field : ExactFields)
    if (Field.Field.Name == Leaf)
      return std::nullopt;

  std::vector<ResolvedOptionField> ChildFields =
      completeCached(Context, Scope, "", Complete);
  if (Context.QueryLimitReached)
    return std::nullopt;
  if (!ChildFields.empty())
    return std::nullopt;

  std::vector<ResolvedOptionField> SiblingFields =
      completeCached(Context, ParentScope, "", Complete);
  if (Context.QueryLimitReached)
    return std::nullopt;
  if (SiblingFields.empty() && !HasClosedParentType)
    return std::nullopt;
  for (const ResolvedOptionField &Field : SiblingFields)
    if (Field.Field.Name == Leaf)
      return std::nullopt;

  return makeUnknownDiagnostic(Binding.path(), Scope);
}

std::vector<NixdDiagnostic>
validateOptionBinding(const Binding &Binding, const ParentMapAnalysis &PM,
                      const VariableLookupAnalysis *VLA,
                      bool IsFlakeSchema,
                      OptionDiagnosticContext &Context,
                      const std::function<std::vector<ResolvedOptionInfo>(
                          const std::vector<std::string> &)> &Resolve,
                      const std::function<std::vector<ResolvedOptionField>(
                          const std::vector<std::string> &,
                          const std::string &)> &Complete) {
  const auto &Value = Binding.value();
  if (!Value)
    return {};
  if (isLetDefinitionBinding(Binding, PM))
    return {};
  if (isNestedInList(Binding, PM))
    return {};
  if (isNestedInConfigWrapper(Binding, PM))
    return {};
  if (isNestedInOptionValueLambda(Binding, PM))
    return {};
  if (isNestedInOptionDeclarationCall(Binding, PM))
    return {};

  auto SemanticResolve = [&](const std::vector<std::string> &Scope) {
    if (!IsFlakeSchema)
      return Resolve(Scope);
    std::vector<ResolvedOptionInfo> Infos = Resolve(Scope);
    if (!Infos.empty())
      return Infos;
    return Resolve(flake_schema::outputsBodyScope(Scope));
  };

  std::optional<SemanticOptionBinding> Semantic =
      findSemanticOptionBinding(Binding, PM, SemanticResolve);
  if (!Semantic && IsFlakeSchema && !hasEnclosingBinding(Binding, PM))
    if (std::optional<std::vector<std::string>> Scope =
            findOptionBindingScope(Binding, PM))
      Semantic =
          SemanticOptionBinding{.Binding = &Binding, .Scope = std::move(*Scope)};
  if (!Semantic)
    return {};
  std::vector<std::string> Scope = Semantic->Scope;
  if (IsFlakeSchema && flake_schema::isInsideOutputsBody(Binding, PM))
    Scope = flake_schema::outputsBodyScope(Scope);
  if (!Scope.empty() && Scope.front() == "options")
    return {};

  std::vector<ResolvedOptionInfo> Infos =
      resolveCached(Context, Scope, Resolve);

  if (Infos.empty()) {
    if (std::optional<NixdDiagnostic> Unknown = validateKnownOptionPath(
            Binding, PM, Scope, Context, Resolve, Complete))
      return {*Unknown};
    return {};
  }

  std::optional<std::vector<NixdDiagnostic>> FirstMismatch;
  for (const ResolvedOptionInfo &Info : Infos) {
    if (!Info.Description.Type)
      continue;

    ValidationResult Result =
        validateType(*Info.Description.Type, *Value, Scope, PM, VLA);
    if (Result.Match == SchemaMatch::Matches)
      return {};
    if (Result.Match == SchemaMatch::Unknown)
      continue;

    if (!FirstMismatch)
      FirstMismatch = std::move(Result.Diagnostics);
  }
  return FirstMismatch ? std::move(*FirstMismatch)
                       : std::vector<NixdDiagnostic>{};
}

void collectOptionDiagnosticsFromNode(
    const Node &Desc, const ParentMapAnalysis &PM,
    const VariableLookupAnalysis *VLA, OptionDiagnosticContext &Context,
    bool IsFlakeSchema,
    const std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)> &Resolve,
    const std::function<std::vector<ResolvedOptionField>(
        const std::vector<std::string> &, const std::string &)> &Complete,
    std::unordered_set<const Node *> &Seen,
    std::vector<NixdDiagnostic> &Diagnostics) {
  if (!Seen.insert(&Desc).second)
    return;

  if (Desc.kind() == Node::NK_Binding) {
    std::vector<NixdDiagnostic> NewDiagnostics = validateOptionBinding(
        static_cast<const Binding &>(Desc), PM, VLA, IsFlakeSchema, Context,
        Resolve, Complete);
    std::move(NewDiagnostics.begin(), NewDiagnostics.end(),
              std::back_inserter(Diagnostics));
  }

  if (IsFlakeSchema && Desc.kind() == Node::NK_ExprLambda) {
    std::vector<NixdDiagnostic> NewDiagnostics =
        validateFlakeOutputInputs(static_cast<const ExprLambda &>(Desc), PM);
    std::move(NewDiagnostics.begin(), NewDiagnostics.end(),
              std::back_inserter(Diagnostics));
  }

  for (const nixf::Node *Child : Desc.children()) {
    if (Child)
      collectOptionDiagnosticsFromNode(*Child, PM, VLA, Context,
                                       IsFlakeSchema, Resolve, Complete, Seen,
                                       Diagnostics);
  }
}

std::vector<NixdDiagnostic> dedupeDiagnostics(std::vector<NixdDiagnostic> In) {
  std::vector<NixdDiagnostic> Out;
  std::set<std::tuple<size_t, size_t, std::string, std::string>> Seen;
  for (NixdDiagnostic &Diagnostic : In) {
    auto Key = std::make_tuple(Diagnostic.Range.lCur().offset(),
                               Diagnostic.Range.rCur().offset(),
                               Diagnostic.Code, Diagnostic.Message);
    if (Seen.insert(std::move(Key)).second)
      Out.emplace_back(std::move(Diagnostic));
  }
  return Out;
}

bool hasRecoverySyntaxError(const NixTU &TU) {
  for (const nixf::Diagnostic &Diagnostic : TU.diagnostics()) {
    const nixf::Diagnostic::Severity Severity =
        nixf::Diagnostic::severity(Diagnostic.kind());
    if (Severity == nixf::Diagnostic::DS_Fatal)
      return true;

    const std::string_view Name = nixf::Diagnostic::sname(Diagnostic.kind());
    if (Severity <= nixf::Diagnostic::DS_Error &&
        (Name.starts_with("parse-") || Name.starts_with("lex-")))
      return true;
  }
  return false;
}

std::optional<std::vector<std::string>>
extractOptionScopeFromProviderError(std::string_view Error) {
  constexpr std::string_view Marker = "option `";
  size_t MarkerPos = Error.find(Marker);
  if (MarkerPos == std::string_view::npos)
    return std::nullopt;

  size_t Begin = MarkerPos + Marker.size();
  size_t End = Error.find_first_of("`'", Begin);
  if (End == std::string_view::npos || End <= Begin)
    return std::nullopt;

  std::string_view ScopeText = Error.substr(Begin, End - Begin);
  if (ScopeText.empty())
    return std::nullopt;

  std::vector<std::string> Scope;
  size_t Pos = 0;
  while (Pos <= ScopeText.size()) {
    size_t Dot = ScopeText.find('.', Pos);
    std::string_view Segment =
        ScopeText.substr(Pos, Dot == std::string_view::npos
                                  ? std::string_view::npos
                                  : Dot - Pos);
    if (Segment.empty())
      return std::nullopt;
    Scope.emplace_back(Segment);
    if (Dot == std::string_view::npos)
      break;
    Pos = Dot + 1;
  }
  return Scope.empty() ? std::nullopt : std::optional{std::move(Scope)};
}

std::optional<nixf::LexerCursorRange>
findBindingRangeForScopeImpl(const nixf::Node &Node,
                             const std::vector<std::string> &Scope,
                             std::vector<std::string> Prefix) {
  if (Node.kind() == nixf::Node::NK_ExprAttrs) {
    const auto &Attrs = static_cast<const nixf::ExprAttrs &>(Node);
    if (!Attrs.binds())
      return std::nullopt;

    for (const std::shared_ptr<nixf::Node> &BindNode : Attrs.binds()->bindings()) {
      if (!BindNode || BindNode->kind() != nixf::Node::NK_Binding)
        continue;

      const auto &Binding = static_cast<const nixf::Binding &>(*BindNode);
      std::vector<std::string> BindingScope = Prefix;
      bool Static = true;
      for (const auto &Name : Binding.path().names()) {
        if (!Name || !Name->isStatic()) {
          Static = false;
          break;
        }
        BindingScope.emplace_back(Name->staticName());
      }
      if (!Static)
        continue;

      if (BindingScope == Scope) {
        const auto &Names = Binding.path().names();
        if (!Names.empty() && Names.back())
          return Names.back()->range();
        return Binding.range();
      }

      if (Binding.value()) {
        if (std::optional<nixf::LexerCursorRange> Range =
                findBindingRangeForScopeImpl(*Binding.value(), Scope,
                                             std::move(BindingScope)))
          return Range;
      }
    }
    return std::nullopt;
  }

  for (const nixf::Node *Child : Node.children()) {
    if (!Child)
      continue;
    if (std::optional<nixf::LexerCursorRange> Range =
            findBindingRangeForScopeImpl(*Child, Scope, Prefix))
      return Range;
  }
  return std::nullopt;
}

std::optional<nixf::LexerCursorRange>
findBindingRangeForScope(const nixf::Node &Node,
                         const std::vector<std::string> &Scope) {
  return findBindingRangeForScopeImpl(Node, Scope, {});
}

bool locationMatchesFile(const lspserver::Location &Location,
                         std::string_view File) {
  llvm::StringRef FileRef(File.data(), File.size());
  lspserver::URIForFile Current =
      lspserver::URIForFile::canonicalize(FileRef, FileRef);
  return Location.uri.file() == Current.file();
}

std::optional<nixf::LexerCursor>
cursorFromPosition(std::string_view Src, const lspserver::Position &Position) {
  llvm::StringRef SrcRef(Src.data(), Src.size());
  llvm::Expected<size_t> Offset =
      lspserver::positionToOffset(SrcRef, Position,
                                  /*AllowColumnsBeyondLineLength=*/false);
  if (!Offset) {
    llvm::consumeError(Offset.takeError());
    return std::nullopt;
  }
  return nixf::LexerCursor::unsafeCreate(Position.line, Position.character,
                                         *Offset);
}

std::optional<nixf::LexerCursorRange>
rangeFromLocation(const lspserver::Location &Location, std::string_view Src) {
  std::optional<nixf::LexerCursor> Start =
      cursorFromPosition(Src, Location.range.start);
  if (!Start)
    return std::nullopt;

  std::optional<nixf::LexerCursor> End =
      cursorFromPosition(Src, Location.range.end);
  if (!End)
    return std::nullopt;

  if (End->offset() <= Start->offset() && Start->offset() < Src.size() &&
      Src[Start->offset()] != '\n') {
    llvm::StringRef SrcRef(Src.data(), Src.size());
    const lspserver::Position EndPos =
        lspserver::offsetToPosition(SrcRef, Start->offset() + 1);
    End = nixf::LexerCursor::unsafeCreate(EndPos.line, EndPos.character,
                                          Start->offset() + 1);
  }

  return nixf::LexerCursorRange(*Start, *End);
}

std::optional<nixf::LexerCursorRange>
expandPointRangeBackward(nixf::LexerCursorRange Range, std::string_view Src) {
  if (Range.lCur().offset() != Range.rCur().offset() ||
      Range.lCur().offset() == 0)
    return Range;

  const size_t StartOffset = Range.lCur().offset() - 1;
  if (Src[StartOffset] == '\n')
    return Range;

  llvm::StringRef SrcRef(Src.data(), Src.size());
  const lspserver::Position StartPos =
      lspserver::offsetToPosition(SrcRef, StartOffset);
  nixf::LexerCursor Start = nixf::LexerCursor::unsafeCreate(
      StartPos.line, StartPos.character, StartOffset);
  return nixf::LexerCursorRange(Start, Range.rCur());
}

std::optional<nixf::LexerCursorRange>
localSyntaxErrorRange(const NixTU &TU, std::string_view Src) {
  for (const nixf::Diagnostic &Diagnostic : TU.diagnostics()) {
    const nixf::Diagnostic::Severity Severity =
        nixf::Diagnostic::severity(Diagnostic.kind());
    const std::string_view Name = nixf::Diagnostic::sname(Diagnostic.kind());
    if (Severity <= nixf::Diagnostic::DS_Error &&
        (Name.starts_with("parse-") || Name.starts_with("lex-")))
      return expandPointRangeBackward(Diagnostic.range(), Src);
  }
  return std::nullopt;
}

bool isProviderSyntaxError(std::string_view Message) {
  return Message.find("syntax error") != std::string_view::npos;
}

std::vector<NixdDiagnostic> providerFailureDiagnostics(
    const NixTU &TU, std::string_view File, std::string_view Src,
    const std::vector<OptionProviderFailure> &Failures) {
  std::vector<NixdDiagnostic> Diagnostics;
  Diagnostics.reserve(Failures.size());

  const nixf::LexerCursor Start = nixf::LexerCursor::unsafeCreate(0, 0, 0);
  for (const OptionProviderFailure &Failure : Failures) {
    std::optional<nixf::LexerCursorRange> Range;
    if (Failure.Location && locationMatchesFile(*Failure.Location, File)) {
      if (isProviderSyntaxError(Failure.Message))
        Range = localSyntaxErrorRange(TU, Src);
      if (!Range)
        Range = rangeFromLocation(*Failure.Location, Src);
    }

    if (TU.ast() && TU.parentMap()) {
      if (std::optional<std::vector<std::string>> Scope =
              extractOptionScopeFromProviderError(Failure.Message)) {
        if (!Range)
          Range = findBindingRangeForScope(*TU.ast(), *Scope);
      }
    }
    Diagnostics.push_back(NixdDiagnostic{
        .Range = Range.value_or(nixf::LexerCursorRange(Start)),
        .Severity = NixdDiagnosticSeverity::Error,
        .Code = "option-provider-eval",
        .Source = "nixd",
        .Message = "option provider `" + Failure.ProviderName +
                   "` failed to evaluate: " + Failure.Message,
    });
  }
  return Diagnostics;
}

} // namespace

std::vector<NixdDiagnostic>
Controller::collectOptionDiagnostics(const NixTU &TU, std::string_view File) {
  std::vector<NixdDiagnostic> Diagnostics;
  const bool IsFlakeSchema = flake_schema::isFlakeFile(File);
  if (!IsFlakeSchema) {
    if (!waitForOptionProvidersReadyForTests())
      return Diagnostics;
    if (!optionProvidersSettledForDiagnostics())
      return Diagnostics;

    std::vector<OptionProviderFailure> Failures =
        optionProviderFailureSnapshot();
    std::erase_if(Failures, [&](const OptionProviderFailure &Failure) {
      if (!Failure.Location || locationMatchesFile(*Failure.Location, File))
        return false;
      std::lock_guard _(TUsLock);
      return TUs.contains(Failure.Location->uri.file());
    });
    std::vector<NixdDiagnostic> ProviderDiagnostics =
        providerFailureDiagnostics(TU, File, TU.src(), Failures);
    std::move(ProviderDiagnostics.begin(), ProviderDiagnostics.end(),
              std::back_inserter(Diagnostics));
    if (!Failures.empty())
      return Diagnostics;
  }

  if (!TU.ast() || !TU.parentMap())
    return Diagnostics;
  if (hasRecoverySyntaxError(TU))
    return Diagnostics;

  OptionDiagnosticContext Context;
  auto Resolve = [this, IsFlakeSchema](
                     const std::vector<std::string> &Scope) {
    if (IsFlakeSchema)
      return flake_schema::resolveDerived(Scope);
    return resolveDerivedOptionInfos(Scope);
  };
  auto Complete = [this, IsFlakeSchema](const std::vector<std::string> &Scope,
                                        const std::string &Prefix) {
    if (IsFlakeSchema)
      return flake_schema::completeDerived(Scope, Prefix);
    return completeDerivedOptions(Scope, Prefix);
  };
  std::unordered_set<const Node *> Seen;
  collectOptionDiagnosticsFromNode(*TU.ast(), *TU.parentMap(),
                                   TU.variableLookup(), Context,
                                   IsFlakeSchema, Resolve, Complete, Seen,
                                   Diagnostics);
  return dedupeDiagnostics(std::move(Diagnostics));
}
