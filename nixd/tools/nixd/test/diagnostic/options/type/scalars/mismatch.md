# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ users.users.alice.uid = { _type = "option"; type = { name = "nullOr"; description = "null or signed integer"; }; }; }' \
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

```nix file:///basic.nix
{ ... }:
{
  users.users.alice.uid = "";
}
```

<-- textDocument/didChange

```json
{
  "jsonrpc": "2.0",
  "method": "textDocument/didChange",
  "params": {
    "textDocument": {
      "uri": "file:///basic.nix",
      "version": 2
    },
    "contentChanges": [
      {
        "text": "{ ... }:\n{\n  users.users.alice.uid = \"\";\n}\n"
      }
    ]
  }
}
```

<-- textDocument/hover(1)

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///basic.nix"
    },
    "position": {
      "line": 2,
      "character": 21
    }
  }
}
```

```
     CHECK: "code": "option-value-type"
CHECK: "message": "value for option `users.users.alice.uid` has type `string`, expected `nullOr null or signed integer`"
CHECK: "source": "nixd"
```

<-- nixd/waitForOptionsSettled(999)

```json
{
  "jsonrpc": "2.0",
  "id": 999,
  "method": "nixd/waitForOptionsSettled",
  "params": null
}
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
