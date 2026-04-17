# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let intType = { name = "int"; description = "signed integer"; }; moduleType = { name = "submodule"; description = "submodule"; getSubOptions = _: { known = { _type = "option"; type = intType; }; }; }; in { services.example.moduleAsLambda = { _type = "option"; type = moduleType; }; }' \
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

```nix file:///module-input-inspect.nix
{
  config,
  ...
}:
{
  services.example.moduleAsLambda =
    { config, ... }:
    {
      known = config.known;
    };
}
```

<-- textDocument/codeAction(1)

```json
{
   "jsonrpc":"2.0",
   "id":1,
   "method":"textDocument/codeAction",
   "params":{
      "textDocument":{
         "uri":"file:///module-input-inspect.nix"
      },
      "range":{
         "start":{
            "line": 1,
            "character":2
         },
         "end":{
            "line":1,
            "character":8
         }
      },
      "context":{
         "diagnostics":[],
         "triggerKind":2
      }
   }
}
```

<-- textDocument/codeAction(2)

```json
{
   "jsonrpc":"2.0",
   "id":2,
   "method":"textDocument/codeAction",
   "params":{
      "textDocument":{
         "uri":"file:///module-input-inspect.nix"
      },
      "range":{
         "start":{
            "line": 6,
            "character":6
         },
         "end":{
            "line":6,
            "character":12
         }
      },
      "context":{
         "diagnostics":[],
         "triggerKind":2
      }
   }
}
```

```
CHECK-LABEL: "id": 1,
CHECK:      "result": [
CHECK:        "input": "config",
CHECK:        "inspectModuleInput": true,
CHECK:        "scope": []
CHECK:        "title": "Inspect module input `config`"
CHECK-LABEL: "id": 2,
CHECK:      "result": [
CHECK:        "title": "Pack dotted path to nested set"
CHECK:        "input": "config",
CHECK:        "inspectModuleInput": true,
CHECK:        "scope": [
CHECK-NEXT:     "services",
CHECK-NEXT:     "example",
CHECK-NEXT:     "moduleAsLambda"
CHECK-NEXT:   ]
CHECK:        "title": "Inspect module input `config`"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
