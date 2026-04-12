# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.k3s.enable = { _type = "option"; type = { name = "bool"; }; }; }' \
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

```nix file:///completion.nix
{ services.k3s.enable = ; }
```

```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion.nix"
        },
        "position": {
            "line": 0,
            "character": 24
        },
        "context": {
            "triggerKind": 1
        }
    }
}
```

```json
{
    "jsonrpc": "2.0",
    "id": 2,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion.nix"
        },
        "position": {
            "line": 0,
            "character": 24
        },
        "context": {
            "triggerKind": 1
        }
    }
}
```

```
CHECK-DAG:  "id": 1,
CHECK-DAG:  "id": 2,
CHECK-DAG:  "label": "true"
CHECK-DAG:  "label": "false"
CHECK-DAG:  "label": "true"
CHECK-DAG:  "label": "false"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
