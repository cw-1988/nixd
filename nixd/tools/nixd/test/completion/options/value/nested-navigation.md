# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let boolType = { name = "bool"; description = "boolean"; }; enumType = { name = "enum"; description = "one of fast or slow"; functor.payload.values = [ "fast" "slow" ]; }; moduleType = { name = "submodule"; description = "submodule"; getSubOptions = _: { mode = { _type = "option"; type = enumType; }; }; }; in { services.example.module = { _type = "option"; type = moduleType; }; services.example.fn = { _type = "option"; type = { name = "functionTo"; description = "function that evaluates to boolean"; nestedTypes.resultType = boolType; }; }; services.example.loa = { _type = "option"; type = { name = "loaOf"; description = "list of attribute sets of enum"; nestedTypes.elemType = enumType; }; }; }' \
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

```nix file:///completion-nested-navigation.nix
{
  services.example.module = { mode = f; };
  services.example.fn = x: t;
  services.example.loa = [ { mode = f; } ];
}
```

```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-nested-navigation.nix"
        },
        "position": {
            "line": 1,
            "character": 37
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
CHECK:      "detail": "enum option value",
CHECK:      "filterText": "fast",
CHECK:      "label": "\"fast\"",
CHECK-NOT:  "filterText": "slow",
```

```json
{
    "jsonrpc": "2.0",
    "id": 2,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-nested-navigation.nix"
        },
        "position": {
            "line": 2,
            "character": 27
        },
        "context": {
            "triggerKind": 1
        }
    }
}
```

```
CHECK:      "id": 2,
CHECK-NOT:  "id": 3,
CHECK:      "detail": "boolean option value",
CHECK:      "label": "true",
CHECK-NOT:  "label": "false",
```

```json
{
    "jsonrpc": "2.0",
    "id": 3,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-nested-navigation.nix"
        },
        "position": {
            "line": 3,
            "character": 36
        },
        "context": {
            "triggerKind": 1
        }
    }
}
```

```
CHECK:      "id": 3,
CHECK-NOT:  "method": "exit",
CHECK:      "detail": "enum option value",
CHECK:      "filterText": "fast",
CHECK:      "label": "\"fast\"",
CHECK-NOT:  "filterText": "slow",
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
