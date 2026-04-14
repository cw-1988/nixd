# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.example.mode = { _type = "option"; type = { name = "enum"; description = "one of fast, 1, true, or null"; functor.payload.values = [ "fast" 1 true null ]; }; }; services.example.escaped = { _type = "option"; type = { name = "enum"; description = "escaped strings"; functor.payload.values = [ "needs \"quote\" and \\ slash and \${literal}" ]; }; }; }' \
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

```nix file:///completion-enum-string.nix
{ services.example.mode = "f"; }
```

```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-enum-string.nix"
        },
        "position": {
            "line": 0,
            "character": 28
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
CHECK:      "textEdit": {
CHECK-NEXT:   "newText": "\"fast\"",
```

<-- textDocument/didOpen

```nix file:///completion-enum-escaped-string.nix
{ services.example.escaped = "needs"; }
```

```json
{
    "jsonrpc": "2.0",
    "id": 2,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-enum-escaped-string.nix"
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
CHECK:      "id": 2,
CHECK:      "filterText": "needs \"quote\" and \\ slash and ${literal}",
CHECK:      "label": "\"needs \\\"quote\\\" and \\\\ slash and \\${literal}\"",
CHECK:      "newText": "\"needs \\\"quote\\\" and \\\\ slash and \\${literal}\"",
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
