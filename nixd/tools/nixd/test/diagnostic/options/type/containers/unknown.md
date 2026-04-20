# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let intType = { name = "int"; description = "signed integer"; }; strType = { name = "str"; description = "string"; }; strictModule = { name = "submodule"; description = "submodule"; getSubOptions = _: { known = { _type = "option"; type = intType; }; }; }; freeModule = { name = "submodule"; description = "submodule"; nestedTypes.freeformType = strType; getSubOptions = _: { known = { _type = "option"; type = intType; }; }; }; listType = { name = "listOf"; description = "list of signed integer"; nestedTypes.elemType = intType; }; nullListType = { name = "nullOr"; description = "null or list of signed integer"; nestedTypes.elemType = listType; }; uniqueListType = { name = "unique"; description = "unique list of signed integer"; nestedTypes.elemType = listType; }; eitherListType = { name = "either"; description = "list or string"; nestedTypes.left = listType; nestedTypes.right = strType; }; attrsType = { name = "attrsOf"; description = "attribute set of signed integer"; nestedTypes.elemType = intType; }; loaType = { name = "loaOf"; description = "list of attribute sets of signed integer"; nestedTypes.elemType = intType; }; attrsWithType = { name = "attrsWith"; description = "attribute set of signed integer"; nestedTypes.elemType = intType; }; in { services.example.nonEmpty = { _type = "option"; type = { name = "listOf"; description = "non-empty list of signed integer"; nestedTypes.elemType = intType; }; }; services.example.listPath = { _type = "option"; type = listType; }; services.example.listValue = { _type = "option"; type = listType; }; services.example.nullListValue = { _type = "option"; type = nullListType; }; services.example.uniqueListValue = { _type = "option"; type = uniqueListType; }; services.example.eitherListValue = { _type = "option"; type = eitherListType; }; services.example.loaPath = { _type = "option"; type = loaType; }; services.example.loaTop = { _type = "option"; type = loaType; }; services.example.loaElem = { _type = "option"; type = loaType; }; services.example.attrsTop = { _type = "option"; type = attrsWithType; }; services.example.attrsElem = { _type = "option"; type = attrsWithType; }; services.example.strict = { _type = "option"; type = strictModule; }; services.example.strictInherit = { _type = "option"; type = strictModule; }; services.example.free = { _type = "option"; type = freeModule; }; services.example.dynamic = { _type = "option"; type = attrsType; }; }' \
# RUN: < %s | FileCheck \
# RUN: --implicit-check-not='strictInherit.mystery' \
# RUN: --implicit-check-not='free.arbitrary' \
# RUN: --implicit-check-not='dynamic.anything.deep' \
# RUN: --implicit-check-not='unknown option `services.example.listValue.bad`' \
# RUN: --implicit-check-not='unknown option `services.example.nullListValue.bad`' \
# RUN: --implicit-check-not='unknown option `services.example.uniqueListValue.bad`' \
# RUN: --implicit-check-not='unknown option `services.example.eitherListValue.bad`' \
# RUN: --implicit-check-not='unknown option `imports`' \
# RUN: %s

<-- initialize(0)

```json
{
   "jsonrpc":"2.0",
   "id":0,
   "method":"initialize",
   "params":{
      "processId":123,
      "rootPath":"",
      "capabilities":{
      },
      "trace":"off"
   }
}
```

<-- textDocument/didOpen

```nix file:///container-unknown.nix
let
  foo = 1;
in
{
  services.example.nonEmpty = [];
  services.example.loaTop = { bad = 1; };
  services.example.loaElem = [ { good = 1; bad = "x"; } ];
  services.example.listPath.bad = true;
  services.example.loaPath.bad = true;
  services.example.listValue = { bad = true; };
  services.example.nullListValue = { bad = true; };
  services.example.uniqueListValue = { bad = true; };
  services.example.eitherListValue = { bad = true; };
  services.example.attrsTop = "bad";
  services.example.attrsElem = { good = 1; bad = "x"; };
  services.example.strict = {
    known = 1;
    mystery = true;
  };
  services.example.strictInherit = {
    inherit foo;
    mystery = true;
  };
  services.example.free = {
    arbitrary = "ok";
  };
  services.example.dynamic.anything.deep = 1;
  imports = [];
  services.example.typo = true;
}
```

```
CHECK-NOT: strictInherit.mystery
CHECK-NOT: free.arbitrary
CHECK-NOT: dynamic.anything.deep
CHECK-NOT: unknown option `services.example.listValue.bad`
CHECK-NOT: unknown option `services.example.nullListValue.bad`
CHECK-NOT: unknown option `services.example.uniqueListValue.bad`
CHECK-NOT: unknown option `services.example.eitherListValue.bad`
CHECK-NOT: unknown option `imports`
CHECK-DAG: "message": "value for option `services.example.nonEmpty` has type `list`, expected `listOf non-empty list of signed integer`"
CHECK-DAG: "message": "value for option `services.example.loaTop` has type `attribute set`, expected `loaOf list of attribute sets of signed integer`"
CHECK-DAG: "message": "value for option `services.example.loaElem[].bad` has type `string`, expected `int signed integer`"
CHECK-DAG: "message": "unknown option `services.example.listPath.bad`"
CHECK-DAG: "message": "unknown option `services.example.loaPath.bad`"
CHECK-DAG: "message": "value for option `services.example.listValue` has type `attribute set`, expected `listOf list of signed integer`"
CHECK-DAG: "message": "value for option `services.example.nullListValue` has type `attribute set`, expected `nullOr null or list of signed integer`"
CHECK-DAG: "message": "value for option `services.example.uniqueListValue` has type `attribute set`, expected `listOf list of signed integer`"
CHECK-DAG: "message": "value for option `services.example.eitherListValue` has type `attribute set`, expected `listOf list of signed integer`"
CHECK-DAG: "message": "value for option `services.example.attrsTop` has type `string`, expected `attrsWith attribute set of signed integer`"
CHECK-DAG: "message": "value for option `services.example.attrsElem.bad` has type `string`, expected `int signed integer`"
CHECK-DAG: "message": "unknown option `services.example.strict.mystery`"
CHECK-DAG: "message": "unknown option `services.example.typo`"
CHECK-NOT: strictInherit.mystery
CHECK-NOT: free.arbitrary
CHECK-NOT: dynamic.anything.deep
CHECK-NOT: unknown option `services.example.listValue.bad`
CHECK-NOT: unknown option `services.example.nullListValue.bad`
CHECK-NOT: unknown option `services.example.uniqueListValue.bad`
CHECK-NOT: unknown option `services.example.eitherListValue.bad`
CHECK-NOT: unknown option `imports`
```

<-- nixd/waitForOptionsSettled(999)

```json
{
  "jsonrpc": "2.0",
  "id": 999,
  "method": "nixd/waitForOptionsSettled",
  "params": null
}
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
