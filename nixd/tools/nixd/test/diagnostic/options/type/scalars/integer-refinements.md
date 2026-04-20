# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.example.positiveGood = { _type = "option"; type = { name = "positiveInt"; description = "positive integer, meaning >0"; }; }; services.example.positiveBad = { _type = "option"; type = { name = "positiveInt"; description = "positive integer, meaning >0"; }; }; services.example.unsignedGood = { _type = "option"; type = { name = "unsignedInt"; description = "unsigned integer, meaning >=0"; }; }; services.example.unsignedBad = { _type = "option"; type = { name = "unsignedInt"; description = "unsigned integer, meaning >=0"; }; }; services.example.portGood = { _type = "option"; type = { name = "port"; description = "16 bit unsigned integer; between 0 and 65535 (both inclusive)"; }; }; services.example.portBad = { _type = "option"; type = { name = "port"; description = "16 bit unsigned integer; between 0 and 65535 (both inclusive)"; }; }; services.example.u16Good = { _type = "option"; type = { name = "unsignedInt16"; description = "16 bit unsigned integer; between 0 and 65535 (both inclusive)"; }; }; services.example.u16Bad = { _type = "option"; type = { name = "unsignedInt16"; description = "16 bit unsigned integer; between 0 and 65535 (both inclusive)"; }; }; services.example.betweenGood = { _type = "option"; type = { name = "intBetween"; description = "integer between 0 and 10 (both inclusive)"; }; }; services.example.betweenBad = { _type = "option"; type = { name = "intBetween"; description = "integer between 0 and 10 (both inclusive)"; }; }; services.example.nullUnsignedGood = { _type = "option"; type = { name = "nullOr"; description = "null or 16 bit unsigned integer; between 0 and 65535 (both inclusive)"; }; }; services.example.nullUnsignedBad = { _type = "option"; type = { name = "nullOr"; description = "null or 16 bit unsigned integer; between 0 and 65535 (both inclusive)"; }; }; services.example.computedTimeout = { _type = "option"; type = { name = "positiveInt"; description = "positive integer, meaning >0"; }; }; services.example.calculatedTimeout = { _type = "option"; type = { name = "positiveInt"; description = "positive integer, meaning >0"; }; }; }' \
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
{ cfg, ... }:
{
  services.example.positiveGood = 1;
  services.example.positiveBad = -1;
  services.example.unsignedGood = 0;
  services.example.unsignedBad = -1;
  services.example.portGood = 65535;
  services.example.portBad = 65536;
  services.example.u16Good = 65535;
  services.example.u16Bad = 65536;
  services.example.betweenGood = 10;
  services.example.betweenBad = 11;
  services.example.nullUnsignedGood = null;
  services.example.nullUnsignedBad = -1;
  services.example.computedTimeout = cfg.timeout;
  services.example.calculatedTimeout = 1 + 2;
}
```

```
CHECK-NOT: value for option `services.example.positiveGood`
CHECK: "message": "value for option `services.example.positiveBad` has type `integer`, expected `positiveInt positive integer, meaning >0`"
CHECK-NOT: value for option `services.example.unsignedGood`
CHECK: "message": "value for option `services.example.unsignedBad` has type `integer`, expected `unsignedInt unsigned integer, meaning >=0`"
CHECK-NOT: value for option `services.example.portGood`
CHECK: "message": "value for option `services.example.portBad` has type `integer`, expected `port 16 bit unsigned integer; between 0 and 65535 (both inclusive)`"
CHECK-NOT: value for option `services.example.u16Good`
CHECK: "message": "value for option `services.example.u16Bad` has type `integer`, expected `unsignedInt16 16 bit unsigned integer; between 0 and 65535 (both inclusive)`"
CHECK-NOT: value for option `services.example.betweenGood`
CHECK: "message": "value for option `services.example.betweenBad` has type `integer`, expected `intBetween integer between 0 and 10 (both inclusive)`"
CHECK-NOT: value for option `services.example.nullUnsignedGood`
CHECK: "message": "value for option `services.example.nullUnsignedBad` has type `integer`, expected `nullOr null or 16 bit unsigned integer; between 0 and 65535 (both inclusive)`"
CHECK-NOT: value for option `services.example.computedTimeout`
CHECK-NOT: value for option `services.example.calculatedTimeout`
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
