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
{ services.k3s.enab }
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
            "character": 19
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
CHECK-NEXT:        "detail": "nixos | bool ()",
CHECK-NEXT:        "documentation": null,
CHECK-NEXT:        "insertText": "enable = true;",
CHECK-NEXT:        "insertTextFormat": 1,
CHECK-NEXT:        "kind": 4,
CHECK-NEXT:        "label": "enable",
CHECK-NEXT:        "score": 0
CHECK-NEXT:      }
CHECK-NEXT:    ]
CHECK-NEXT:  }
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
