# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.example.pkgPath = { _type = "option"; type = { name = "package"; description = "package"; }; }; services.example.pkgStoreString = { _type = "option"; type = { name = "package"; description = "package"; }; }; services.example.pkgDerivationType = { _type = "option"; type = { name = "package"; description = "package"; }; }; services.example.pkgDerivationOutPath = { _type = "option"; type = { name = "package"; description = "package"; }; }; services.example.pkgPlainAttrset = { _type = "option"; type = { name = "package"; description = "package"; }; }; services.example.pkgBadString = { _type = "option"; type = { name = "package"; description = "package"; }; }; services.example.pkgBadBool = { _type = "option"; type = { name = "package"; description = "package"; }; }; services.example.pkgBadList = { _type = "option"; type = { name = "package"; description = "package"; }; }; }' \
# RUN: < %s | FileCheck \
# RUN: --implicit-check-not='value for option `services.example.pkgPath`' \
# RUN: --implicit-check-not='value for option `services.example.pkgStoreString`' \
# RUN: --implicit-check-not='value for option `services.example.pkgDerivationType`' \
# RUN: --implicit-check-not='value for option `services.example.pkgDerivationOutPath`' \
# RUN: --implicit-check-not='value for option `services.example.pkgPlainAttrset`' \
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

```nix file:///package-diagnostics.nix
{
  services.example.pkgPath = ./default.nix;
  services.example.pkgStoreString = "/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-example";
  services.example.pkgDerivationType = { type = "derivation"; };
  services.example.pkgDerivationOutPath = { outPath = "/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-example"; };
  services.example.pkgPlainAttrset = { name = "not-a-derivation"; };
  services.example.pkgBadString = "hello";
  services.example.pkgBadBool = true;
  services.example.pkgBadList = [];
}
```

```
CHECK-DAG: "message": "value for option `services.example.pkgBadString` has type `string`, expected `package package`"
CHECK-DAG: "message": "value for option `services.example.pkgBadBool` has type `boolean`, expected `package package`"
CHECK-DAG: "message": "value for option `services.example.pkgBadList` has type `list`, expected `package package`"
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
