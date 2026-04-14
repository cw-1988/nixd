# RUN: nixd --lit-test \
# RUN: --nixpkgs-expr='{ curl = 1; cowsay = 2; hello = 3; }' \
# RUN: --nixos-options-expr='let strType = { name = "str"; description = "string"; }; moduleType = { name = "submodule"; description = "submodule"; getSubOptions = _: { required = { _type = "option"; type = strType; required = true; }; optional = { _type = "option"; type = strType; default = ""; }; }; }; in { services.example.pkg = { _type = "option"; type = { name = "package"; description = "package"; }; }; services.example.relPath = { _type = "option"; type = { name = "path"; description = "path"; }; }; services.example.absPath = { _type = "option"; type = { name = "path"; description = "absolute path"; }; }; services.example.storePath = { _type = "option"; type = { name = "pathInStore"; description = "store path"; functor.payload = {}; }; }; services.example.module = { _type = "option"; type = moduleType; }; }' \
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
        "textDocument": {
          "completion": {
            "completionItem": {
              "snippetSupport": true
            }
          }
        }
      },
      "trace":"off"
   }
}
```

<-- textDocument/didOpen

```nix file:///completion-package-path-snippet.nix
{
  services.example.pkg = cu;
  services.example.relPath = ;
  services.example.absPath = ;
  services.example.storePath = ;
  services.example.module = ;
}
```

```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-package-path-snippet.nix"
        },
        "position": {
            "line": 1,
            "character": 25
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
CHECK:      "detail": "package option value",
CHECK:      "filterText": "curl",
CHECK:      "label": "pkgs.curl",
CHECK-NOT:  "label": "pkgs.cowsay",
```

```json
{
    "jsonrpc": "2.0",
    "id": 2,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-package-path-snippet.nix"
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
CHECK-NOT:  "id": 3,
CHECK:      "detail": "path option value",
CHECK:      "label": "./",
CHECK-NOT:  "label": "/",
```

```json
{
    "jsonrpc": "2.0",
    "id": 3,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-package-path-snippet.nix"
        },
        "position": {
            "line": 3,
            "character": 29
        },
        "context": {
            "triggerKind": 1
        }
    }
}
```

```
CHECK:      "id": 3,
CHECK-NOT:  "id": 4,
CHECK:      "detail": "path option value",
CHECK:      "label": "/",
CHECK-NOT:  "label": "./",
CHECK-NOT:  "label": "/nix/store/",
```

```json
{
    "jsonrpc": "2.0",
    "id": 4,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-package-path-snippet.nix"
        },
        "position": {
            "line": 4,
            "character": 31
        },
        "context": {
            "triggerKind": 1
        }
    }
}
```

```
CHECK:      "id": 4,
CHECK-NOT:  "id": 5,
CHECK:      "detail": "path option value",
CHECK:      "label": "/nix/store/",
```

```json
{
    "jsonrpc": "2.0",
    "id": 5,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-package-path-snippet.nix"
        },
        "position": {
            "line": 5,
            "character": 28
        },
        "context": {
            "triggerKind": 1
        }
    }
}
```

```
CHECK:      "id": 5,
CHECK-NOT:  "method": "exit",
CHECK:      "detail": "option value snippet",
CHECK:      "insertTextFormat": 2,
CHECK:      "label": "required fields",
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
