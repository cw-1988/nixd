# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let boolType = { name = "bool"; description = "boolean"; }; submoduleType = { name = "submodule"; description = "submodule"; getSubOptions = _: { enable = { _type = "option"; type = boolType; default = false; }; name = { _type = "option"; type = { name = "str"; description = "string"; }; }; }; }; in { services.example.instances = { _type = "option"; type = { name = "attrsOf"; description = "attribute set of submodule"; nestedTypes.elemType = submoduleType; }; }; }' \
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

```nix file:///completion-attrs-submodule.nix
{ services.example.instances.web = { en }; }
```

```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "textDocument/completion",
    "params": {
        "textDocument": {
            "uri": "file:///completion-attrs-submodule.nix"
        },
        "position": {
            "line": 0,
            "character": 39
        },
        "context": {
            "triggerKind": 1
        }
    }
}
```

```
     CHECK: "id": 1,
CHECK:      "detail": "nixos | bool (boolean)",
CHECK:      "kind": 4,
CHECK:      "label": "enable",
CHECK-NOT:  "label": "name",
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
