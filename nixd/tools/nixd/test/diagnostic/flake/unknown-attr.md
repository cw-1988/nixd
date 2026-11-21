# RUN: nixd --lit-test < %s | FileCheck \
# RUN: --implicit-check-not='attribute `description`' \
# RUN: --implicit-check-not='attribute `inputs`' \
# RUN: --implicit-check-not='attribute `outputs`' \
# RUN: --implicit-check-not='attribute `nixConfig`' \
# RUN: --implicit-check-not='attribute `dynamic`' \
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

Only `description`, `inputs`, `outputs` and `nixConfig` live at the root of a
flake. `inputs.nixpkgs.url` is a path whose root attribute is `inputs`, so it
must not be flagged; `${"dynamic"}` cannot be resolved statically, so it is
left alone too.

```nix file:///flake.nix
let
  stray = 1;
in
{
  description = "example";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs";
  nixConfig.extra-substituters = [ "https://example.cachix.org" ];
  outputs = { ... }: { };

  ouputs = { ... }: { };
  packages.x86_64-linux.default = 1;
  inherit stray;
  ${"dynamic"} = 1;
}
```

```
CHECK-DAG: "code": "flake-unknown-attr"
CHECK-DAG: "message": "attribute `ouputs` is ignored by Nix, `flake.nix` only accepts `description`, `inputs`, `outputs` and `nixConfig`"
CHECK-DAG: "message": "attribute `packages` is ignored by Nix, `flake.nix` only accepts `description`, `inputs`, `outputs` and `nixConfig`"
CHECK-DAG: "message": "attribute `stray` is ignored by Nix, `flake.nix` only accepts `description`, `inputs`, `outputs` and `nixConfig`"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
