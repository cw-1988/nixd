# RUN: mkdir -p %t.dir && printf '{ config, ... }:\n\nlet\n  secretFiles = {\n    env = "/run/authentik/env";\n  };\n  requiredFiles = [\n    secretFiles.env\n    config.path\n  ];\nin\n{\n  inherit requiredFiles;\n}\n' > %t.dir/helpers.nix && sed 's|TEST_DIR|%t.dir|g' %s > %t && nixd --lit-test < %t | FileCheck %s

Test hover previews for attributes exported by inherit from an imported function.

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

```nix file://TEST_DIR/main.nix
{ config, ... }:
let
  authentik = import ./helpers.nix { inherit config; };
in
authentik.requiredFiles
```

<-- textDocument/hover(1)


```json
{
   "jsonrpc":"2.0",
   "id":1,
   "method":"textDocument/hover",
   "params":{
      "textDocument":{
         "uri":"file://TEST_DIR/main.nix"
      },
      "position":{
         "line":4,
         "character":15
      }
   }
}
```

```
     CHECK:  "id": 1,
CHECK-NEXT:  "jsonrpc": "2.0",
CHECK-NEXT:  "result": {
CHECK-NEXT:    "contents": {
CHECK-NEXT:      "kind": "markdown",
CHECK-NEXT:      "value": "```nix\nrequiredFiles = [\n  secretFiles.env\n  config.path\n];\n```"
CHECK-NEXT:    },
CHECK-NEXT:    "range": {
CHECK-NEXT:      "end": {
CHECK-NEXT:        "character": 23,
CHECK-NEXT:        "line": 4
CHECK-NEXT:      },
CHECK-NEXT:      "start": {
CHECK-NEXT:        "character": 10,
CHECK-NEXT:        "line": 4
CHECK-NEXT:      }
CHECK-NEXT:    }
CHECK-NEXT:  }
CHECK-NEXT:  }
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
