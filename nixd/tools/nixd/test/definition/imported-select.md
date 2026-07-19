# RUN: mkdir -p %t.dir && printf 'let\n  ignored = 1;\nin\n{\n  mkImportedHelper = value: value;\n}\n' > %t.dir/helpers.nix && sed 's|TEST_DIR|%t.dir|g' %s > %t && nixd --lit-test < %t | FileCheck %s

Test go-to-definition for attributes selected from a statically imported file.

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
let helpers = import ./helpers.nix; in helpers.mkImportedHelper
```

<-- textDocument/definition(2)


```json
{
   "jsonrpc":"2.0",
   "id":2,
   "method":"textDocument/definition",
   "params":{
      "textDocument":{
         "uri":"file://TEST_DIR/main.nix"
      },
      "position":{
        "line": 0,
        "character": 50
      }
   }
}
```

```
     CHECK:  "id": 2,
CHECK-NEXT:  "jsonrpc": "2.0",
CHECK-NEXT:  "result": {
CHECK-NEXT:    "range": {
CHECK-NEXT:      "end": {
CHECK-NEXT:        "character": 18,
CHECK-NEXT:        "line": 4
CHECK-NEXT:      },
CHECK-NEXT:      "start": {
CHECK-NEXT:        "character": 2,
CHECK-NEXT:        "line": 4
CHECK-NEXT:      }
CHECK-NEXT:    },
CHECK:         "uri": "file://
CHECK:         helpers.nix"
```


```json
{"jsonrpc":"2.0","method":"exit"}
```
