# RUN: nixd --lit-test \
# RUN: --nixos-options-expr="{ services.foo = { _type = \"option\"; }; systemd.services = { _type = \"option\"; }; }" \
# RUN: < %s | FileCheck \
# RUN: --implicit-check-not='"label": "services"' \
# RUN: --implicit-check-not='"label": "systemd"' \
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
      "capabilities": {
      },
      "trace":"off"
   }
}
```

<-- textDocument/didOpen

```nix file:///flake.nix
{
  description = "x";
  
}
```

The flake root is a closed schema, so NixOS options must never be offered
there. A blank prefix lists all four attributes Nix accepts.

```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "textDocument/completion",
    "params": {
        "textDocument": { "uri": "file:///flake.nix" },
        "position": { "line": 2, "character": 2 },
        "context": { "triggerKind": 1 }
    }
}
```

```
     CHECK: "id": 1,
CHECK-DAG: "label": "description"
CHECK-DAG: "label": "inputs"
CHECK-DAG: "label": "outputs"
CHECK-DAG: "label": "nixConfig"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
