# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.k3s.enable = { _type = "option"; description = "Enable k3s."; declarationPositions = [ { file = "/decl-k3s-enable"; line = 9; column = 1; } ]; type = { name = "bool"; description = "boolean"; }; }; services.k3s.role = { _type = "option"; description = "Node role."; declarationPositions = [ { file = "/decl-k3s-role"; line = 13; column = 1; } ]; type = { name = "enum"; description = "role"; }; }; }' \
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

```nix file:///definition-options-namespace-attrset.nix
{
  services.k3s = {
    enable = true;
    role = "server";
  };
}
```

<-- textDocument/definition(2)

```json
{
   "jsonrpc":"2.0",
   "id":2,
   "method":"textDocument/definition",
   "params":{
      "textDocument":{
         "uri":"file:///definition-options-namespace-attrset.nix"
      },
      "position":{
        "line": 1,
        "character":2
      }
   }
}
```

<-- textDocument/definition(3)

```json
{
   "jsonrpc":"2.0",
   "id":3,
   "method":"textDocument/definition",
   "params":{
      "textDocument":{
         "uri":"file:///definition-options-namespace-attrset.nix"
      },
      "position":{
        "line": 1,
        "character":11
      }
   }
}
```

```
CHECK-LABEL: "id": 2,
CHECK:       "result": {
CHECK:       "uri": "file:///decl-k3s-enable"
CHECK-LABEL: "id": 3,
CHECK:       "result": {
CHECK:       "uri": "file:///decl-k3s-enable"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
