# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ }' \
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
{ services.k3s.enable = ""; }
```

```
CHECK: "id": 0
CHECK-NOT: option-value-type
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
