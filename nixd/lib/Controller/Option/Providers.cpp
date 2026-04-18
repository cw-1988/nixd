#include "nixd/Controller/Controller.h"
#include "FlakeSchema.h"
#include "Navigation.h"

#include <boost/asio/post.hpp>

#include <llvm/Support/Error.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <memory>
#include <optional>
#include <semaphore>
#include <set>
#include <string_view>
#include <tuple>

using namespace nixd;

namespace {

struct ProviderInfoReplyState {
  std::binary_semaphore Ready{0};
  std::optional<OptionDescription> Desc;
};

struct ProviderCompleteReplyState {
  std::binary_semaphore Ready{0};
  OptionCompleteResponse Names;
};

std::optional<OptionType> elemTypeFor(const OptionType &Type,
                                      std::string_view LowerName) {
  return option_navigation::elemTypeFor(Type, LowerName);
}

std::optional<OptionType> nullOrTypeFor(const OptionType &Type) {
  return option_navigation::nullOrTypeFor(Type);
}

std::vector<OptionType> alternativeTypesFor(const OptionType &Type,
                                            std::string_view LowerName) {
  return option_navigation::alternativeTypesFor(Type, LowerName);
}

bool hasSubOptionMetadata(const OptionType &Type) {
  return option_navigation::hasSubOptionMetadata(Type);
}

std::optional<OptionType>
deriveTypeFromResolvedInfo(const ResolvedOptionInfo &Info,
                           const std::vector<std::string> &Suffix) {
  if (!Info.Description.Type)
    return std::nullopt;
  return option_navigation::deriveTypeFromResolvedInfo(Info, Suffix);
}

void appendDefinitionLocations(std::vector<lspserver::Location> &Locations,
                               const OptionDescription &Desc) {
  Locations.insert(Locations.end(), Desc.Declarations.begin(),
                   Desc.Declarations.end());
  Locations.insert(Locations.end(), Desc.Definitions.begin(),
                   Desc.Definitions.end());
}

std::vector<ResolvedOptionField> fieldsFromType(std::string_view ProviderName,
                                                const OptionType &Type,
                                                const std::string &Prefix);

void appendFieldsFromType(std::vector<ResolvedOptionField> &Fields,
                          std::string_view ProviderName, const OptionType &Type,
                          const std::string &Prefix) {
  const std::string LowerName = option_navigation::lowerTypeName(Type);

  if (LowerName == "nullor") {
    if (std::optional<OptionType> Elem = nullOrTypeFor(Type)) {
      std::vector<ResolvedOptionField> Nested =
          fieldsFromType(ProviderName, *Elem, Prefix);
      Fields.insert(Fields.end(), std::make_move_iterator(Nested.begin()),
                    std::make_move_iterator(Nested.end()));
    }
    return;
  }

  if (LowerName == "unique") {
    if (std::optional<OptionType> Elem = elemTypeFor(Type, LowerName)) {
      std::vector<ResolvedOptionField> Nested =
          fieldsFromType(ProviderName, *Elem, Prefix);
      Fields.insert(Fields.end(), std::make_move_iterator(Nested.begin()),
                    std::make_move_iterator(Nested.end()));
    }
    return;
  }

  std::vector<OptionType> Alternatives = alternativeTypesFor(Type, LowerName);
  if (!Alternatives.empty()) {
    for (const OptionType &Alternative : Alternatives) {
      std::vector<ResolvedOptionField> Nested =
          fieldsFromType(ProviderName, Alternative, Prefix);
      Fields.insert(Fields.end(), std::make_move_iterator(Nested.begin()),
                    std::make_move_iterator(Nested.end()));
    }
    return;
  }

  if (LowerName != "submodule" && LowerName != "submodulewith" &&
      !hasSubOptionMetadata(Type))
    return;

  std::set<std::string> Added;
  for (const auto &[Name, Child] : Type.NestedTypes) {
    if (Name == "freeformType" || !Name.starts_with(Prefix))
      continue;
    OptionField Field;
    Field.Name = Name;
    if (Type.KnownSubOptions.contains(Name)) {
      OptionDescription Desc;
      Desc.Type = Child;
      Field.Description = std::move(Desc);
    }
    Fields.push_back(ResolvedOptionField{
        .ProviderName = std::string(ProviderName), .Field = std::move(Field)});
    Added.insert(Name);
  }

  for (const auto &[Name, Summary] : Type.KnownSubOptions) {
    (void)Summary;
    if (Added.contains(Name) || !Name.starts_with(Prefix))
      continue;
    OptionField Field;
    Field.Name = Name;
    Field.Description = OptionDescription{};
    Fields.push_back(ResolvedOptionField{
        .ProviderName = std::string(ProviderName), .Field = std::move(Field)});
  }
}

std::vector<ResolvedOptionField> fieldsFromType(std::string_view ProviderName,
                                                const OptionType &Type,
                                                const std::string &Prefix) {
  std::vector<ResolvedOptionField> Fields;
  appendFieldsFromType(Fields, ProviderName, Type, Prefix);
  return Fields;
}

std::optional<OptionDescription>
synthesizeNamespaceDescription(const std::vector<ResolvedOptionField> &Fields) {
  if (Fields.empty())
    return std::nullopt;

  OptionType Type;
  Type.Name = "namespace";
  for (const ResolvedOptionField &Entry : Fields) {
    Type.KnownSubOptions.try_emplace(Entry.Field.Name);
    if (!Entry.Field.Description)
      continue;

    OptionType Child;
    bool HasChild = false;
    if (Entry.Field.Description->Type) {
      Child = *Entry.Field.Description->Type;
      HasChild = true;
    }
    if (!Child.Description && Entry.Field.Description->Description) {
      Child.Description = *Entry.Field.Description->Description;
      HasChild = true;
    }
    if (HasChild)
      Type.NestedTypes[Entry.Field.Name] = std::move(Child);
  }

  OptionDescription Desc;
  Desc.Type = std::move(Type);
  return Desc;
}

std::vector<ResolvedOptionInfo>
synthesizeNamespaceInfos(const std::vector<ResolvedOptionField> &Fields) {
  std::map<std::string, std::vector<ResolvedOptionField>> FieldsByProvider;
  for (const ResolvedOptionField &Field : Fields)
    FieldsByProvider[Field.ProviderName].push_back(Field);

  std::vector<ResolvedOptionInfo> Infos;
  Infos.reserve(FieldsByProvider.size());
  for (auto &[ProviderName, ProviderFields] : FieldsByProvider) {
    if (std::optional<OptionDescription> Desc =
            synthesizeNamespaceDescription(ProviderFields)) {
      Infos.push_back(ResolvedOptionInfo{.ProviderName = std::move(ProviderName),
                                         .Description = std::move(*Desc)});
    }
  }
  return Infos;
}

} // namespace

bool OptionService::InfoCacheKey::operator<(
    const OptionService::InfoCacheKey &Other) const {
  return std::tie(ProviderName, Generation, Scope) <
         std::tie(Other.ProviderName, Other.Generation, Other.Scope);
}

void OptionService::invalidate() {
  std::lock_guard _(CacheLock);
  InfoCache.clear();
}

void OptionService::invalidateProvider(std::string_view ProviderName) {
  std::lock_guard _(CacheLock);
  std::erase_if(InfoCache, [&](const auto &Entry) {
    return Entry.first.ProviderName == ProviderName;
  });
}

std::optional<OptionDescription>
OptionService::resolveProviderInfo(const OptionProviderRef &Provider,
                                   const std::vector<std::string> &Scope) {
  if (!Provider.Client)
    return std::nullopt;

  InfoCacheKey Key{.ProviderName = Provider.Name,
                   .Generation = Provider.Generation,
                   .Scope = Scope};
  {
    std::lock_guard _(CacheLock);
    if (auto It = InfoCache.find(Key); It != InfoCache.end())
      return It->second;
  }

  auto State = std::make_shared<ProviderInfoReplyState>();
  auto OnReply = [State, Name = Provider.Name](
                     llvm::Expected<OptionInfoResponse> Resp) {
    if (Resp) {
      State->Desc = *Resp;
    } else {
      lspserver::elog("option provider {0}: {1}", Name, Resp.takeError());
    }
    State->Ready.release();
  };

  Provider.Client->optionInfo(Scope, std::move(OnReply));
  State->Ready.acquire();

  {
    std::lock_guard _(CacheLock);
    InfoCache.insert_or_assign(std::move(Key), State->Desc);
  }
  return State->Desc;
}

std::vector<ResolvedOptionField>
OptionService::complete(const std::vector<OptionProviderRef> &Providers,
                        const std::vector<std::string> &Scope,
                        const std::string &Prefix) {
  std::vector<ResolvedOptionField> Fields;
  for (const OptionProviderRef &Provider : Providers) {
    if (!Provider.Client)
      continue;

    auto State = std::make_shared<ProviderCompleteReplyState>();
    auto OnReply = [State, Name = Provider.Name](
                       llvm::Expected<OptionCompleteResponse> Resp) {
      if (!Resp) {
        lspserver::elog("option worker {0}: {1}", Name, Resp.takeError());
        State->Ready.release();
        return;
      }
      State->Names = *Resp;
      State->Ready.release();
    };

    Provider.Client->optionComplete({Scope, Prefix}, std::move(OnReply));
    State->Ready.acquire();

    for (OptionField &Field : State->Names) {
      Fields.push_back(ResolvedOptionField{
          .ProviderName = Provider.Name,
          .Field = std::move(Field),
      });
    }
  }
  return Fields;
}

std::vector<ResolvedOptionField>
OptionService::completeDerived(const std::vector<OptionProviderRef> &Providers,
                               const std::vector<std::string> &Scope,
                               const std::string &Prefix) {
  std::vector<ResolvedOptionField> Fields = complete(Providers, Scope, Prefix);
  if (!Fields.empty())
    return Fields;

  for (const ResolvedOptionInfo &Info : resolve(Providers, Scope)) {
    if (!Info.Description.Type)
      continue;
    std::vector<ResolvedOptionField> Nested =
        fieldsFromType(Info.ProviderName, *Info.Description.Type, Prefix);
    Fields.insert(Fields.end(), std::make_move_iterator(Nested.begin()),
                  std::make_move_iterator(Nested.end()));
  }
  if (!Fields.empty())
    return Fields;

  for (size_t PrefixLen = Scope.size(); PrefixLen > 0; --PrefixLen) {
    std::vector<std::string> ParentScope(Scope.begin(),
                                         Scope.begin() + PrefixLen);
    std::vector<std::string> Suffix(Scope.begin() + PrefixLen, Scope.end());
    if (Suffix.empty())
      continue;

    for (const ResolvedOptionInfo &Info : resolve(Providers, ParentScope)) {
      std::optional<OptionType> Derived =
          deriveTypeFromResolvedInfo(Info, Suffix);
      if (!Derived)
        continue;
      std::vector<ResolvedOptionField> Nested =
          fieldsFromType(Info.ProviderName, *Derived, Prefix);
      Fields.insert(Fields.end(), std::make_move_iterator(Nested.begin()),
                    std::make_move_iterator(Nested.end()));
    }
    if (!Fields.empty())
      return Fields;
  }

  return Fields;
}

std::vector<ResolvedOptionInfo>
OptionService::resolve(const std::vector<OptionProviderRef> &Providers,
                       const std::vector<std::string> &Scope) {
  std::vector<ResolvedOptionInfo> Infos;
  for (const OptionProviderRef &Provider : Providers) {
    if (std::optional<OptionDescription> Desc =
            resolveProviderInfo(Provider, Scope))
      Infos.push_back(ResolvedOptionInfo{.ProviderName = Provider.Name,
                                         .Description = std::move(*Desc)});
  }
  return Infos;
}

std::vector<ResolvedOptionInfo>
OptionService::resolveDerived(const std::vector<OptionProviderRef> &Providers,
                              const std::vector<std::string> &Scope) {
  std::vector<ResolvedOptionInfo> Infos = resolve(Providers, Scope);
  if (!Infos.empty()) {
    const bool HasTypedInfo = std::any_of(Infos.begin(), Infos.end(),
                                          [](const ResolvedOptionInfo &Info) {
                                            return Info.Description.Type.has_value();
                                          });
    if (!HasTypedInfo) {
      std::vector<ResolvedOptionInfo> NamespaceInfos =
          synthesizeNamespaceInfos(complete(Providers, Scope, ""));
      for (ResolvedOptionInfo &Info : Infos) {
        if (Info.Description.Type)
          continue;
        auto It = std::find_if(
            NamespaceInfos.begin(), NamespaceInfos.end(),
            [&](const ResolvedOptionInfo &NamespaceInfo) {
              return NamespaceInfo.ProviderName == Info.ProviderName;
            });
        if (It != NamespaceInfos.end())
          Info.Description.Type = It->Description.Type;
      }
      for (ResolvedOptionInfo &NamespaceInfo : NamespaceInfos) {
        const bool Present = std::any_of(
            Infos.begin(), Infos.end(), [&](const ResolvedOptionInfo &Info) {
              return Info.ProviderName == NamespaceInfo.ProviderName;
            });
        if (!Present)
          Infos.push_back(std::move(NamespaceInfo));
      }
    }
    return Infos;
  }

  Infos = synthesizeNamespaceInfos(complete(Providers, Scope, ""));
  if (!Infos.empty())
    return Infos;

  for (size_t PrefixLen = Scope.size(); PrefixLen > 0; --PrefixLen) {
    std::vector<std::string> ParentScope(Scope.begin(),
                                         Scope.begin() + PrefixLen);
    std::vector<std::string> Suffix(Scope.begin() + PrefixLen, Scope.end());
    if (Suffix.empty())
      continue;

    for (const ResolvedOptionInfo &Info : resolve(Providers, ParentScope)) {
      std::optional<OptionType> Derived =
          deriveTypeFromResolvedInfo(Info, Suffix);
      if (!Derived)
        continue;

      OptionDescription Desc;
      Desc.Type = std::move(*Derived);
      Infos.push_back(ResolvedOptionInfo{.ProviderName = Info.ProviderName,
                                         .Description = std::move(Desc)});
    }

    if (!Infos.empty())
      return Infos;
  }

  return Infos;
}

std::vector<lspserver::Location> OptionService::declarationLocations(
    const std::vector<OptionProviderRef> &Providers,
    const std::vector<std::string> &Scope) {
  std::vector<lspserver::Location> Locations;
  for (const ResolvedOptionInfo &Info : resolve(Providers, Scope)) {
    const auto &Declarations = Info.Description.Declarations;
    Locations.insert(Locations.end(), Declarations.begin(), Declarations.end());
  }
  return Locations;
}

bool Controller::noteOptionProviderChanged(std::string_view Name,
                                           std::uint64_t EvalGeneration) {
  {
    std::lock_guard _(OptionsLock);
    std::string ProviderName(Name);
    auto EvalIt = OptionEvalGenerations.find(ProviderName);
    if (EvalIt == OptionEvalGenerations.end() ||
        EvalIt->second != EvalGeneration)
      return false;
    OptionProviderErrors.erase(ProviderName);
    ReadyOptions.insert(ProviderName);
    SettledOptions.insert(ProviderName);
    OptionGenerations[std::move(ProviderName)] = NextOptionGeneration++;
  }
  OptService.invalidateProvider(Name);
  OptionsReadyCV.notify_all();
  return true;
}

bool Controller::noteOptionProviderSettled(std::string_view Name,
                                           std::uint64_t EvalGeneration,
                                           std::optional<std::string> Error) {
  {
    std::lock_guard _(OptionsLock);
    std::string ProviderName(Name);
    auto EvalIt = OptionEvalGenerations.find(ProviderName);
    if (EvalIt == OptionEvalGenerations.end() ||
        EvalIt->second != EvalGeneration)
      return false;
    SettledOptions.insert(ProviderName);
    if (Error) {
      OptionProviderErrors[ProviderName] = std::move(*Error);
      ReadyOptions.erase(ProviderName);
    }
  }
  OptionsReadyCV.notify_all();
  return Error.has_value();
}

std::vector<OptionProviderRef> Controller::optionProviderSnapshot() {
  std::vector<OptionProviderRef> Providers;
  std::lock_guard _(OptionsLock);
  Providers.reserve(Options.size());
  for (const auto &[Name, Provider] : Options) {
    AttrSetClient *Client = Provider ? Provider->client() : nullptr;
    if (!Client) [[unlikely]]
      continue;
    if (OptionProviderErrors.contains(Name))
      continue;
    auto It = OptionGenerations.find(Name);
    if (It == OptionGenerations.end())
      It = OptionGenerations.emplace(Name, NextOptionGeneration++).first;
    Providers.push_back(OptionProviderRef{
        .Name = Name,
        .Client = Client,
        .Generation = It->second,
    });
  }
  return Providers;
}

bool Controller::allOptionProvidersReadyLocked() const {
  return std::all_of(Options.begin(), Options.end(), [&](const auto &Entry) {
    const auto &[Name, Provider] = Entry;
    return Provider && Provider->client() && ReadyOptions.contains(Name);
  });
}

bool Controller::allOptionProvidersSettledLocked() const {
  return std::all_of(Options.begin(), Options.end(), [&](const auto &Entry) {
    const auto &[Name, Provider] = Entry;
    return !Provider || !Provider->client() || SettledOptions.contains(Name);
  });
}

bool Controller::waitForOptionProvidersReadyForTests() {
  if (!useTrackedTestPool())
    return true;

  std::unique_lock Lock(OptionsLock);
  if (Options.empty())
    return true;

  OptionsReadyCV.wait(Lock, [this]() {
    return ShuttingDown || allOptionProvidersSettledLocked();
  });
  return allOptionProvidersSettledLocked();
}

bool Controller::optionProvidersReadyForDiagnostics() {
  std::lock_guard _(OptionsLock);
  return !Options.empty() && allOptionProvidersReadyLocked();
}

bool Controller::optionProvidersSettledForDiagnostics() {
  std::lock_guard _(OptionsLock);
  return !Options.empty() && allOptionProvidersSettledLocked();
}

std::vector<std::pair<std::string, std::string>>
Controller::optionProviderFailureSnapshot() {
  std::vector<std::pair<std::string, std::string>> Failures;
  std::lock_guard _(OptionsLock);
  Failures.reserve(OptionProviderErrors.size());
  for (const auto &[Name, Error] : OptionProviderErrors)
    Failures.emplace_back(Name, Error);
  return Failures;
}

std::vector<ResolvedOptionField>
Controller::completeOptions(const std::vector<std::string> &Scope,
                            const std::string &Prefix) {
  return OptService.complete(optionProviderSnapshot(), Scope, Prefix);
}

std::vector<ResolvedOptionField>
Controller::completeDerivedOptions(const std::vector<std::string> &Scope,
                                   const std::string &Prefix) {
  return OptService.completeDerived(optionProviderSnapshot(), Scope, Prefix);
}

std::vector<ResolvedOptionField>
Controller::completeDerivedOptionsForFile(
    std::string_view File, const std::vector<std::string> &Scope,
    const std::string &Prefix) {
  if (flake_schema::isFlakeFile(File))
    return flake_schema::completeDerived(Scope, Prefix);
  return completeDerivedOptions(Scope, Prefix);
}

std::vector<ResolvedOptionInfo>
Controller::resolveOptionInfos(const std::vector<std::string> &Scope) {
  return OptService.resolve(optionProviderSnapshot(), Scope);
}

std::vector<ResolvedOptionInfo>
Controller::resolveOptionInfosForFile(std::string_view File,
                                      const std::vector<std::string> &Scope) {
  if (flake_schema::isFlakeFile(File))
    return flake_schema::resolve(Scope);
  return resolveOptionInfos(Scope);
}

std::vector<ResolvedOptionInfo>
Controller::resolveDerivedOptionInfos(const std::vector<std::string> &Scope) {
  return OptService.resolveDerived(optionProviderSnapshot(), Scope);
}

std::vector<ResolvedOptionInfo> Controller::resolveDerivedOptionInfosForFile(
    std::string_view File, const std::vector<std::string> &Scope) {
  if (flake_schema::isFlakeFile(File))
    return flake_schema::resolveDerived(Scope);
  return resolveDerivedOptionInfos(Scope);
}

std::vector<lspserver::Location> Controller::optionDefinitionLocationsForFile(
    std::string_view File, const std::vector<std::string> &Scope,
    const std::vector<std::string> &FullScope) {
  std::vector<lspserver::Location> Locations;
  for (const ResolvedOptionInfo &Info : resolveOptionInfosForFile(File, Scope))
    appendDefinitionLocations(Locations, Info.Description);
  if (!Locations.empty())
    return Locations;

  for (size_t PrefixLen = Scope.size(); PrefixLen > 0; --PrefixLen) {
    std::vector<std::string> ParentScope(Scope.begin(),
                                         Scope.begin() + PrefixLen);
    std::vector<std::string> Suffix(Scope.begin() + PrefixLen, Scope.end());
    if (Suffix.empty())
      continue;

    for (const ResolvedOptionInfo &Info :
         resolveOptionInfosForFile(File, ParentScope)) {
      if (!deriveTypeFromResolvedInfo(Info, Suffix))
        continue;
      appendDefinitionLocations(Locations, Info.Description);
    }
    if (!Locations.empty())
      return Locations;
  }

  for (size_t PrefixLen = Scope.size() + 1; PrefixLen <= FullScope.size();
       ++PrefixLen) {
    std::vector<std::string> DescendantScope(FullScope.begin(),
                                             FullScope.begin() + PrefixLen);
    for (const ResolvedOptionInfo &Info :
         resolveOptionInfosForFile(File, DescendantScope))
      appendDefinitionLocations(Locations, Info.Description);
    if (!Locations.empty())
      return Locations;
  }

  return Locations;
}

std::vector<lspserver::Location>
Controller::optionDeclarationLocations(const std::vector<std::string> &Scope) {
  return OptService.declarationLocations(optionProviderSnapshot(), Scope);
}
