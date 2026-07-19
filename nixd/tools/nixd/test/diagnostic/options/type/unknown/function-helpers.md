# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.example.enable = { _type = "option"; type = { name = "bool"; description = "boolean"; }; }; }' \
# RUN: < %s | FileCheck \
# RUN: --implicit-check-not='unknown option `mkUnitDependencies`' \
# RUN: --implicit-check-not='unknown option `mkUnitDependencies.after`' \
# RUN: --implicit-check-not='unknown option `mkUnitDependencies.wants`' \
# RUN: --implicit-check-not='unknown option `mkTimer`' \
# RUN: --implicit-check-not='unknown option `mkTimer.wantedBy`' \
# RUN: --implicit-check-not='unknown option `localOpenBaoAddr`' \
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

```nix file:///function-helpers.nix
let
  localValue = "ok";
in
rec {
  localOpenBaoAddr = "http://127.0.0.1:8200";

  mkUnitDependencies = units: {
    after = units;
    wants = units;
  };

  mkTimer = timerConfig: {
    wantedBy = [ "timers.target" ];
    inherit timerConfig;
  };

  services.example.typo = true;
}
```

```
CHECK: "id": 0
CHECK: "message": "unknown option `services.example.typo`"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
