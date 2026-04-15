#include "nixd/Protocol/AttrSet.h"

using namespace nixd;
using namespace llvm::json;

Value nixd::toJSON(const OptionType::EnumValue &Params) {
  if (Params.String)
    return *Params.String;
  if (Params.Integer)
    return *Params.Integer;
  if (Params.Boolean)
    return *Params.Boolean;
  return nullptr;
}

bool nixd::fromJSON(const Value &Params, OptionType::EnumValue &R, Path P) {
  R = OptionType::EnumValue{};
  if (auto S = Params.getAsString()) {
    R.String = std::string(*S);
    return true;
  }
  if (auto I = Params.getAsInteger()) {
    R.Integer = I;
    return true;
  }
  if (auto B = Params.getAsBoolean()) {
    R.Boolean = B;
    return true;
  }
  if (Params.getAsNull()) {
    R.IsNull = true;
    return true;
  }
  P.report("expected enum scalar");
  return false;
}

Value nixd::toJSON(const OptionType::StringConstraint &Params) {
  Object O{
      {"NonEmpty", Params.NonEmpty},
      {"SingleLine", Params.SingleLine},
      {"PasswdEntry", Params.PasswdEntry},
      {"SystemdUnitName", Params.SystemdUnitName},
  };
  if (Params.Pattern)
    O.try_emplace("Pattern", *Params.Pattern);
  return O;
}

bool nixd::fromJSON(const Value &Params, OptionType::StringConstraint &R,
                    Path P) {
  ObjectMapper O(Params, P);
  return O                                              //
         && O.mapOptional("NonEmpty", R.NonEmpty)       //
         && O.mapOptional("SingleLine", R.SingleLine)   //
         && O.mapOptional("PasswdEntry", R.PasswdEntry) //
         && O.mapOptional("SystemdUnitName", R.SystemdUnitName) &&
         O.mapOptional("Pattern", R.Pattern);
}

Value nixd::toJSON(const OptionType::PathConstraint &Params) {
  Object Result{{"AcceptsStringLike", Params.AcceptsStringLike}};
  if (Params.Absolute)
    Result.try_emplace("Absolute", *Params.Absolute);
  if (Params.InStore)
    Result.try_emplace("InStore", *Params.InStore);
  return Result;
}

bool nixd::fromJSON(const Value &Params, OptionType::PathConstraint &R,
                    Path P) {
  const Object *O = Params.getAsObject();
  if (!O) {
    P.report("expected object");
    return false;
  }

  if (const Value *Absolute = O->get("Absolute")) {
    if (std::optional<bool> V = Absolute->getAsBoolean())
      R.Absolute = *V;
    else
      return false;
  }
  if (const Value *InStore = O->get("InStore")) {
    if (std::optional<bool> V = InStore->getAsBoolean())
      R.InStore = *V;
    else
      return false;
  }
  if (const Value *AcceptsStringLike = O->get("AcceptsStringLike")) {
    if (std::optional<bool> V = AcceptsStringLike->getAsBoolean())
      R.AcceptsStringLike = *V;
    else
      return false;
  }
  return true;
}

Value nixd::toJSON(const OptionType::KnownSubOption &Params) {
  return Object{
      {"HasDefault", Params.HasDefault},
      {"HasEmptyValue", Params.HasEmptyValue},
      {"Required", Params.Required},
      {"Truncated", Params.Truncated},
  };
}

bool nixd::fromJSON(const Value &Params, OptionType::KnownSubOption &R,
                    Path P) {
  R = OptionType::KnownSubOption{};
  ObjectMapper O(Params, P);
  return O                                                  //
         && O.mapOptional("HasDefault", R.HasDefault)       //
         && O.mapOptional("HasEmptyValue", R.HasEmptyValue) //
         && O.mapOptional("Required", R.Required)           //
         && O.mapOptional("Truncated", R.Truncated);
}

Value nixd::toJSON(const OptionType &Params) {
  Object O{
      {"Description", Params.Description},
      {"Name", Params.Name},
  };
  if (!Params.NestedTypes.empty()) {
    Object NestedTypes;
    for (const auto &[Name, Type] : Params.NestedTypes)
      NestedTypes.try_emplace(Name, toJSON(Type));
    O.try_emplace("NestedTypes", std::move(NestedTypes));
  }
  if (!Params.EnumValues.empty())
    O.try_emplace("EnumValues", Params.EnumValues);
  if (Params.String)
    O.try_emplace("StringConstraint", *Params.String);
  if (Params.Path)
    O.try_emplace("PathConstraint", *Params.Path);
  if (!Params.KnownSubOptions.empty()) {
    Object KnownSubOptions;
    for (const auto &[Name, Summary] : Params.KnownSubOptions)
      KnownSubOptions.try_emplace(Name, toJSON(Summary));
    O.try_emplace("KnownSubOptions", std::move(KnownSubOptions));
  }
  if (!Params.KnownSubOptionsComplete)
    O.try_emplace("KnownSubOptionsComplete", false);
  return O;
}

bool nixd::fromJSON(const Value &Params, OptionType &R, Path P) {
  R = OptionType{};
  ObjectMapper O(Params, P);
  return O                                                      //
         && O.mapOptional("Description", R.Description)         //
         && O.mapOptional("Name", R.Name)                       //
         && O.mapOptional("NestedTypes", R.NestedTypes)         //
         && O.mapOptional("EnumValues", R.EnumValues)           //
         && O.mapOptional("StringConstraint", R.String)         //
         && O.mapOptional("PathConstraint", R.Path)             //
         && O.mapOptional("KnownSubOptions", R.KnownSubOptions) //
         && O.mapOptional("KnownSubOptionsComplete", R.KnownSubOptionsComplete);
}

Value nixd::toJSON(const OptionDescription &Params) {
  return Object{
      {"Description", Params.Description},
      {"Declarations", Params.Declarations},
      {"Definitions", Params.Definitions},
      {"Example", Params.Example},
      {"Type", Params.Type},
  };
}
bool nixd::fromJSON(const Value &Params, OptionDescription &R, Path P) {
  ObjectMapper O(Params, P);
  return O                                                //
         && O.mapOptional("Description", R.Description)   //
         && O.mapOptional("Declarations", R.Declarations) //
         && O.mapOptional("Definitions", R.Definitions)   //
         && O.mapOptional("Example", R.Example)           //
         && O.mapOptional("Type", R.Type)                 //
      ;
}

Value nixd::toJSON(const OptionField &Params) {
  return Object{
      {"Name", Params.Name},
      {"Description", Params.Description},
  };
}
bool nixd::fromJSON(const Value &Params, OptionField &R, Path P) {
  ObjectMapper O(Params, P);
  return O                                              //
         && O.mapOptional("Description", R.Description) //
         && O.mapOptional("Name", R.Name)               //
      ;
}

Value nixd::toJSON(const PackageDescription &Params) {
  return Object{
      {"Name", Params.Name},
      {"PName", Params.PName},
      {"Version", Params.Version},
      {"Description", Params.Description},
      {"LongDescription", Params.LongDescription},
      {"Position", Params.Position},
      {"Homepage", Params.Homepage},
  };
}

bool nixd::fromJSON(const llvm::json::Value &Params, PackageDescription &R,
                    llvm::json::Path P) {
  ObjectMapper O(Params, P);
  return O                                              //
         && O.map("Name", R.Name)                       //
         && O.map("PName", R.PName)                     //
         && O.map("Version", R.Version)                 //
         && O.map("Description", R.Description)         //
         && O.map("LongDescription", R.LongDescription) //
         && O.map("Position", R.Position)               //
         && O.map("Homepage", R.Homepage)               //
      ;
}

Value nixd::toJSON(const ValueMeta &Params) {
  return Object{
      {"Type", Params.Type},
      {"Location", Params.Location},
  };
}

bool nixd::fromJSON(const llvm::json::Value &Params, ValueMeta &R,
                    llvm::json::Path P) {
  ObjectMapper O(Params, P);
  return O                                        //
         && O.map("Type", R.Type)                 //
         && O.mapOptional("Location", R.Location) //
      ;
}

Value nixd::toJSON(const AttrPathInfoResponse &Params) {
  return Object{
      {"Meta", Params.Meta},
      {"PackageDesc", Params.PackageDesc},
      {"ValueDesc", Params.ValueDesc},
  };
}

bool nixd::fromJSON(const llvm::json::Value &Params, AttrPathInfoResponse &R,
                    llvm::json::Path P) {
  ObjectMapper O(Params, P);
  return O                                              //
         && O.map("Meta", R.Meta)                       //
         && O.mapOptional("PackageDesc", R.PackageDesc) //
         && O.mapOptional("ValueDesc", R.ValueDesc)     //
      ;
}

Value nixd::toJSON(const AttrPathCompleteParams &Params) {
  return Object{{"Scope", Params.Scope}, {"Prefix", Params.Prefix}};
}
bool nixd::fromJSON(const llvm::json::Value &Params, AttrPathCompleteParams &R,
                    llvm::json::Path P) {
  ObjectMapper O(Params, P);
  return O                            //
         && O.map("Scope", R.Scope)   //
         && O.map("Prefix", R.Prefix) //
      ;
}

llvm::json::Value nixd::toJSON(const ValueDescription &Params) {
  return Object{
      {"arity", Params.Arity},
      {"doc", Params.Doc},
      {"args", Params.Args},
  };
}
bool nixd::fromJSON(const llvm::json::Value &Params, ValueDescription &R,
                    llvm::json::Path P) {

  ObjectMapper O(Params, P);
  return O                          //
         && O.map("arity", R.Arity) //
         && O.map("doc", R.Doc)     //
         && O.map("args", R.Args);
}
