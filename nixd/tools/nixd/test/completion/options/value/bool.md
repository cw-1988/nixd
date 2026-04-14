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

<-- textDocument/didOpen

```nix file:///completion-string.nix
{ services.k3s.enable = ""; }
```

```json
{
    "jsonrpc": "2.0",
    "id": 2,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-string.nix"
        },
        "position": {
            "line": 0,
            "character": 25
        },
        "context": {
            "triggerKind": 1
        }
    }
}
```

```
     CHECK: "id": 2,
CHECK-NEXT:  "jsonrpc": "2.0",
CHECK-NEXT:  "result": {
CHECK-NEXT:    "isIncomplete": false,
CHECK-NEXT:    "items": [
CHECK-NEXT:      {
CHECK-NEXT:        "data": "",
CHECK-NEXT:        "detail": "boolean option value",
CHECK-NEXT:        "kind": 14,
CHECK-NEXT:        "label": "true",
CHECK-NEXT:        "score": 0,
CHECK-NEXT:        "textEdit": {
CHECK-NEXT:          "newText": "true",
CHECK-NEXT:          "range": {
CHECK-NEXT:            "end": {
CHECK-NEXT:              "character": 26,
CHECK-NEXT:              "line": 0
CHECK-NEXT:            },
CHECK-NEXT:            "start": {
CHECK-NEXT:              "character": 24,
CHECK-NEXT:              "line": 0
CHECK-NEXT:            }
CHECK-NEXT:          }
CHECK-NEXT:        }
CHECK-NEXT:      },
CHECK-NEXT:      {
CHECK-NEXT:        "data": "",
CHECK-NEXT:        "detail": "boolean option value",
CHECK-NEXT:        "kind": 14,
CHECK-NEXT:        "label": "false",
CHECK-NEXT:        "score": 0,
CHECK-NEXT:        "textEdit": {
CHECK-NEXT:          "newText": "false",
CHECK-NEXT:          "range": {
CHECK-NEXT:            "end": {
CHECK-NEXT:              "character": 26,
CHECK-NEXT:              "line": 0
CHECK-NEXT:            },
CHECK-NEXT:            "start": {
CHECK-NEXT:              "character": 24,
CHECK-NEXT:              "line": 0
CHECK-NEXT:            }
CHECK-NEXT:          }
CHECK-NEXT:        }
CHECK-NEXT:      }
CHECK-NEXT:    ]
CHECK-NEXT:  }
```

<-- textDocument/didOpen

```nix file:///completion-string-prefix.nix
{ services.k3s.enable = "f"; }
```

```json
{
    "jsonrpc": "2.0",
    "id": 3,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-string-prefix.nix"
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
     CHECK: "id": 3,
CHECK-NEXT:  "jsonrpc": "2.0",
CHECK-NEXT:  "result": {
CHECK-NEXT:    "isIncomplete": false,
CHECK-NEXT:    "items": [
CHECK-NEXT:      {
CHECK-NEXT:        "data": "",
CHECK-NEXT:        "detail": "boolean option value",
CHECK-NEXT:        "kind": 14,
CHECK-NEXT:        "label": "false",
CHECK-NEXT:        "score": 0,
CHECK-NEXT:        "textEdit": {
CHECK-NEXT:          "newText": "false",
CHECK-NEXT:          "range": {
CHECK-NEXT:            "end": {
CHECK-NEXT:              "character": 27,
CHECK-NEXT:              "line": 0
CHECK-NEXT:            },
CHECK-NEXT:            "start": {
CHECK-NEXT:              "character": 24,
CHECK-NEXT:              "line": 0
CHECK-NEXT:            }
CHECK-NEXT:          }
CHECK-NEXT:        }
CHECK-NEXT:      }
CHECK-NEXT:    ]
CHECK-NEXT:  }
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
