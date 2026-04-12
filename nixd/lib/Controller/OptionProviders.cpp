#include "nixd/Controller/Controller.h"

#include <boost/asio/post.hpp>

#include <llvm/Support/Error.h>

#include <algorithm>
#include <optional>
#include <semaphore>
#include <tuple>

using namespace nixd;

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

  std::binary_semaphore Ready(0);
  std::optional<OptionDescription> Desc;
  auto OnReply = [&Ready, &Desc, Name = Provider.Name](
                     llvm::Expected<OptionInfoResponse> Resp) {
    if (Resp) {
      Desc = *Resp;
    } else {
      lspserver::elog("option provider {0}: {1}", Name, Resp.takeError());
    }
    Ready.release();
  };

  Provider.Client->optionInfo(Scope, std::move(OnReply));
  Ready.acquire();

  {
    std::lock_guard _(CacheLock);
    InfoCache.insert_or_assign(std::move(Key), Desc);
  }
  return Desc;
}

std::vector<ResolvedOptionField>
OptionService::complete(const std::vector<OptionProviderRef> &Providers,
                        const std::vector<std::string> &Scope,
                        const std::string &Prefix) {
  std::vector<ResolvedOptionField> Fields;
  for (const OptionProviderRef &Provider : Providers) {
    if (!Provider.Client)
      continue;

    std::binary_semaphore Ready(0);
    OptionCompleteResponse Names;
    auto OnReply = [&Ready, &Names, Name = Provider.Name](
                       llvm::Expected<OptionCompleteResponse> Resp) {
      if (!Resp) {
        lspserver::elog("option worker {0}: {1}", Name, Resp.takeError());
        Ready.release();
        return;
      }
      Names = *Resp;
      Ready.release();
    };

    Provider.Client->optionComplete({Scope, Prefix}, std::move(OnReply));
    Ready.acquire();

    for (OptionField &Field : Names) {
      Fields.push_back(ResolvedOptionField{
          .ProviderName = Provider.Name,
          .Field = std::move(Field),
      });
    }
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
    OptionGenerations[std::string(Name)] = NextOptionGeneration++;
  }
  OptService.invalidateProvider(Name);
  boost::asio::post(Pool, [this]() { refreshDiagnostics(); });
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

std::vector<ResolvedOptionField>
Controller::completeOptions(const std::vector<std::string> &Scope,
                            const std::string &Prefix) {
  return OptService.complete(optionProviderSnapshot(), Scope, Prefix);
}

std::vector<ResolvedOptionInfo>
Controller::resolveOptionInfos(const std::vector<std::string> &Scope) {
  return OptService.resolve(optionProviderSnapshot(), Scope);
}

std::vector<lspserver::Location>
Controller::optionDeclarationLocations(const std::vector<std::string> &Scope) {
  return OptService.declarationLocations(optionProviderSnapshot(), Scope);
}
