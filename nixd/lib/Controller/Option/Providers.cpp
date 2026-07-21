#include "Navigation.h"
#include "nixd/Controller/Controller.h"

#include <boost/asio/post.hpp>

#include <llvm/Support/Error.h>

#include <nix/util/config-global.hh>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
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
  bool Succeeded = false;
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

std::vector<ResolvedOptionField> fieldsFromType(std::string_view ProviderName,
                                                const OptionType &Type,
                                                const std::string &Prefix);

bool hasField(const std::vector<ResolvedOptionField> &Fields,
              std::string_view Name) {
  return std::any_of(Fields.begin(), Fields.end(),
                     [&](const ResolvedOptionField &Field) {
                       return Field.Field.Name == Name;
                     });
}

using NixSettingMap = std::map<std::string, nix::AbstractConfig::SettingInfo>;

NixSettingMap globalConfigSettings() {
  NixSettingMap Settings;
  nix::globalConfig.getSettings(Settings);
  return Settings;
}

bool hasGlobalConfigSettingEvidence(
    const std::vector<ResolvedOptionField> &Fields,
    const NixSettingMap &Settings) {
  int Matches = 0;
  for (const ResolvedOptionField &Field : Fields) {
    if (Settings.contains(Field.Field.Name))
      ++Matches;
    if (Matches >= 2)
      return true;
  }
  return false;
}

void appendGlobalConfigSettings(
    std::vector<ResolvedOptionField> &Fields,
    const std::vector<ResolvedOptionField> &Evidence,
    const std::vector<OptionProviderRef> &Providers,
    const std::string &Prefix) {
  NixSettingMap Settings = globalConfigSettings();

  if (!hasGlobalConfigSettingEvidence(Evidence, Settings))
    return;

  const std::string ProviderName =
      Providers.empty() ? "nixos" : Providers.front().Name;
  for (const auto &[Name, Info] : Settings) {
    if (!Name.starts_with(Prefix) || hasField(Fields, Name))
      continue;

    OptionField Field;
    Field.Name = Name;

    OptionDescription Desc;
    Desc.Description = Info.description;
    Field.Description = std::move(Desc);

    Fields.push_back(ResolvedOptionField{.ProviderName = ProviderName,
                                         .Field = std::move(Field)});
  }
}

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

} // namespace

bool OptionService::InfoCacheKey::operator<(
    const OptionService::InfoCacheKey &Other) const {
  return std::tie(ProviderName, Generation, Scope) <
         std::tie(Other.ProviderName, Other.Generation, Other.Scope);
}

bool OptionService::CompleteCacheKey::operator<(
    const OptionService::CompleteCacheKey &Other) const {
  return std::tie(ProviderName, Generation, Scope, Prefix, FullDescriptions) <
         std::tie(Other.ProviderName, Other.Generation, Other.Scope,
                  Other.Prefix, Other.FullDescriptions);
}

void OptionService::invalidate() {
  std::lock_guard _(CacheLock);
  InfoCache.clear();
  CompleteCache.clear();
}

void OptionService::invalidateProvider(std::string_view ProviderName) {
  std::lock_guard _(CacheLock);
  std::erase_if(InfoCache, [&](const auto &Entry) {
    return Entry.first.ProviderName == ProviderName;
  });
  std::erase_if(CompleteCache, [&](const auto &Entry) {
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
  auto OnReply =
      [State, Name = Provider.Name](llvm::Expected<OptionInfoResponse> Resp) {
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
                        const std::string &Prefix, bool FullDescriptions) {
  std::vector<ResolvedOptionField> Fields;
  for (const OptionProviderRef &Provider : Providers) {
    if (!Provider.Client)
      continue;

    CompleteCacheKey Key{.ProviderName = Provider.Name,
                         .Generation = Provider.Generation,
                         .Scope = Scope,
                         .Prefix = Prefix,
                         .FullDescriptions = FullDescriptions};
    OptionCompleteResponse Names;
    bool CacheHit = false;
    {
      std::lock_guard _(CacheLock);
      if (auto It = CompleteCache.find(Key); It != CompleteCache.end()) {
        Names = It->second;
        CacheHit = true;
      }
    }

    if (CacheHit) {
      for (const OptionField &Field : Names)
        Fields.push_back(
            ResolvedOptionField{.ProviderName = Provider.Name, .Field = Field});
      continue;
    }

    auto State = std::make_shared<ProviderCompleteReplyState>();
    auto OnReply = [State, Name = Provider.Name](
                       llvm::Expected<OptionCompleteResponse> Resp) {
      if (!Resp) {
        lspserver::elog("option worker {0}: {1}", Name, Resp.takeError());
        State->Ready.release();
        return;
      }
      State->Names = std::move(*Resp);
      State->Succeeded = true;
      State->Ready.release();
    };

    Provider.Client->optionComplete({.Scope = Scope,
                                     .Prefix = Prefix,
                                     .FullDescriptions = FullDescriptions},
                                    std::move(OnReply));
    State->Ready.acquire();

    if (State->Succeeded) {
      std::lock_guard _(CacheLock);
      CompleteCache.insert_or_assign(std::move(Key), State->Names);
    }

    for (const OptionField &Field : State->Names) {
      Fields.push_back(ResolvedOptionField{
          .ProviderName = Provider.Name,
          .Field = Field,
      });
    }
  }
  return Fields;
}

std::vector<ResolvedOptionField>
OptionService::completeDerived(const std::vector<OptionProviderRef> &Providers,
                               const std::vector<std::string> &Scope,
                               const std::string &Prefix,
                               bool FullDescriptions) {
  std::vector<ResolvedOptionField> Fields =
      complete(Providers, Scope, Prefix, FullDescriptions);
  std::vector<ResolvedOptionField> Evidence =
      Prefix.empty() ? Fields
                     : complete(Providers, Scope, "", FullDescriptions);
  appendGlobalConfigSettings(Fields, Evidence, Providers, Prefix);
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

void Controller::noteOptionProviderChanged(std::string_view Name) {
  {
    std::lock_guard _(OptionsLock);
    std::string ProviderName(Name);
    ReadyOptions.insert(ProviderName);
    SettledOptions.insert(ProviderName);
    OptionGenerations[std::move(ProviderName)] = NextOptionGeneration++;
  }
  OptService.invalidateProvider(Name);
  if (!ShuttingDown)
    postToDiagnosticsPool([this]() { refreshDiagnostics(); });
  OptionsReadyCV.notify_all();
}

void Controller::noteOptionProviderSettled(std::string_view Name) {
  {
    std::lock_guard _(OptionsLock);
    SettledOptions.insert(std::string(Name));
  }
  OptionsReadyCV.notify_all();
}

std::vector<OptionProviderRef> Controller::optionProviderSnapshot() {
  std::vector<OptionProviderRef> Providers;
  std::lock_guard _(OptionsLock);
  Providers.reserve(Options.size());
  for (const auto &[Name, Provider] : Options) {
    AttrSetClient *Client = Provider ? Provider->client() : nullptr;
    if (!Client) [[unlikely]]
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
  return allOptionProvidersReadyLocked();
}

bool Controller::optionProvidersReadyForDiagnostics() {
  std::lock_guard _(OptionsLock);
  return !Options.empty() && allOptionProvidersReadyLocked();
}

std::vector<ResolvedOptionField>
Controller::completeOptions(const std::vector<std::string> &Scope,
                            const std::string &Prefix) {
  return OptService.complete(optionProviderSnapshot(), Scope, Prefix);
}

std::vector<ResolvedOptionField>
Controller::completeDerivedOptions(const std::vector<std::string> &Scope,
                                   const std::string &Prefix,
                                   bool FullDescriptions) {
  return OptService.completeDerived(optionProviderSnapshot(), Scope, Prefix,
                                    FullDescriptions);
}

std::vector<ResolvedOptionInfo>
Controller::resolveOptionInfos(const std::vector<std::string> &Scope) {
  return OptService.resolve(optionProviderSnapshot(), Scope);
}

std::vector<ResolvedOptionInfo>
Controller::resolveDerivedOptionInfos(const std::vector<std::string> &Scope) {
  return OptService.resolveDerived(optionProviderSnapshot(), Scope);
}

std::vector<lspserver::Location>
Controller::optionDeclarationLocations(const std::vector<std::string> &Scope) {
  return OptService.declarationLocations(optionProviderSnapshot(), Scope);
}
