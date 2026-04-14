# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.example.zeroGood = { _type = "option"; type = { name = "enum"; description = "zero"; functor.payload.values = [ 0 ]; }; }; services.example.zeroBad = { _type = "option"; type = { name = "enum"; description = "zero"; functor.payload.values = [ 0 ]; }; }; services.example.falseGood = { _type = "option"; type = { name = "enum"; description = "false"; functor.payload.values = [ false ]; }; }; services.example.falseBad = { _type = "option"; type = { name = "enum"; description = "false"; functor.payload.values = [ false ]; }; }; }' \
# RUN: < %s | FileCheck \
# RUN: --implicit-check-not='value for option `services.example.zeroGood`' \
# RUN: --implicit-check-not='value for option `services.example.falseGood`' \
# RUN: %s

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

```nix file:///enum-false-zero.nix
{
  services.example.zeroGood = 0;
  services.example.zeroBad = 1;
  services.example.falseGood = false;
  services.example.falseBad = true;
}
```

```
CHECK-DAG: "message": "value for option `services.example.zeroBad` has type `integer`, expected `enum zero`"
CHECK-DAG: "message": "value for option `services.example.falseBad` has type `boolean`, expected `enum false`"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
