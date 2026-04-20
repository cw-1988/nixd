# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let submoduleType = { name = "submodule"; description = "submodule"; getSubOptions = _: {}; }; in { services.example.goodList = { _type = "option"; type = { name = "listOf"; description = "list of signed integer"; }; }; services.example.badList = { _type = "option"; type = { name = "listOf"; description = "list of signed integer"; }; }; services.example.nullList = { _type = "option"; type = { name = "nullOr"; description = "null or (list of signed integer)"; }; }; services.example.badNullList = { _type = "option"; type = { name = "nullOr"; description = "null or (list of signed integer)"; }; }; services.example.goodAttrs = { _type = "option"; type = { name = "attrsOf"; description = "attribute set of string"; }; }; services.example.badAttrs = { _type = "option"; type = { name = "attrsOf"; description = "attribute set of string"; }; }; services.example.goodModule = { _type = "option"; type = submoduleType; }; services.example.badModule = { _type = "option"; type = submoduleType; }; services.example.fnModule = { _type = "option"; type = submoduleType; }; }' \
# RUN: < %s | FileCheck %s

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

```nix file:///basic.nix
{
  services.example.goodList = [];
  services.example.badList = "";
  services.example.nullList = null;
  services.example.badNullList = "";
  services.example.goodAttrs = {};
  services.example.badAttrs = "";
  services.example.goodModule = {};
  services.example.badModule = true;
  services.example.fnModule = name: {};
}
```

```
CHECK-NOT: value for option `services.example.goodList`
CHECK: "message": "value for option `services.example.badList` has type `string`, expected `listOf list of signed integer`"
CHECK-NOT: value for option `services.example.nullList`
CHECK: "message": "value for option `services.example.badNullList` has type `string`, expected `nullOr null or (list of signed integer)`"
CHECK-NOT: value for option `services.example.goodAttrs`
CHECK: "message": "value for option `services.example.badAttrs` has type `string`, expected `attrsOf attribute set of string`"
CHECK-NOT: value for option `services.example.goodModule`
CHECK: "message": "value for option `services.example.badModule` has type `boolean`, expected `submodule submodule`"
CHECK-NOT: value for option `services.example.fnModule`
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
