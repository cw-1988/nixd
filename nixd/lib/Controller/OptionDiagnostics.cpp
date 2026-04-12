#include "nixd/Controller/Controller.h"
#include "nixd/Controller/Option.h"

#include <boost/asio/post.hpp>

#include <functional>
#include <map>
#include <optional>
#include <sstream>

using namespace nixd;
using namespace nixf;

namespace {

constexpr size_t MaxOptionDiagnosticQueries = 128;

struct OptionDiagnosticContext {
  std::map<std::vector<std::string>, std::vector<ResolvedOptionInfo>> InfoCache;
  size_t Queries = 0;
};

std::string renderScope(const std::vector<std::string> &Scope) {
  std::ostringstream OS;
  for (size_t I = 0; I < Scope.size(); ++I) {
    if (I)
      OS << ".";
    OS << Scope[I];
  }
  return OS.str();
}

std::optional<NixdDiagnostic>
validateOptionBinding(const Binding &Binding, const ParentMapAnalysis &PM,
                      OptionDiagnosticContext &Context,
                      const std::function<std::vector<ResolvedOptionInfo>(
                          const std::vector<std::string> &)> &Resolve) {
  const auto &Value = Binding.value();
  if (!Value)
    return std::nullopt;

  const OptionLiteralKind Actual = classifyOptionLiteral(*Value);
  if (Actual == OptionLiteralKind::Unknown)
    return std::nullopt;

  std::optional<std::vector<std::string>> Scope =
      findOptionBindingScope(Binding, PM);
  if (!Scope)
    return std::nullopt;

  auto [It, Inserted] = Context.InfoCache.try_emplace(*Scope);
  if (Inserted) {
    if (Context.Queries >= MaxOptionDiagnosticQueries)
      return std::nullopt;
    ++Context.Queries;
    It->second = Resolve(*Scope);
  }

  std::optional<NixdDiagnostic> FirstMismatch;
  for (const ResolvedOptionInfo &Info : It->second) {
    if (!Info.Description.Type)
      continue;

    const std::optional<ParsedOptionType> Expected =
        parseOptionType(*Info.Description.Type);
    if (!Expected)
      continue;
    const OptionValueMatch Match = optionValueMatch(*Expected, *Value, Actual);
    if (Match == OptionValueMatch::Matches)
      return std::nullopt;
    if (Match == OptionValueMatch::Unknown)
      continue;

    if (!FirstMismatch) {
      FirstMismatch = NixdDiagnostic{
          .Range = Value->range(),
          .Severity = NixdDiagnosticSeverity::Error,
          .Code = "option-value-type",
          .Source = "nixd",
          .Message = "value for option `" + renderScope(*Scope) +
                     "` has type `" + optionLiteralKindName(Actual) +
                     "`, expected `" + Expected->Rendered + "`",
      };
    }
  }
  return FirstMismatch;
}

void collectOptionDiagnosticsFromNode(
    const Node &Desc, const ParentMapAnalysis &PM,
    OptionDiagnosticContext &Context,
    const std::function<std::vector<ResolvedOptionInfo>(
        const std::vector<std::string> &)> &Resolve,
    std::vector<NixdDiagnostic> &Diagnostics) {
  if (Desc.kind() == Node::NK_Binding) {
    if (std::optional<NixdDiagnostic> Diag = validateOptionBinding(
            static_cast<const Binding &>(Desc), PM, Context, Resolve))
      Diagnostics.emplace_back(std::move(*Diag));
  }

  for (const nixf::Node *Child : Desc.children()) {
    if (Child)
      collectOptionDiagnosticsFromNode(*Child, PM, Context, Resolve,
                                       Diagnostics);
  }
}

} // namespace

std::vector<NixdDiagnostic>
Controller::collectOptionDiagnostics(const NixTU &TU) {
  std::vector<NixdDiagnostic> Diagnostics;
  if (!TU.ast() || !TU.parentMap())
    return Diagnostics;

  OptionDiagnosticContext Context;
  auto Resolve = [this](const std::vector<std::string> &Scope) {
    return resolveOptionInfos(Scope);
  };
  collectOptionDiagnosticsFromNode(*TU.ast(), *TU.parentMap(), Context, Resolve,
                                   Diagnostics);
  return Diagnostics;
}

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
