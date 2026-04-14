# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let boolType = { name = "bool"; description = "boolean"; }; enumType = { name = "enum"; description = "one of fast or slow"; functor.payload.values = [ "fast" "slow" ]; }; in { services.example.flags = { _type = "option"; type = { name = "listOf"; description = "list of boolean"; nestedTypes.elemType = boolType; }; }; services.example.modes = { _type = "option"; type = { name = "listOf"; description = "list of null or one of fast or slow"; nestedTypes.elemType = { name = "nullOr"; description = "null or one of fast or slow"; nestedTypes.elemType = enumType; }; }; }; }' \
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

```nix file:///completion-list-values.nix
{
  services.example.flags = [ t ];
  services.example.modes = [ n ];
}
```

```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-list-values.nix"
        },
        "position": {
            "line": 1,
            "character": 29
        },
        "context": {
            "triggerKind": 1
        }
    }
}
```

```
CHECK:      "id": 1,
CHECK-NOT:  "id": 2,
CHECK:      "detail": "boolean option value",
CHECK:      "label": "true",
CHECK-NOT:  "label": "false",
```

```json
{
    "jsonrpc": "2.0",
    "id": 2,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-list-values.nix"
        },
        "position": {
            "line": 2,
            "character": 29
        },
        "context": {
            "triggerKind": 1
        }
    }
}
```

```
CHECK:      "id": 2,
CHECK-NOT:  "method": "exit",
CHECK:      "detail": "null option value",
CHECK:      "label": "null",
CHECK-NOT:  "label": "\"fast\"",
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
