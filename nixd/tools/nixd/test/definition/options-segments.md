# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let strType = { name = "str"; description = "string"; }; keysType = { name = "listOf"; description = "list of string"; nestedTypes.elemType = strType; }; authorizedKeysType = { name = "submodule"; description = "authorized key options"; getSubOptions = _: { keys = { _type = "option"; description = "Authorized public keys."; declarationPositions = [ { file = "/decl-user-keys"; line = 11; column = 1; } ]; type = keysType; }; }; }; userType = { name = "submodule"; description = "user options"; getSubOptions = _: { openssh = { _type = "option"; description = "OpenSSH settings."; type = { name = "submodule"; description = "OpenSSH settings"; getSubOptions = _: { authorizedKeys = { _type = "option"; description = "Authorized key settings."; type = authorizedKeysType; }; }; }; }; }; }; in { accounts = { _type = "option"; declarationPositions = [ { file = "/decl-accounts"; line = 3; column = 1; } ]; description = "Accounts namespace."; type = { name = "submodule"; description = "accounts namespace"; getSubOptions = _: { users = { _type = "option"; description = "User accounts."; declarationPositions = [ { file = "/decl-accounts-users"; line = 7; column = 1; } ]; type = { name = "attrsOf"; description = "attribute set of submodule"; nestedTypes.elemType = userType; }; }; }; }; }; }' \
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

```nix file:///definition-options-segments.nix
{
  accounts.users.root.openssh.authorizedKeys.keys = [ ];
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
         "uri":"file:///definition-options-segments.nix"
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
         "uri":"file:///definition-options-segments.nix"
      },
      "position":{
        "line": 1,
        "character":11
      }
   }
}
```

<-- textDocument/definition(4)

```json
{
   "jsonrpc":"2.0",
   "id":4,
   "method":"textDocument/definition",
   "params":{
      "textDocument":{
         "uri":"file:///definition-options-segments.nix"
      },
      "position":{
        "line": 1,
        "character":17
      }
   }
}
```

<-- textDocument/definition(5)

```json
{
   "jsonrpc":"2.0",
   "id":5,
   "method":"textDocument/definition",
   "params":{
      "textDocument":{
         "uri":"file:///definition-options-segments.nix"
      },
      "position":{
        "line": 1,
        "character":22
      }
   }
}
```

<-- textDocument/definition(6)

```json
{
   "jsonrpc":"2.0",
   "id":6,
   "method":"textDocument/definition",
   "params":{
      "textDocument":{
         "uri":"file:///definition-options-segments.nix"
      },
      "position":{
        "line": 1,
        "character":30
      }
   }
}
```

<-- textDocument/definition(7)

```json
{
   "jsonrpc":"2.0",
   "id":7,
   "method":"textDocument/definition",
   "params":{
      "textDocument":{
         "uri":"file:///definition-options-segments.nix"
      },
      "position":{
        "line": 1,
        "character":45
      }
   }
}
```

```
CHECK-LABEL: "id": 2,
CHECK:       "result": {
CHECK:       "uri": "file:///decl-accounts-users"
CHECK-LABEL: "id": 3,
CHECK:       "result": {
CHECK:       "uri": "file:///decl-accounts-users"
CHECK-LABEL: "id": 4,
CHECK:       "result": {
CHECK:       "uri": "file:///decl-accounts-users"
CHECK-LABEL: "id": 5,
CHECK:       "result": {
CHECK:       "uri": "file:///decl-accounts-users"
CHECK-LABEL: "id": 6,
CHECK:       "result": {
CHECK:       "uri": "file:///decl-accounts-users"
CHECK-LABEL: "id": 7,
CHECK:       "result": {
CHECK:       "uri": "file:///decl-user-keys"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
