# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ nix.settings = { _type = "option"; type = { name = "submodule"; description = "open submodule"; nestedTypes.freeformType = { name = "attrsOf"; description = "attribute set of Nix config values"; nestedTypes.elemType = { name = "either"; description = "string or list of strings"; nestedTypes.left = { name = "str"; description = "string"; }; nestedTypes.right = { name = "listOf"; description = "list of string"; nestedTypes.elemType = { name = "str"; description = "string"; }; }; }; }; getSubOptions = _: { max-jobs = { _type = "option"; type = { name = "int"; description = "integer"; }; }; sandbox = { _type = "option"; type = { name = "bool"; description = "boolean"; }; }; }; }; }; }' \
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
  nix.settings.experimental-features = [
    "nix-command"
    "flakes"
  ];
}
```

```
CHECK: "id": 0
CHECK-NOT: option-unknown
CHECK-NOT: unknown option `nix.settings.experimental-features`
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
