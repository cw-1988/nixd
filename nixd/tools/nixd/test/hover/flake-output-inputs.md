# RUN: nixd --lit-test < %s | FileCheck %s

<-- initialize(0)

```json
{
  "jsonrpc": "2.0",
  "id": 0,
  "method": "initialize",
  "params": {
    "processId": 123,
    "rootPath": "",
    "capabilities": {},
    "trace": "off"
  }
}
```

<-- textDocument/didOpen

```nix file:///flake.nix
{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs";
  inputs.flake-utils.url = "github:numtide/flake-utils";

  outputs =
    { self, nixpkgs, ... }:
    {
      packages.x86_64-linux.default =
        nixpkgs.legacyPackages.x86_64-linux.hello;
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
      "line": 5,
      "character": 13
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
      "uri": "file:///flake.nix"
    },
    "position": {
      "line": 5,
      "character": 22
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
      "uri": "file:///flake.nix"
    },
    "position": {
      "line": 8,
      "character": 10
    }
  }
}
```

```
CHECK:      "id": 1,
CHECK:      "value": "## Flake Output Input\n\n`nixpkgs`\n\nProvided by: `inputs.nixpkgs`.\n\nURL: `github:NixOS/nixpkgs`.
CHECK-SAME: Nix calls `outputs` with `self` plus one attribute for each top-level flake input.
CHECK:      "id": 2,
CHECK:      "value": "## Additional Flake Output Inputs\n\n`...` keeps this `outputs` lambda open to `self` and inputs that are not listed explicitly.
CHECK-SAME: - `self` (`flake evaluator`)
CHECK-SAME: - `flake-utils` (`inputs.flake-utils`, `github:numtide/flake-utils`)
CHECK-SAME: - `nixpkgs` (`inputs.nixpkgs`, `github:NixOS/nixpkgs`)
CHECK:      "id": 3,
CHECK:      "value": "## Flake Output Input\n\n`nixpkgs`\n\nProvided by: `inputs.nixpkgs`.\n\nURL: `github:NixOS/nixpkgs`.
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
