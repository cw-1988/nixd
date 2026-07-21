# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='(builtins.listToAttrs (builtins.genList (i: { name = "namespace${toString i}"; value.child = {}; }) 30)) // { virtualisation.oci-containers = { _type = "option"; }; }' \
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

```nix file:///completion-root-empty-namespaces.nix
{ config, ... }:
{
  
}
```

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "textDocument/completion",
  "params": {
    "textDocument": {
      "uri": "file:///completion-root-empty-namespaces.nix"
    },
    "position": {
      "line": 2,
      "character": 2
    },
    "context": {
      "triggerKind": 1
    }
  }
}
```

```
     CHECK: "id": 1,
CHECK:      "label": "namespace29",
CHECK:      "label": "virtualisation",
```

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "textDocument/completion",
  "params": {
    "textDocument": {
      "uri": "file:///completion-root-empty-namespaces.nix"
    },
    "position": {
      "line": 2,
      "character": 2
    },
    "context": {
      "triggerKind": 1
    }
  }
}
```

```
     CHECK: "id": 2,
CHECK:      "label": "namespace29",
CHECK:      "label": "virtualisation",
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
