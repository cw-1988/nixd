# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.example.mode = { _type = "option"; type = { name = "enum"; description = "one of fast, 1, true, or null"; functor.payload.values = [ "fast" 1 true null ]; }; }; }' \
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

```nix file:///completion-enum.nix
{ services.example.mode = ; }
```

```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-enum.nix"
        },
        "position": {
            "line": 0,
            "character": 26
        },
        "context": {
            "triggerKind": 1
        }
    }
}
```

```
CHECK:      "id": 1,
CHECK:      "detail": "enum option value",
CHECK:      "filterText": "fast",
CHECK:      "kind": 20,
CHECK:      "label": "\"fast\"",
CHECK:      "detail": "enum option value",
CHECK:      "filterText": "1",
CHECK:      "kind": 20,
CHECK:      "label": "1",
CHECK:      "detail": "enum option value",
CHECK:      "filterText": "true",
CHECK:      "kind": 20,
CHECK:      "label": "true",
CHECK:      "detail": "enum option value",
CHECK:      "filterText": "null",
CHECK:      "kind": 20,
CHECK:      "label": "null",
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
