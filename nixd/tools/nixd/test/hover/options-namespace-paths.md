# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ users.users = { _type = "option"; description = "User accounts."; type = { name = "attrsOf"; description = "attribute set of submodule"; getSubOptions = _: { openssh = { authorizedKeys = { keys = { _type = "option"; description = "Authorized public keys."; type = { name = "listOf"; description = "list of string"; nestedTypes.elemType = { name = "str"; description = "string"; }; }; }; }; }; }; }; }; }' \
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

```nix file:///hover-options-namespace-paths.nix
{
  users.users.root.openssh.authorizedKeys.keys = [
    "ssh-ed25519 AAAA...your-key..."
  ];
}
```

<-- textDocument/hover(1)

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///hover-options-namespace-paths.nix"
    },
    "position": {
      "line": 1,
      "character": 2
    }
  }
}
```

```
CHECK:      "id": 1,
CHECK:      "kind": "markdown",
CHECK:      "value": "\"type\": `namespace`"
```

<-- textDocument/hover(2)

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///hover-options-namespace-paths.nix"
    },
    "position": {
      "line": 1,
      "character": 8
    }
  }
}
```

```
CHECK:      "id": 2,
CHECK:      "value": "\"type\": `attrsOf` - attribute set of submodule  \n\"description\": User accounts."
```

<-- textDocument/hover(3)

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///hover-options-namespace-paths.nix"
    },
    "position": {
      "line": 1,
      "character": 14
    }
  }
}
```

```
CHECK:      "id": 3,
CHECK:      "value": "\"type\": `namespace`"
```

<-- textDocument/hover(4)

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///hover-options-namespace-paths.nix"
    },
    "position": {
      "line": 1,
      "character": 19
    }
  }
}
```

```
CHECK:      "id": 4,
CHECK:      "value": "\"type\": `namespace`"
```

<-- textDocument/hover(5)

```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///hover-options-namespace-paths.nix"
    },
    "position": {
      "line": 1,
      "character": 27
    }
  }
}
```

```
CHECK:      "id": 5,
CHECK:      "value": "\"type\": `namespace`"
```

<-- textDocument/hover(6)

```json
{
  "jsonrpc": "2.0",
  "id": 6,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///hover-options-namespace-paths.nix"
    },
    "position": {
      "line": 1,
      "character": 42
    }
  }
}
```

```
CHECK:      "id": 6,
CHECK:      "value": "\"type\": `listOf` - list of string"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
