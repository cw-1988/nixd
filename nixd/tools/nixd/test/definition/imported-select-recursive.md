# RUN: mkdir -p %t.dir && printf '{\n  mkNestedHelper = value: value;\n}\n' > %t.dir/nested.nix && printf '{\n  nested = import ./nested.nix;\n}\n' > %t.dir/helpers.nix && sed 's|TEST_DIR|%t.dir|g' %s > %t && nixd --lit-test < %t | FileCheck %s

Test go-to-definition for attributes selected through nested static imports.

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
let helpers = import ./helpers.nix; in helpers.nested.mkNestedHelper
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
        "character": 60
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
CHECK-NEXT:        "character": 16,
CHECK-NEXT:        "line": 1
CHECK-NEXT:      },
CHECK-NEXT:      "start": {
CHECK-NEXT:        "character": 2,
CHECK-NEXT:        "line": 1
CHECK-NEXT:      }
CHECK-NEXT:    },
CHECK:         "uri": "file://
CHECK:         nested.nix"
```


```json
{"jsonrpc":"2.0","method":"exit"}
```
