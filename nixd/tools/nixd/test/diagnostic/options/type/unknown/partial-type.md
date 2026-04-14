# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.example.port = { _type = "option"; type = { name = "positiveInt"; }; }; services.example.items = { _type = "option"; type = { name = "listOf"; description = "list of signed integer"; }; }; }' \
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
{
  services.example.port = "";
  services.example.items = "";
}
```

```
CHECK: "message": "value for option `services.example.port` has type `string`, expected `positiveInt`"
CHECK: "message": "value for option `services.example.items` has type `string`, expected `listOf list of signed integer`"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
