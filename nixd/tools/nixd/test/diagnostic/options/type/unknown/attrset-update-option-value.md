# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let strType = { name = "str"; description = "string"; }; boolType = { name = "bool"; description = "boolean"; }; tokenModule = { name = "submodule"; description = "submodule"; nestedTypes.freeformType = strType; }; consumerModule = { name = "submodule"; description = "submodule"; getSubOptions = _: { databaseName = { _type = "option"; type = strType; }; ownerRole = { _type = "option"; type = strType; }; }; }; registryModule = { name = "submodule"; description = "submodule"; getSubOptions = _: { serviceTokens = { _type = "option"; type = { name = "attrsOf"; description = "attribute set of submodule"; nestedTypes.elemType = tokenModule; }; }; bootstrapDependentServices = { _type = "option"; type = { name = "listOf"; description = "list of string"; nestedTypes.elemType = strType; }; }; updateServices = { _type = "option"; type = { name = "listOf"; description = "list of string"; nestedTypes.elemType = strType; }; }; dbCredentialConsumers = { _type = "option"; type = { name = "attrsOf"; description = "attribute set of submodule"; nestedTypes.elemType = consumerModule; }; }; }; }; in { clusterZero.openbaoCredentialRegistry = { _type = "option"; type = registryModule; }; services.example.enable = { _type = "option"; type = boolType; }; }' \
# RUN: < %s | FileCheck \
# RUN: --implicit-check-not='unknown option `serviceTokens`' \
# RUN: --implicit-check-not='unknown option `bootstrapDependentServices`' \
# RUN: --implicit-check-not='unknown option `dbCredentialConsumers.authentik`' \
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

```nix file:///attrset-update-option-value.nix
let
  credentials = {
    serviceTokens.admin.tokenFile = "/run/admin-token";
    mkRegistryServices = names: {
      updateServices = map (name: "${name}.service") names;
    };
  };
in
{
  config.clusterZero.openbaoCredentialRegistry =
    credentials.mkRegistryServices [
      "admin-password"
      "secret-key"
    ]
    // {
      dbCredentialConsumers.authentik = {
        databaseName = "authentik";
        ownerRole = "authentik";
      };

      serviceTokens = credentials.serviceTokens;

      bootstrapDependentServices = [
        "authentik-server.service"
        "authentik-worker.service"
      ];

      typo = true;
    };

  config.services.example.enable = true;
}
```

```
CHECK: "id": 0
CHECK: "message": "unknown option `clusterZero.openbaoCredentialRegistry.typo`"
CHECK-NOT: unknown option `serviceTokens`
CHECK-NOT: unknown option `bootstrapDependentServices`
CHECK-NOT: unknown option `dbCredentialConsumers.authentik`
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
