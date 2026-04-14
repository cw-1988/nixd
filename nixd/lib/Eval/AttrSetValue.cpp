#include "AttrSetValue.h"

#include "lspserver/Protocol.h"

#include <nixt/Value.h>

#include <cassert>
#include <exception>

using namespace nixd;
using namespace lspserver;

namespace {

constexpr int MaxItems = 30;

void fillString(nix::EvalState &State, nix::Value &V,
                const std::vector<std::string_view> &AttrPath,
                std::optional<std::string> &Field) {
  try {
    nix::Value &Select = nixt::selectStringViews(State, V, AttrPath);
    State.forceValue(Select, nix::noPos);
    if (Select.type() == nix::ValueType::nString)
      Field = Select.string_view();
  } catch (std::exception &) {
    Field = std::nullopt;
  }
}

std::optional<Location> locationOf(nix::PosTable &PTable, nix::Value &V) {
  nix::PosIdx P = V.determinePos(nix::noPos);
  if (!P)
    return std::nullopt;

  nix::Pos NixPos = PTable[P];
  const auto *SP = std::get_if<nix::SourcePath>(&NixPos.origin);

  if (!SP)
    return std::nullopt;

  Position LPos = {
      .line = static_cast<int64_t>(NixPos.line - 1),
      .character = static_cast<int64_t>(NixPos.column - 1),
  };

  return Location{
      .uri = URIForFile::canonicalize(SP->path.abs(), SP->path.abs()),
      .range = {LPos, LPos},
  };
}

} // namespace

PackageDescription nixd::describePackage(nix::EvalState &State,
                                         nix::Value &Package) {
  PackageDescription R;
  fillString(State, Package, {"name"}, R.Name);
  fillString(State, Package, {"pname"}, R.PName);
  fillString(State, Package, {"version"}, R.Version);
  fillString(State, Package, {"meta", "description"}, R.Description);
  fillString(State, Package, {"meta", "longDescription"}, R.LongDescription);
  fillString(State, Package, {"meta", "position"}, R.Position);
  fillString(State, Package, {"meta", "homepage"}, R.Homepage);
  return R;
}

ValueMeta nixd::metadataOf(nix::EvalState &State, nix::Value &V) {
  return {
      .Type = V.type(true),
      .Location = locationOf(State.positions, V),
  };
}

std::optional<ValueDescription> nixd::describeValue(nix::EvalState &State,
                                                    nix::Value &V) {
  if (V.isPrimOp()) {
    const auto *PrimOp = V.primOp();
    assert(PrimOp);
    return ValueDescription{
        .Doc = PrimOp->doc.value_or(""),
        .Arity = static_cast<int>(PrimOp->arity),
        .Args = PrimOp->args,
    };
  } else if (V.isLambda()) {
    auto *Lambda = V.lambda().fun;
    assert(Lambda);
    const auto DocComment = Lambda->docComment;

    // Nix exposes doc comments for lambdas, but not arity or arguments.
    if (!DocComment)
      return std::nullopt;

    return ValueDescription{
        .Doc = DocComment.getInnerText(State.positions),
        .Arity = 0,
        .Args = {},
    };
  }

  return std::nullopt;
}

std::vector<std::string> nixd::completeNames(nix::Value &Scope,
                                             const nix::EvalState &State,
                                             std::string_view Prefix) {
  int Num = 0;
  std::vector<std::string> Names;

  for (const auto *AttrPtr : Scope.attrs()->lexicographicOrder(State.symbols)) {
    const nix::Attr &Attr = *AttrPtr;
    const std::string_view Name = State.symbols[Attr.name];
    if (Name.starts_with(Prefix)) {
      ++Num;
      Names.emplace_back(Name);
      if (Num > MaxItems)
        break;
    }
  }
  return Names;
}
