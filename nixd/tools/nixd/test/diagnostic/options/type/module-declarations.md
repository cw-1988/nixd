# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.example.enable = { _type = "option"; type = { name = "bool"; description = "boolean"; }; }; }' \
# RUN: < %s | FileCheck \
# RUN: --implicit-check-not='unknown option `options' \
# RUN: --implicit-check-not='unknown option `type' \
# RUN: --implicit-check-not='unknown option `description' \
# RUN: --implicit-check-not='value for option `options' \
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

```nix file:///module-declarations.nix
{ lib, ... }:

let
  inherit (lib) mkOption types;

  nestedModule = types.submodule {
    options.inner = mkOption {
      type = types.str;
      default = "";
      description = "inner declaration";
    };
  };
in
{
  options.services.example = {
    enable = lib.mkEnableOption "example";

    nested = mkOption {
      type = nestedModule;
      default = { };
      description = "nested declaration";
    };
  };

  config.services.example.enable = "bad";
}
```

<-- textDocument/didChange

```json
{
  "jsonrpc": "2.0",
  "method": "textDocument/didChange",
  "params": {
    "textDocument": {
      "uri": "file:///module-declarations.nix",
      "version": 2
    },
    "contentChanges": [
      {
        "text": "{ lib, ... }:\n\nlet\n  inherit (lib) mkOption types;\n\n  nestedModule = types.submodule {\n    options.inner = mkOption {\n      type = types.str;\n      default = \"\";\n      description = \"inner declaration\";\n    };\n  };\nin\n{\n  options.services.example = {\n    enable = lib.mkEnableOption \"example\";\n\n    nested = mkOption {\n      type = nestedModule;\n      default = { };\n      description = \"nested declaration\";\n    };\n  };\n\n  config.services.example.enable = \"bad\";\n}\n"
      }
    ]
  }
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
      "uri": "file:///module-declarations.nix"
    },
    "position": {
      "line": 24,
      "character": 25
    }
  }
}
```

```
CHECK: "message": "value for option `services.example.enable` has type `string`, expected `bool boolean`"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
