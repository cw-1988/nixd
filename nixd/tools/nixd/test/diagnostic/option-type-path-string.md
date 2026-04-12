# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.k3s.agentTokenFile = { _type = "option"; type = { name = "nullOr"; description = "null or absolute path"; }; }; services.k3s.badTokenFile = { _type = "option"; type = { name = "nullOr"; description = "null or absolute path"; }; }; }' \
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
  services.k3s.agentTokenFile = "/etc";
  services.k3s.badTokenFile = "etc";
}
```

```
CHECK-NOT: value for option `services.k3s.agentTokenFile`
     CHECK: "code": "option-value-type"
CHECK: "message": "value for option `services.k3s.badTokenFile` has type `string`, expected `nullOr null or absolute path`"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
