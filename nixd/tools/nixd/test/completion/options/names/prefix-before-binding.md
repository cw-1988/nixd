# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let boolType = { name = "bool"; description = "boolean"; }; submoduleType = { name = "submodule"; description = "submodule"; getSubOptions = _: { after = { _type = "option"; type = { name = "listOf"; description = "list of string"; nestedTypes.elemType = { name = "str"; description = "string"; }; }; }; wants = { _type = "option"; type = { name = "listOf"; description = "list of string"; nestedTypes.elemType = { name = "str"; description = "string"; }; }; }; enable = { _type = "option"; type = boolType; default = false; }; }; }; in { systemd.services = { _type = "option"; type = { name = "attrsOf"; description = "attribute set of submodule"; nestedTypes.elemType = submoduleType; }; }; }' \
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

```nix file:///completion-prefix-before-binding.nix
{
  systemd.services.authentik-server = {
    w
    unitConfig.ConditionPathExists = true;
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
            "uri": "file:///completion-prefix-before-binding.nix"
        },
        "position": {
            "line": 2,
            "character": 5
        },
        "context": {
            "triggerKind": 3
        }
    }
}
```

```
     CHECK: "id": 1,
CHECK:      "label": "wants",
CHECK-NOT:  "label": "after",
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
