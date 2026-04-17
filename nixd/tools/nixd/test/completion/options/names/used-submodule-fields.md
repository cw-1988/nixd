# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let boolType = { name = "bool"; description = "boolean"; }; strType = { name = "str"; description = "string"; }; pathType = { name = "path"; description = "path"; }; submoduleType = { name = "submodule"; description = "submodule"; getSubOptions = _: { enable = { _type = "option"; type = boolType; }; role = { _type = "option"; type = strType; }; charts = { _type = "option"; type = { name = "attrsOf"; description = "attribute set of path"; nestedTypes.elemType = pathType; }; }; }; }; in { services.k3s = { _type = "option"; type = submoduleType; }; }' \
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

```nix file:///completion-used-submodule-fields.nix
{
  services.k3s = {
    enable = true;
    
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
            "uri": "file:///completion-used-submodule-fields.nix"
        },
        "position": {
            "line": 3,
            "character": 4
        },
        "context": {
            "triggerKind": 1
        }
    }
}
```

```
     CHECK: "id": 1,
CHECK:      "label": "charts",
CHECK-NOT:  "label": "enable",
CHECK:      "label": "role",
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
