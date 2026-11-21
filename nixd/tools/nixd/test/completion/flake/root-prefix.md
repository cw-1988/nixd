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
  outputs = { ... }: {
    s
  };
  i
}
```

`i` at the flake root narrows the schema to `inputs`.

```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "textDocument/completion",
    "params": {
        "textDocument": { "uri": "file:///flake.nix" },
        "position": { "line": 5, "character": 3 },
        "context": { "triggerKind": 1 }
    }
}
```

```
     CHECK: "id": 1,
CHECK-NEXT: "jsonrpc": "2.0",
CHECK-NEXT: "result": {
CHECK-NEXT:   "isIncomplete": false,
CHECK-NEXT:   "items": [
CHECK-NEXT:     {
CHECK-NEXT:       "data": "",
CHECK-NEXT:       "detail": "flake | attribute set of flake references",
 CHECK-NOT: "label"
     CHECK:       "insertText": "inputs = { };",
CHECK-NEXT:       "insertTextFormat": 1,
CHECK-NEXT:       "kind": 4,
CHECK-NEXT:       "label": "inputs",
CHECK-NEXT:       "score": 0
CHECK-NEXT:     }
CHECK-NEXT:   ]
```

`s` inside `outputs` is outside the flake schema, so it stays empty rather
than falling back to NixOS options.

```json
{
    "jsonrpc": "2.0",
    "id": 2,
    "method": "textDocument/completion",
    "params": {
        "textDocument": { "uri": "file:///flake.nix" },
        "position": { "line": 3, "character": 5 },
        "context": { "triggerKind": 1 }
    }
}
```

```
     CHECK: "id": 2,
CHECK-NEXT: "jsonrpc": "2.0",
CHECK-NEXT: "result": {
CHECK-NEXT:   "isIncomplete": false,
CHECK-NEXT:   "items": []
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
