# RUN: nixd --lit-test \
# RUN: --nixpkgs-expr='{ lib = { id = x: x; lists = { singleton = x: [x]; }; }; hello = {}; stdenv = { hostPlatform = {}; }; }' \
# RUN: --nixos-options-expr='{ _module.args = { _type = "option"; type = { name = "lazyAttrsOf"; description = "lazy attribute set of raw value"; }; value = { pkgs = {}; customTop = true; }; }; foo = { _type = "option"; type = { name = "str"; description = "string"; }; }; }' \
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

```nix file:///module-input-definition.nix
{ config, lib, pkgs, customTop, ... }:
{
  foo = config.foo;
  message = lib.id customTop;
  packageSet = pkgs;
}
```

<-- textDocument/definition(2)

```json
{
   "jsonrpc":"2.0",
   "id":2,
   "method":"textDocument/definition",
   "params":{
      "textDocument":{
         "uri":"file:///module-input-definition.nix"
      },
      "position":{
        "line": 0,
        "character":4
      }
   }
}
```

<-- textDocument/definition(3)

```json
{
   "jsonrpc":"2.0",
   "id":3,
   "method":"textDocument/definition",
   "params":{
      "textDocument":{
         "uri":"file:///module-input-definition.nix"
      },
      "position":{
        "line": 2,
        "character":8
      }
   }
}
```

<-- textDocument/definition(4)

```json
{
   "jsonrpc":"2.0",
   "id":4,
   "method":"textDocument/definition",
   "params":{
      "textDocument":{
         "uri":"file:///module-input-definition.nix"
      },
      "position":{
        "line": 0,
        "character":12
      }
   }
}
```

<-- textDocument/definition(5)

```json
{
   "jsonrpc":"2.0",
   "id":5,
   "method":"textDocument/definition",
   "params":{
      "textDocument":{
         "uri":"file:///module-input-definition.nix"
      },
      "position":{
        "line": 0,
        "character":17
      }
   }
}
```

<-- textDocument/definition(6)

```json
{
   "jsonrpc":"2.0",
   "id":6,
   "method":"textDocument/definition",
   "params":{
      "textDocument":{
         "uri":"file:///module-input-definition.nix"
      },
      "position":{
        "line": 0,
        "character":23
      }
   }
}
```

```
CHECK-LABEL: "id": 2,
CHECK:       "result": {
CHECK:       "uri": "file://{{.*}}nixd-inspect/module-input-definition-config-{{[0-9]+}}.nix"
CHECK-LABEL: "id": 3,
CHECK:       "result": {
CHECK:       "uri": "file://{{.*}}nixd-inspect/module-input-definition-config-{{[0-9]+}}.nix"
CHECK-LABEL: "id": 4,
CHECK:       "result": {
CHECK:       "uri": "file://{{.*}}nixd-inspect/module-input-definition-lib-{{[0-9]+}}.nix"
CHECK-LABEL: "id": 5,
CHECK:       "result": {
CHECK:       "uri": "file://{{.*}}nixd-inspect/module-input-definition-pkgs-{{[0-9]+}}.nix"
CHECK-LABEL: "id": 6,
CHECK:       "result": {
CHECK:       "uri": "file://{{.*}}nixd-inspect/module-input-definition-customTop-{{[0-9]+}}.nix"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
