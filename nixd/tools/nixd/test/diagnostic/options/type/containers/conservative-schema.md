# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let intType = { name = "int"; description = "signed integer"; }; strType = { name = "str"; description = "string"; }; nestedModule = { name = "submodule"; description = "submodule"; getSubOptions = _: { settings = { bar = { _type = "option"; type = intType; }; }; }; }; knownNoTypeModule = { name = "submodule"; description = "submodule"; getSubOptions = _: { knownNoType = { _type = "option"; }; }; }; requiredPolicyModule = { name = "submodule"; description = "submodule"; getSubOptions = _: { requiredFalse = { _type = "option"; type = strType; required = false; }; missingRequired = { _type = "option"; type = strType; }; explicit = { _type = "option"; type = strType; required = true; }; }; }; largeModule = { name = "submodule"; description = "submodule"; getSubOptions = _: builtins.listToAttrs (builtins.genList (n: { name = "opt${toString (n + 100)}"; value = { _type = "option"; type = intType; }; }) 70); }; partialModule = { name = "submodule"; description = "submodule"; getSubOptions = _: builtins.throw "suboptions unavailable"; }; unsupportedPatternType = { name = "strMatching"; description = "string matching digits"; functor.payload = "\\d+"; }; in { services.example.nested = { _type = "option"; type = nestedModule; }; services.example.knownNoType = { _type = "option"; type = knownNoTypeModule; }; services.example.requiredPolicy = { _type = "option"; type = requiredPolicyModule; }; services.example.large = { _type = "option"; type = largeModule; }; services.example.partial = { _type = "option"; type = partialModule; }; services.example.pattern = { _type = "option"; type = unsupportedPatternType; }; }' \
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
  services.example.nested = {
    settings = {
      bar = "bad";
    };
  };

  services.example.knownNoType = {
    knownNoType = true;
    trulyUnknown = true;
  };

  services.example.requiredPolicy = { };

  services.example.large = {
    opt169 = "bad";
  };

  services.example.partial = {
    mystery = true;
  };

  services.example.pattern = "abc";
}
```

```
CHECK: "message": "value for option `services.example.nested.settings.bar` has type `string`, expected `int signed integer`"
CHECK: "message": "unknown option `services.example.knownNoType.trulyUnknown`"
CHECK: "message": "required option `services.example.requiredPolicy.explicit` is missing"
CHECK-NOT: unknown option `services.example.knownNoType.knownNoType`
CHECK-NOT: unknown option `services.example.large.opt169`
CHECK-NOT: unknown option `services.example.partial.mystery`
CHECK-NOT: required option `services.example.requiredPolicy.requiredFalse`
CHECK-NOT: required option `services.example.requiredPolicy.missingRequired`
CHECK-NOT: value for option `services.example.pattern`
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
