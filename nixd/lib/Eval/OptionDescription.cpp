#include "OptionDescription.h"

#include "lspserver/Protocol.h"

#include <nix/expr/attr-path.hh>
#include <nixt/Value.h>

#include <cassert>
#include <cctype>
#include <exception>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace nixd;

namespace {

// Describing leaf options can require evaluation. Keep that work bounded while
// still returning the complete set of cheap, structural namespace entries so
// the client can filter them locally.
constexpr int MaxOptions = 30;
constexpr int MaxOptionTypeDepth = 8;
constexpr int MaxKnownSubOptions = 64;

std::string toLowerCopy(std::string_view S) {
  std::string Lower;
  Lower.reserve(S.size());
  for (char C : S)
    Lower.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(C))));
  return Lower;
}

nix::Value *getAttr(nix::EvalState &State, nix::Value &V,
                    std::string_view Name) {
  State.forceValue(V, nix::noPos);
  if (V.type() != nix::ValueType::nAttrs || !V.attrs())
    return nullptr;

  const auto *It = V.attrs()->get(State.symbols.create(Name));
  if (!It)
    return nullptr;
  return It->value;
}

bool hasAttr(nix::EvalState &State, nix::Value &V, std::string_view Name) {
  return getAttr(State, V, Name) != nullptr;
}

std::optional<bool> getBool(nix::EvalState &State, nix::Value &V,
                            std::string_view Name) {
  nix::Value *Attr = getAttr(State, V, Name);
  if (!Attr)
    return std::nullopt;
  State.forceValue(*Attr, nix::noPos);
  if (Attr->type() != nix::ValueType::nBool)
    return std::nullopt;
  return Attr->boolean();
}

std::optional<std::string> getString(nix::EvalState &State, nix::Value &V,
                                     std::string_view Name) {
  nix::Value *Attr = getAttr(State, V, Name);
  if (!Attr)
    return std::nullopt;
  State.forceValue(*Attr, nix::noPos);
  if (Attr->type() != nix::ValueType::nString)
    return std::nullopt;
  return std::string(Attr->string_view());
}

std::optional<OptionType::EnumValue> scalarEnumValue(nix::EvalState &State,
                                                     nix::Value &V) {
  State.forceValue(V, nix::noPos);
  OptionType::EnumValue R;
  switch (V.type()) {
  case nix::ValueType::nString:
    R.String = std::string(V.string_view());
    return R;
  case nix::ValueType::nInt:
    R.Integer = static_cast<std::int64_t>(V.integer());
    return R;
  case nix::ValueType::nBool:
    R.Boolean = V.boolean();
    return R;
  case nix::ValueType::nNull:
    R.IsNull = true;
    return R;
  default:
    return std::nullopt;
  }
}

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

void fillUnsafeGetAttrPosLocation(nix::EvalState &State, nix::Value &V,
                                  lspserver::Location &Loc) {
  State.forceValue(V, nix::noPos);
  nix::Value &File = nixt::selectAttr(State, V, State.symbols.create("file"));
  nix::Value &Line = nixt::selectAttr(State, V, State.symbols.create("line"));
  nix::Value &Column =
      nixt::selectAttr(State, V, State.symbols.create("column"));

  State.forceValue(File, nix::noPos);
  State.forceValue(Line, nix::noPos);
  State.forceValue(Column, nix::noPos);

  if (File.type() == nix::ValueType::nString)
    Loc.uri = lspserver::URIForFile::canonicalize(File.c_str(), File.c_str());

  if (Line.type() == nix::ValueType::nInt &&
      Column.type() == nix::ValueType::nInt) {
    lspserver::Position Pos = {static_cast<int64_t>(Line.integer()) - 1,
                               static_cast<int64_t>(Column.integer()) - 1};
    Loc.range = {Pos, Pos};
  }
}

void fillOptionDeclarationPositions(nix::EvalState &State, nix::Value &V,
                                    OptionDescription &R) {
  State.forceValue(V, nix::noPos);
  if (V.type() != nix::ValueType::nList)
    return;
  for (nix::Value *Item : V.listView()) {
    lspserver::Location Loc;
    fillUnsafeGetAttrPosLocation(State, *Item, Loc);
    R.Declarations.emplace_back(std::move(Loc));
  }
}

void fillOptionDeclarations(nix::EvalState &State, nix::Value &V,
                            OptionDescription &R) {
  try {
    nix::Value &DeclarationPositions = nixt::selectAttr(
        State, V, State.symbols.create("declarationPositions"));

    State.forceValue(DeclarationPositions, nix::noPos);
    fillOptionDeclarationPositions(State, DeclarationPositions, R);
  } catch (nix::AttrPathNotFound &) {
    return;
  }
}

std::optional<nix::Value> callGetSubOptions(nix::EvalState &State,
                                            nix::Value &VType) {
  nix::Value *GetSubOptions = getAttr(State, VType, "getSubOptions");
  if (!GetSubOptions)
    return std::nullopt;

  auto List = State.buildList(0);
  auto EmptyList = State.allocValue();
  EmptyList->mkList(List);

  nix::Value Result;
  State.callFunction(*GetSubOptions, *EmptyList, Result, nix::noPos);
  State.forceValue(Result, nix::noPos);
  return Result;
}

void fillOptionType(nix::EvalState &State, nix::Value &VType, OptionType &R,
                    int Depth = 0);

bool hasUsableTypeMetadata(const OptionType &Type) {
  return Type.Description || Type.Name || !Type.NestedTypes.empty() ||
         !Type.EnumValues.empty() || Type.String || Type.Path ||
         !Type.KnownSubOptions.empty() || !Type.KnownSubOptionsComplete;
}

bool isOptionNode(nix::EvalState &State, nix::Value &V) {
  std::optional<std::string> Type = getString(State, V, "_type");
  return Type && *Type == "option";
}

void fillNestedTypes(nix::EvalState &State, nix::Value &VType, OptionType &R,
                     int Depth) {
  if (Depth >= MaxOptionTypeDepth)
    return;

  nix::Value *Nested = getAttr(State, VType, "nestedTypes");
  if (!Nested)
    return;

  State.forceValue(*Nested, nix::noPos);
  if (Nested->type() != nix::ValueType::nAttrs || !Nested->attrs())
    return;

  for (const auto *AttrPtr :
       Nested->attrs()->lexicographicOrder(State.symbols)) {
    const nix::Attr &Attr = *AttrPtr;
    OptionType Child;
    fillOptionType(State, *Attr.value, Child, Depth + 1);
    R.NestedTypes.emplace(std::string(State.symbols[Attr.name]),
                          std::move(Child));
  }
}

void fillEnumValues(nix::EvalState &State, nix::Value &Payload, OptionType &R) {
  nix::Value *Values = getAttr(State, Payload, "values");
  if (!Values)
    return;

  State.forceValue(*Values, nix::noPos);
  if (Values->type() != nix::ValueType::nList)
    return;

  for (nix::Value *Item : Values->listView()) {
    if (std::optional<OptionType::EnumValue> Scalar =
            scalarEnumValue(State, *Item))
      R.EnumValues.emplace_back(std::move(*Scalar));
  }
}

bool isStringPasswdEntry(std::string_view LowerName, const OptionType &R) {
  if (LowerName == "passwdentry" || LowerName == "passwdentry str")
    return true;
  if (!LowerName.starts_with("passwdentry ") || !R.Description)
    return false;
  const std::string LowerDescription = toLowerCopy(*R.Description);
  return LowerDescription.starts_with("string,");
}

void fillStringConstraint(nix::EvalState &State, nix::Value *Payload,
                          OptionType &R, std::string_view LowerName) {
  OptionType::StringConstraint Constraint;
  bool HasConstraint = false;

  if (LowerName == "nonemptystr") {
    Constraint.NonEmpty = true;
    HasConstraint = true;
  } else if (LowerName == "singlelinestr") {
    Constraint.SingleLine = true;
    HasConstraint = true;
  } else if (isStringPasswdEntry(LowerName, R)) {
    Constraint.PasswdEntry = true;
    Constraint.SingleLine = true;
    HasConstraint = true;
  } else if (LowerName == "systemdunitname") {
    Constraint.SystemdUnitName = true;
    HasConstraint = true;
  }

  if (Payload) {
    State.forceValue(*Payload, nix::noPos);
    if (Payload->type() == nix::ValueType::nString) {
      if (LowerName == "strmatching") {
        Constraint.Pattern = std::string(Payload->string_view());
        HasConstraint = true;
      }
    } else if (Payload->type() == nix::ValueType::nAttrs) {
      if (std::optional<std::string> Pattern =
              getString(State, *Payload, "pattern")) {
        Constraint.Pattern = std::move(*Pattern);
        HasConstraint = true;
      }
      if (std::optional<bool> NonEmpty = getBool(State, *Payload, "nonEmpty")) {
        Constraint.NonEmpty = *NonEmpty;
        HasConstraint = true;
      }
      if (std::optional<bool> SingleLine =
              getBool(State, *Payload, "singleLine")) {
        Constraint.SingleLine = *SingleLine;
        HasConstraint = true;
      }
      if (std::optional<bool> PasswdEntry =
              getBool(State, *Payload, "passwdEntry")) {
        Constraint.PasswdEntry = *PasswdEntry;
        HasConstraint = true;
      }
      if (std::optional<bool> SystemdUnitName =
              getBool(State, *Payload, "systemdUnitName")) {
        Constraint.SystemdUnitName = *SystemdUnitName;
        HasConstraint = true;
      }
    }
  }

  if (HasConstraint)
    R.String = std::move(Constraint);
}

void fillPathConstraint(nix::EvalState &State, nix::Value *Payload,
                        OptionType &R, std::string_view LowerName) {
  OptionType::PathConstraint Constraint;
  bool HasConstraint = false;

  if (LowerName == "pathinstore") {
    Constraint.Absolute = true;
    Constraint.InStore = true;
    Constraint.AcceptsStringLike = true;
    HasConstraint = true;
  } else if (LowerName == "path" && R.Description &&
             toLowerCopy(*R.Description) == "absolute path") {
    Constraint.Absolute = true;
    Constraint.AcceptsStringLike = true;
    HasConstraint = true;
  }

  if (Payload) {
    bool SawStringLikePathPayload = false;
    if (std::optional<bool> Absolute = getBool(State, *Payload, "absolute")) {
      Constraint.Absolute = *Absolute;
      HasConstraint = true;
      if (*Absolute)
        SawStringLikePathPayload = true;
    }
    if (std::optional<bool> InStore = getBool(State, *Payload, "inStore")) {
      Constraint.InStore = *InStore;
      if (*InStore) {
        Constraint.Absolute = true;
        SawStringLikePathPayload = true;
      }
      HasConstraint = true;
    }
    if (std::optional<bool> AcceptsString =
            getBool(State, *Payload, "acceptsStringLike")) {
      Constraint.AcceptsStringLike = *AcceptsString;
      HasConstraint = true;
      SawStringLikePathPayload = false;
    }
    if (SawStringLikePathPayload)
      Constraint.AcceptsStringLike = true;
  }

  if (HasConstraint)
    R.Path = Constraint;
}

void fillSubmoduleOptionTree(nix::EvalState &State, nix::Value &Options,
                             OptionType &R, int Depth) {
  if (Depth >= MaxOptionTypeDepth) {
    R.KnownSubOptionsComplete = false;
    return;
  }

  State.forceValue(Options, nix::noPos);
  if (Options.type() != nix::ValueType::nAttrs || !Options.attrs()) {
    R.KnownSubOptionsComplete = false;
    return;
  }

  int Count = 0;
  for (const auto *AttrPtr :
       Options.attrs()->lexicographicOrder(State.symbols)) {
    if (Count >= MaxKnownSubOptions) {
      R.KnownSubOptionsComplete = false;
      return;
    }

    const nix::Attr &Attr = *AttrPtr;
    const std::string Name(State.symbols[Attr.name]);
    OptionType::KnownSubOption Summary;
    bool Counted = false;

    try {
      State.forceValue(*Attr.value, nix::noPos);
    } catch (const std::exception &) {
      R.KnownSubOptionsComplete = false;
      continue;
    }

    try {
      if (isOptionNode(State, *Attr.value)) {
        Summary.HasDefault = hasAttr(State, *Attr.value, "default");
        Summary.HasEmptyValue = hasAttr(State, *Attr.value, "emptyValue");
        if (std::optional<bool> Required =
                getBool(State, *Attr.value, "required"))
          Summary.Required = *Required;

        R.KnownSubOptions.emplace(Name, Summary);
        ++Count;
        Counted = true;

        if (nix::Value *ChildType = getAttr(State, *Attr.value, "type")) {
          OptionType Child;
          fillOptionType(State, *ChildType, Child, Depth + 1);
          if (hasUsableTypeMetadata(Child))
            R.NestedTypes[Name] = std::move(Child);
        }
        continue;
      }
    } catch (const std::exception &) {
      R.KnownSubOptionsComplete = false;
      if (!Counted) {
        R.KnownSubOptions.emplace(Name, Summary);
        ++Count;
      }
      continue;
    }

    if (Attr.value->type() == nix::ValueType::nAttrs && Attr.value->attrs()) {
      OptionType ChildNamespace;
      try {
        fillSubmoduleOptionTree(State, *Attr.value, ChildNamespace, Depth + 1);
      } catch (const std::exception &) {
        ChildNamespace.KnownSubOptionsComplete = false;
      }
      R.KnownSubOptions.emplace(Name, Summary);
      if (hasUsableTypeMetadata(ChildNamespace))
        R.NestedTypes.emplace(Name, std::move(ChildNamespace));
      ++Count;
      continue;
    }

    R.KnownSubOptionsComplete = false;
  }
}

void fillSubmoduleOptions(nix::EvalState &State, nix::Value &VType,
                          OptionType &R, int Depth) {
  if (Depth >= MaxOptionTypeDepth) {
    R.KnownSubOptionsComplete = false;
    return;
  }

  std::optional<nix::Value> SubOptions;
  try {
    SubOptions = callGetSubOptions(State, VType);
  } catch (const std::exception &) {
    R.KnownSubOptionsComplete = false;
    return;
  }
  if (!SubOptions) {
    R.KnownSubOptionsComplete = false;
    return;
  }

  try {
    fillSubmoduleOptionTree(State, *SubOptions, R, Depth);
  } catch (const std::exception &) {
    R.KnownSubOptionsComplete = false;
  }
}

void fillPayloadMetadata(nix::EvalState &State, nix::Value &VType,
                         OptionType &R, int Depth, std::string_view LowerName) {
  nix::Value *Functor = getAttr(State, VType, "functor");
  nix::Value *Payload = Functor ? getAttr(State, *Functor, "payload") : nullptr;
  if (!Payload)
    Payload = getAttr(State, VType, "payload");

  if (Payload && LowerName == "enum")
    fillEnumValues(State, *Payload, R);

  fillStringConstraint(State, Payload, R, LowerName);
  fillPathConstraint(State, Payload, R, LowerName);

  if (!Payload)
    return;

  if (Depth < MaxOptionTypeDepth) {
    if (nix::Value *FreeformType = getAttr(State, *Payload, "freeformType");
        FreeformType && !R.NestedTypes.contains("freeformType")) {
      OptionType Child;
      fillOptionType(State, *FreeformType, Child, Depth + 1);
      R.NestedTypes.emplace("freeformType", std::move(Child));
    }
  }
}

void fillOptionType(nix::EvalState &State, nix::Value &VType, OptionType &R,
                    int Depth) {
  fillString(State, VType, {"description"}, R.Description);
  fillString(State, VType, {"name"}, R.Name);

  std::string LowerName = R.Name ? toLowerCopy(*R.Name) : "";
  try {
    fillNestedTypes(State, VType, R, Depth);
  } catch (const std::exception &) {
  }
  try {
    fillPayloadMetadata(State, VType, R, Depth, LowerName);
  } catch (const std::exception &) {
  }
  if (LowerName == "submodule" || LowerName == "submodulewith") {
    try {
      fillSubmoduleOptions(State, VType, R, Depth);
    } catch (const std::exception &) {
    }
  }
}

} // namespace

void nixd::fillOptionDescription(nix::EvalState &State, nix::Value &V,
                                 OptionDescription &R) {
  fillString(State, V, {"description"}, R.Description);
  fillOptionDeclarations(State, V, R);
  // FIXME: add definitions location.
  if (V.type() == nix::ValueType::nAttrs) [[likely]] {
    assert(V.attrs());
    if (auto *It = V.attrs()->get(State.symbols.create("type"))) [[likely]] {
      OptionType Type;
      fillOptionType(State, *It->value, Type);
      R.Type = std::move(Type);
    }

    if (auto *It = V.attrs()->get(State.symbols.create("example"))) {
      State.forceValue(*It->value, It->pos);

      // In nixpkgs some examples are nested in "literalExpression".
      if (nixt::checkField(State, *It->value, "_type", "literalExpression")) {
        R.Example = nixt::getFieldString(State, *It->value, "text");
      } else {
        std::ostringstream OS;
        It->value->print(State, OS);
        R.Example = OS.str();
      }
    }
  }
}

static void fillOptionSummary(nix::EvalState &State, nix::Value &V,
                              OptionDescription &R) {
  fillString(State, V, {"description"}, R.Description);
  if (nix::Value *VType = getAttr(State, V, "type")) {
    OptionType Type;
    fillString(State, *VType, {"description"}, Type.Description);
    fillString(State, *VType, {"name"}, Type.Name);
    R.Type = std::move(Type);
  }
}

OptionCompleteResponse nixd::completeOptionsInScope(nix::EvalState &State,
                                                    nix::Value &Scope,
                                                    std::string_view Prefix,
                                                    bool FullDescriptions) {
  OptionCompleteResponse Response;
  size_t OptionCount = 0;

  for (const auto *AttrPtr : Scope.attrs()->lexicographicOrder(State.symbols)) {
    const nix::Attr &Attr = *AttrPtr;
    std::string_view Name = State.symbols[Attr.name];
    if (!Name.starts_with(Prefix))
      continue;

    assert(Attr.value);
    OptionField NewField;
    NewField.Name = Name;
    if (nixt::isOption(State, *Attr.value)) {
      if (OptionCount >= MaxOptions)
        continue;
      ++OptionCount;

      OptionDescription Desc;
      if (FullDescriptions)
        fillOptionDescription(State, *Attr.value, Desc);
      else
        fillOptionSummary(State, *Attr.value, Desc);
      NewField.Description = std::move(Desc);
    }
    Response.emplace_back(std::move(NewField));
  }
  return Response;
}
