# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let intType = { name = "int"; description = "signed integer"; }; strType = { name = "str"; description = "string"; }; boolType = { name = "bool"; description = "boolean"; }; freeModule = { name = "submodule"; description = "submodule"; nestedTypes.freeformType = boolType; getSubOptions = _: { known = { _type = "option"; type = intType; }; }; }; strictModule = { name = "submodule"; description = "submodule"; getSubOptions = _: { known = { _type = "option"; type = intType; }; }; }; requiredModule = { name = "submodule"; description = "submodule"; getSubOptions = _: { known = { _type = "option"; type = intType; default = 1; }; required = { _type = "option"; type = strType; required = true; }; }; }; in { services.example.ports = { _type = "option"; type = { name = "listOf"; description = "list of signed integer"; nestedTypes.elemType = intType; }; }; services.example.settings = { _type = "option"; type = { name = "attrsOf"; description = "attribute set of signed integer"; nestedTypes.elemType = intType; }; }; services.example.mode = { _type = "option"; type = { name = "enum"; description = "one of fast or slow"; functor.payload.values = [ "fast" "slow" ]; }; }; services.example.freeModule = { _type = "option"; type = freeModule; }; services.example.strictModule = { _type = "option"; type = strictModule; }; services.example.requiredModule = { _type = "option"; type = requiredModule; }; services.example.fn = { _type = "option"; type = { name = "functionTo"; description = "function that evaluates to signed integer"; nestedTypes.resultType = intType; }; }; services.example.store = { _type = "option"; type = { name = "pathInStore"; description = "store path"; functor.payload = {}; }; }; services.example.coerced = { _type = "option"; type = { name = "coercedTo"; description = "string coerced to signed integer"; nestedTypes.coercedType = strType; nestedTypes.finalType = intType; }; }; }' \
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
let
  badPort = "8080";
  goodPort = 8080;
in
{
  services.example.ports = [ 80 "8080" badPort goodPort ];
  services.example.settings = {
    good = 1;
    bad = "2";
  };
  services.example.mode = "medium";
  services.example.freeModule = {
    known = "bad";
    arbitrary = "bad";
  };
  services.example.strictModule = {
    known = 1;
    mystery = true;
  };
  services.example.requiredModule = {
    known = 1;
  };
  services.example.fn = x: "bad";
  services.example.store = ./relative;
  services.example.coerced = false;
}
```

```
CHECK: "message": "value for option `services.example.ports[]` has type `string`, expected `int signed integer`"
CHECK: "message": "value for option `services.example.ports[]` has type `string`, expected `int signed integer`"
CHECK: "message": "value for option `services.example.settings.bad` has type `string`, expected `int signed integer`"
CHECK: "message": "value for option `services.example.mode` has type `string`, expected `enum one of fast or slow`"
CHECK: "message": "value for option `services.example.freeModule.known` has type `string`, expected `int signed integer`"
CHECK: "message": "value for option `services.example.freeModule.arbitrary` has type `string`, expected `bool boolean`"
CHECK: "code": "option-unknown"
CHECK: "message": "unknown option `services.example.strictModule.mystery`"
CHECK: "code": "option-required-missing"
CHECK: "message": "required option `services.example.requiredModule.required` is missing"
CHECK: "message": "value for option `services.example.fn.<return>` has type `string`, expected `int signed integer`"
CHECK: "message": "value for option `services.example.store` has type `path`, expected `pathInStore store path`"
CHECK: "message": "value for option `services.example.coerced` has type `boolean`, expected `str string`"
CHECK-NOT: value for option `services.example.ports[]` has type `integer`
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
