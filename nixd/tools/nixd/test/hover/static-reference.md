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

```nix file:///basic.nix
rec {
  mkOneshotServiceConfig = {
    Type = "oneshot";
  };

  mkHardenedOneshotServiceConfig = args: mkOneshotServiceConfig // mkHardenedServiceConfig args;
  mkHardenedServiceConfig = args: { };
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
         "uri":"file:///basic.nix"
      },
      "position":{
         "line":5,
         "character":45
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
CHECK-NEXT:      "value": "```nix\nmkOneshotServiceConfig = {\n  Type = \"oneshot\";\n};\n```"
CHECK-NEXT:    },
CHECK-NEXT:    "range": {
CHECK-NEXT:      "end": {
CHECK-NEXT:        "character": 63,
CHECK-NEXT:        "line": 5
CHECK-NEXT:      },
CHECK-NEXT:      "start": {
CHECK-NEXT:        "character": 41,
CHECK-NEXT:        "line": 5
CHECK-NEXT:      }
CHECK-NEXT:    }
CHECK-NEXT:  }
CHECK-NEXT:  }
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
