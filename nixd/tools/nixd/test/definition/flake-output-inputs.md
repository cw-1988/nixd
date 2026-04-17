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

<-- textDocument/definition(1)

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "textDocument/definition",
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

<-- textDocument/definition(2)

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "textDocument/definition",
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
CHECK-LABEL: "id": 1,
CHECK:       "result": {
CHECK:       "uri": "file://{{.*}}nixd-inspect/flake-flake-nixpkgs-{{[0-9]+}}.nix"
CHECK-LABEL: "id": 2,
CHECK:       "result": {
CHECK:       "uri": "file://{{.*}}nixd-inspect/flake-flake-nixpkgs-{{[0-9]+}}.nix"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
