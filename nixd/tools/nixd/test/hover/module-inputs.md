# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let intType = { name = "int"; description = "signed integer"; }; strType = { name = "str"; description = "string"; }; argType = { name = "lazyAttrsOf"; description = "lazy attribute set of raw value"; }; moduleType = { name = "submodule"; description = "submodule"; getSubOptions = _: { _module.args = { _type = "option"; type = argType; value = { name = "api"; subArg = true; }; }; known = { _type = "option"; type = intType; }; required = { _type = "option"; type = strType; required = true; }; settings = { _type = "option"; type = { name = "attrsOf"; description = "attribute set of signed integer"; nestedTypes.elemType = intType; }; }; }; }; in { _module.args = { _type = "option"; type = argType; value = { customTop = true; pkgs = {}; }; }; _module.specialArgs = { _type = "option"; type = argType; value = { modulesPath = "/nix/modules"; }; }; services.example.moduleAsLambda = { _type = "option"; type = moduleType; }; }' \
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

```nix file:///module-inputs.nix
{
  config,
  lib,
  pkgs,
  ...
}:
{
  services.example.moduleAsLambda =
    { config, ... }:
    {
      known = config.settings.retries + 1;
      settings.retries = 6;
    };
}
```

<-- textDocument/hover(1)

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///module-inputs.nix"
    },
    "position": {
      "line": 1,
      "character": 4
    }
  }
}
```

<-- textDocument/hover(2)

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///module-inputs.nix"
    },
    "position": {
      "line": 4,
      "character": 4
    }
  }
}
```

<-- textDocument/hover(3)

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///module-inputs.nix"
    },
    "position": {
      "line": 8,
      "character": 8
    }
  }
}
```

<-- textDocument/hover(4)

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///module-inputs.nix"
    },
    "position": {
      "line": 8,
      "character": 15
    }
  }
}
```

<-- textDocument/hover(5)

```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///module-inputs.nix"
    },
    "position": {
      "line": 2,
      "character": 3
    }
  }
}
```

<-- textDocument/hover(6)

```json
{
  "jsonrpc": "2.0",
  "id": 6,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///module-inputs.nix"
    },
    "position": {
      "line": 3,
      "character": 3
    }
  }
}
```

```
CHECK:      "id": 1,
CHECK:      "value": "## Module Input\n\n`config`\n\nProvided by: `module system`."
CHECK:      "id": 2,
CHECK:      "value": "## Additional Module Inputs\n\n`...` keeps this lambda open to module arguments that are not listed explicitly.
CHECK-SAME: - `customTop` (`_module.args`)
CHECK-SAME: - `modulesPath` (`specialArgs`)
CHECK-SAME: - `pkgs` (`_module.args`)
CHECK:      "id": 3,
CHECK:      "value": "## Module Input\n\n`config`\n\nProvided by: `module system`.\n\n**Type** `submodule` - submodule"
CHECK:      "id": 4,
CHECK:      "value": "## Additional Module Inputs\n\n`...` keeps this lambda open to module arguments that are not listed explicitly.
CHECK-SAME: - `name` (`_module.args`)
CHECK-SAME: - `subArg` (`_module.args`)
CHECK-SAME: `config` and `options` are scoped to this submodule.
CHECK:      "id": 5,
CHECK:      "value": "## Module Input\n\n`lib`\n\nProvided by: `module system`."
CHECK:      "id": 6,
CHECK:      "value": "## Module Input\n\n`pkgs`\n\nProvided by: `_module.args`."
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
