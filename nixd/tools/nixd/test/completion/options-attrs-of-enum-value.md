# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.example.modes = { _type = "option"; type = { name = "attrsOf"; description = "attribute set of enum"; nestedTypes.elemType = { name = "enum"; description = "one of fast or slow"; functor.payload.values = [ "fast" "slow" ]; }; }; }; }' \
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

```nix file:///completion-attrs-enum.nix
{ services.example.modes.default = ; }
```

```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-attrs-enum.nix"
        },
        "position": {
            "line": 0,
            "character": 35
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
CHECK:      "label": "\"fast\"",
CHECK:      "detail": "enum option value",
CHECK:      "filterText": "slow",
CHECK:      "label": "\"slow\"",
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
