# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.k3s.enable = { _type = "option"; type = { name = "bool"; description = "boolean"; }; }; }' \
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

```nix file:///completion-helper-call-attrset.nix
let
  mkHelper = args: args;
in {
  baseModule = mkHelper {
    
  };
}
```

```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-helper-call-attrset.nix"
        },
        "position": {
            "line": 4,
            "character": 4
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
CHECK-NEXT:    "items": []
CHECK-NEXT:  }
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
