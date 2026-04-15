# RUN: nixd --lit-test < %s | FileCheck %s

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

```nix file:///flake.nix
{ des }
```

```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///flake.nix"
        },
        "position": {
            "line": 0,
            "character": 5
        },
        "context": {
            "triggerKind": 1
        }
    }
}
```

```
CHECK: "id": 1,
CHECK: "detail": "flake | str (string)"
CHECK: "label": "description"
```

<-- textDocument/didOpen

```nix file:///outputs/flake.nix
{
  outputs = _: {
    nixo
  };
}
```

```json
{
    "jsonrpc": "2.0",
    "id": 2,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///outputs/flake.nix"
        },
        "position": {
            "line": 2,
            "character": 8
        },
        "context": {
            "triggerKind": 1
        }
    }
}
```

```
CHECK: "id": 2,
CHECK: "label": "nixosConfigurations"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
