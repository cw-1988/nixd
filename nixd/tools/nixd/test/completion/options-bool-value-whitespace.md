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
{
  services.k3s.enable =
    ;
}
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
CHECK-NEXT:  "jsonrpc": "2.0",
CHECK-NEXT:  "result": {
CHECK-NEXT:    "isIncomplete": false,
CHECK-NEXT:    "items": [
CHECK-NEXT:      {
CHECK-NEXT:        "data": "",
CHECK-NEXT:        "detail": "boolean option value",
CHECK-NEXT:        "kind": 14,
CHECK-NEXT:        "label": "true",
CHECK-NEXT:        "score": 0
CHECK-NEXT:      },
CHECK-NEXT:      {
CHECK-NEXT:        "data": "",
CHECK-NEXT:        "detail": "boolean option value",
CHECK-NEXT:        "kind": 14,
CHECK-NEXT:        "label": "false",
CHECK-NEXT:        "score": 0
CHECK-NEXT:      }
CHECK-NEXT:    ]
CHECK-NEXT:  }
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
