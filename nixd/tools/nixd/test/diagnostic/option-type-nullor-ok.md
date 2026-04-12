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
{
  users.users.alice.uid = null;
  users.users.bob.uid = 1000;
}
```

```
CHECK: "id": 0
CHECK-NOT: option-value-type
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
