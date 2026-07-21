# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let boolType = { name = "bool"; description = "boolean"; }; submoduleType = { name = "submodule"; description = "submodule"; getSubOptions = _: { after = { _type = "option"; type = { name = "listOf"; description = "list of string"; nestedTypes.elemType = { name = "str"; description = "string"; }; }; }; wants = { _type = "option"; type = { name = "listOf"; description = "list of string"; nestedTypes.elemType = { name = "str"; description = "string"; }; }; }; enable = { _type = "option"; type = boolType; default = false; }; }; }; in { systemd.services = { _type = "option"; type = { name = "attrsOf"; description = "attribute set of submodule"; nestedTypes.elemType = submoduleType; }; }; }' \
# RUN: < %s | FileCheck %s

<-- initialize(0)

```json
{
  "jsonrpc": "2.0",
  "id": 0,
  "method": "initialize",
  "params": {
    "processId": 123,
    "rootPath": "",
    "capabilities": {},
    "trace": "off"
  }
}
```

<-- textDocument/didOpen

```nix file:///completion-no-nested-call-argument.nix
{
  systemd.services.authentik-server =
    container.mkContainer "authentik-server" [ "server" ]
      {
        
      };
}
```

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "textDocument/completion",
  "params": {
    "textDocument": {
      "uri": "file:///completion-no-nested-call-argument.nix"
    },
    "position": {
      "line": 4,
      "character": 8
    },
    "context": {
      "triggerKind": 1
    }
  }
}
```

```
     CHECK: "id": 1,
CHECK:      "items": []
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
