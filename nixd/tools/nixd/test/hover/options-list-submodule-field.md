# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let strType = { name = "str"; description = "string"; }; routeType = { name = "submodule"; description = "route submodule"; getSubOptions = _: { path = { _type = "option"; description = "HTTP route path."; type = strType; }; upstream = { _type = "option"; type = strType; }; }; }; workerType = { name = "submodule"; description = "worker submodule"; getSubOptions = _: { routes = { _type = "option"; description = "Routes served by this worker."; type = { name = "listOf"; description = "list of route"; nestedTypes.elemType = routeType; }; }; }; }; in { services.example.workers = { _type = "option"; description = "Dynamic workers."; type = { name = "attrsOf"; description = "attribute set of worker"; nestedTypes.elemType = workerType; }; }; }' \
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

```nix file:///hover-options-list-submodule-field.nix
{
  services.example.workers.queue = {
    routes = [
      {
        path = "/jobs";
      }
    ];
  };
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
      "uri": "file:///hover-options-list-submodule-field.nix"
    },
    "position": {
      "line": 4,
      "character": 10
    }
  }
}
```

```
CHECK:      "id": 1,
CHECK:      "kind": "markdown",
CHECK:      "value": "\"type\": `str` - string"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
