# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let boolType = { name = "bool"; description = "boolean"; }; enumType = { name = "enum"; description = "one of fast or slow"; functor.payload.values = [ "fast" "slow" ]; }; in { services.example.module.settings.mode = { _type = "option"; type = enumType; }; services.example.module.nested.flag = { _type = "option"; type = boolType; }; services.example.fnElem = { _type = "option"; type = { name = "functionTo"; description = "function that evaluates to boolean"; nestedTypes.elemType = boolType; }; }; }' \
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

```nix file:///completion-deep-navigation.nix
{
  services.example.module.settings.mode = f;
  services.example.module = { nested.flag = t; };
  services.example.fnElem = x: t;
}
```

<-- textDocument/hover(99)

```json
{
  "jsonrpc": "2.0",
  "id": 99,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///completion-deep-navigation.nix"
    },
    "position": {
      "line": 1,
      "character": 20
    }
  }
}
```

```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-deep-navigation.nix"
        },
        "position": {
            "line": 1,
            "character": 43
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
            "uri": "file:///completion-deep-navigation.nix"
        },
        "position": {
            "line": 2,
            "character": 45
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
            "uri": "file:///completion-deep-navigation.nix"
        },
        "position": {
            "line": 3,
            "character": 32
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
CHECK:      "detail": "boolean option value",
CHECK:      "label": "true",
CHECK-NOT:  "label": "false",
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
