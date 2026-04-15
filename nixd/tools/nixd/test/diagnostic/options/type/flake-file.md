# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.example.enable = { _type = "option"; type = { name = "bool"; description = "boolean"; }; }; }' \
# RUN: < %s | FileCheck \
# RUN: --implicit-check-not='unknown option `description`' \
# RUN: --implicit-check-not='unknown option `inputs`' \
# RUN: --implicit-check-not='unknown option `outputs`' \
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

```nix file:///flake.nix
{
  description = "not a NixOS module";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { nixpkgs, ... }:
    {
      nixosConfigurations.demo = nixpkgs.lib.nixosSystem {
        system = "x86_64-linux";
        modules = [ ./configuration.nix ];
      };
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
      "uri": "file:///flake.nix"
    },
    "position": {
      "line": 1,
      "character": 4
    }
  }
}
```

```
CHECK: "diagnostics": []
CHECK: "value": "## Type\n\n`str` - string\n\n## Description\n\nA short human-readable description of the flake."
```

<-- textDocument/hover(2)

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///flake.nix"
    },
    "position": {
      "line": 3,
      "character": 18
    }
  }
}
```

```
CHECK: "value": "## Type\n\n`str` - string"
```

<-- textDocument/didOpen

```nix file:///bad/flake.nix
{
  description = 42;
  inputs.nixpkgs.url = "github:NixOS/nixpkgs";
  inputs.nixpkgs.flake = "yes";
  output = _: {};
}
```

```
CHECK: "code": "option-value-type"
CHECK: "message": "value for option `description` has type `integer`, expected `str string`"
CHECK: "code": "option-value-type"
CHECK: "message": "value for option `inputs.nixpkgs.flake` has type `string`, expected `bool boolean`"
CHECK: "code": "option-unknown"
CHECK: "message": "unknown option `output`"
```

<-- textDocument/didOpen

```nix file:///outputs/flake.nix
{
  outputs = _: {
    apps.x86_64-linux.demo = {
      type = 42;
      program = 42;
    };
  };
}
```

```
CHECK: "message": "value for option `outputs.apps.x86_64-linux.demo.type` has type `integer`, expected `str string`"
CHECK: "message": "value for option `outputs.apps.x86_64-linux.demo.program` has type `integer`, expected `str string`"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
