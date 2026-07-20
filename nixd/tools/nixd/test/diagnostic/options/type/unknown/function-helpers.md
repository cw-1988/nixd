# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.example.enable = { _type = "option"; type = { name = "bool"; description = "boolean"; }; }; systemd.tmpfiles.rules = { _type = "option"; type = { name = "listOf"; description = "list of string"; nestedTypes.elemType = { name = "str"; description = "string"; }; }; }; }' \
# RUN: < %s | FileCheck \
# RUN: --implicit-check-not='unknown option `mkUnitDependencies`' \
# RUN: --implicit-check-not='unknown option `mkUnitDependencies.after`' \
# RUN: --implicit-check-not='unknown option `mkUnitDependencies.wants`' \
# RUN: --implicit-check-not='unknown option `mkTimer`' \
# RUN: --implicit-check-not='unknown option `mkTimer.wantedBy`' \
# RUN: --implicit-check-not='unknown option `localOpenBaoAddr`' \
# RUN: --implicit-check-not='unknown option `paths`' \
# RUN: --implicit-check-not='unknown option `mode`' \
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

  helpers.mkTmpfileDirectories = { paths, mode }: [ mode ];

  systemd.tmpfiles.rules = helpers.mkTmpfileDirectories {
    paths = [ "/var/lib/authentik" ];
    mode = "0750";
  };
  services.example.typo = true;
}
```

```
CHECK: "id": 0
CHECK: "code": "option-unknown"
CHECK-NEXT: "message": "unknown option `services.example.typo`"
CHECK: "severity": 4
CHECK-NEXT: "source": "nixd"
CHECK-NEXT: "tags": [
CHECK-NEXT: 1
CHECK-NEXT: ]
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
