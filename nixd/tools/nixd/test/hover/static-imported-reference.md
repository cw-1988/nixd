# RUN: mkdir -p %t.dir && printf 'let\n  ignored = 1;\nin\nrec {\n  mkTmpfileDirectories = paths: paths;\n}\n' > %t.dir/helpers.nix && sed 's|TEST_DIR|%t.dir|g' %s > %t && nixd --lit-test < %t | FileCheck %s

Test hover previews for attributes selected from a statically imported file.

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
let helpers = import ./helpers.nix; in helpers.mkTmpfileDirectories {
  paths = [ ./state ];
  mode = "0750";
}
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
         "line":0,
         "character":55
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
CHECK-NEXT:      "value": "```nix\nmkTmpfileDirectories = paths: paths;\n```"
CHECK-NEXT:    },
CHECK-NEXT:    "range": {
CHECK-NEXT:      "end": {
CHECK-NEXT:        "character": 67,
CHECK-NEXT:        "line": 0
CHECK-NEXT:      },
CHECK-NEXT:      "start": {
CHECK-NEXT:        "character": 47,
CHECK-NEXT:        "line": 0
CHECK-NEXT:      }
CHECK-NEXT:    }
CHECK-NEXT:  }
CHECK-NEXT:  }
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
