# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ systemd.services = { _type = "option"; }; virtualisation.oci-containers = { _type = "option"; }; }' \
# RUN: < %s | FileCheck %s

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

```nix file:///completion-root-prefix-before-binding.nix
{ config, ... }:
let
  helper = true;
in
{
  virtualisation

  systemd.services.example = {};
}
```

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "textDocument/completion",
  "params": {
    "textDocument": {
      "uri": "file:///completion-root-prefix-before-binding.nix"
    },
    "position": {
      "line": 5,
      "character": 16
    },
    "context": {
      "triggerKind": 3
    }
  }
}
```

```
     CHECK: "id": 1,
CHECK:      "label": "virtualisation",
CHECK-NOT:  "label": "systemd",
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
