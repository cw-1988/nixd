# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.k3s.agentTokenFile = { _type = "option"; type = { name = "nullOr"; description = "null or absolute path"; }; }; services.k3s.badTokenFile = { _type = "option"; type = { name = "nullOr"; description = "null or absolute path"; }; }; services.k3s.absolutePath = { _type = "option"; type = { name = "path"; description = "absolute path"; }; }; services.k3s.relativePath = { _type = "option"; type = { name = "path"; description = "absolute path"; }; }; services.k3s.absoluteString = { _type = "option"; type = { name = "path"; description = "absolute path"; }; }; services.k3s.relativeString = { _type = "option"; type = { name = "path"; description = "absolute path"; }; }; services.k3s.storePath = { _type = "option"; type = { name = "path"; description = "path in the Nix store"; functor.payload = { absolute = null; inStore = true; }; }; }; services.k3s.storeString = { _type = "option"; type = { name = "path"; description = "path in the Nix store"; functor.payload = { absolute = null; inStore = true; }; }; }; services.k3s.badStoreString = { _type = "option"; type = { name = "path"; description = "path in the Nix store"; functor.payload = { absolute = null; inStore = true; }; }; }; services.k3s.payloadRelativePath = { _type = "option"; type = { name = "path"; description = "path"; functor.payload = { absolute = false; inStore = null; }; }; }; services.k3s.payloadString = { _type = "option"; type = { name = "path"; description = "path"; functor.payload = { absolute = false; inStore = null; }; }; }; }' \
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
  services.k3s.absolutePath = /etc;
  services.k3s.relativePath = ./etc;
  services.k3s.absoluteString = "/etc";
  services.k3s.relativeString = "etc";
  services.k3s.storePath = /nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-example;
  services.k3s.storeString = "/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-example";
  services.k3s.badStoreString = "/etc";
  services.k3s.payloadRelativePath = ./etc;
  services.k3s.payloadString = "etc";
}
```

```
CHECK-NOT: value for option `services.k3s.agentTokenFile`
     CHECK: "code": "option-value-type"
CHECK: "message": "value for option `services.k3s.badTokenFile` has type `string`, expected `nullOr null or absolute path`"
CHECK-NOT: value for option `services.k3s.absolutePath`
CHECK: "message": "value for option `services.k3s.relativePath` has type `path`, expected `path absolute path`"
CHECK-NOT: value for option `services.k3s.absoluteString`
CHECK: "message": "value for option `services.k3s.relativeString` has type `string`, expected `path absolute path`"
CHECK-NOT: value for option `services.k3s.storePath`
CHECK-NOT: value for option `services.k3s.storeString`
CHECK: "message": "value for option `services.k3s.badStoreString` has type `string`, expected `path path in the Nix store`"
CHECK-NOT: value for option `services.k3s.payloadRelativePath`
CHECK: "message": "value for option `services.k3s.payloadString` has type `string`, expected `path path`"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
