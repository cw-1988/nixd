#pragma once

#include "Controller/Completion/Options.h"
#include "Controller/Option/Navigation.h"

#include <llvm/ADT/StringRef.h>

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace nixd::completion::options::value {

std::string lowerCopy(std::string_view S);

std::vector<option_navigation::ChildStep>
navigationPath(const OptionValueContext &Context);
std::string prefix(const OptionValueContext &Context);
void addInsertEdits(const OptionValueContext &Context, llvm::StringRef Src,
                    std::string_view Value, lspserver::CompletionItem &Item);
bool isSelectContext(const OptionValueContext &Context);

std::optional<OptionType> elemTypeFor(const OptionType &Type);
std::vector<OptionType> alternativeTypesFor(const OptionType &Type,
                                            std::string_view LowerName);

bool completeEnum(const OptionValueContext &Context, const OptionType &Type,
                  const std::string &Prefix, llvm::StringRef Src,
                  std::set<std::string> &SeenLabels,
                  std::vector<lspserver::CompletionItem> &Items);
bool completeBoolean(const OptionValueContext &Context, const OptionType &Type,
                     const std::string &Prefix, llvm::StringRef Src,
                     std::set<std::string> &SeenLabels,
                     std::vector<lspserver::CompletionItem> &Items);
bool completeNull(const OptionValueContext &Context,
                  const std::string &Prefix, llvm::StringRef Src,
                  std::set<std::string> &SeenLabels,
                  std::vector<lspserver::CompletionItem> &Items);

bool typeAcceptsPath(const OptionType &Type);
bool completePath(const OptionValueContext &Context, const OptionType &Type,
                  const std::string &Prefix, llvm::StringRef Src,
                  std::set<std::string> &SeenLabels,
                  std::vector<lspserver::CompletionItem> &Items);

bool typeAcceptsPackage(const OptionType &Type);
bool completePackage(const OptionValueContext &Context, const OptionType &Type,
                     const std::string &Prefix, llvm::StringRef Src,
                     AttrSetClient *NixpkgsClient,
                     std::set<std::string> &SeenLabels,
                     std::vector<lspserver::CompletionItem> &Items);

bool completeSnippet(const OptionValueContext &Context, const OptionType &Type,
                     const std::string &Prefix, llvm::StringRef Src,
                     bool ClientSupportSnippet,
                     std::set<std::string> &SeenLabels,
                     std::vector<lspserver::CompletionItem> &Items);

} // namespace nixd::completion::options::value
