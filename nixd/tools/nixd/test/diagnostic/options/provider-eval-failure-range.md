# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='builtins.throw "The option `services.example.enable'\'' does not exist."' \
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

```nix file:///basic.nix
{ services.example.enable = true; }
```

<-- textDocument/hover(1)

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "textDocument/hover",
  "params": {
    "textDocument": {
      "uri": "file:///basic.nix"
    },
    "position": {
      "line": 0,
      "character": 19
    }
  }
}
```

```
CHECK: "code": "option-provider-eval"
CHECK: "message": "option provider `nixos` failed to evaluate: -32001: The option `services.example.enable' does not exist."
CHECK: "end": {
CHECK-NEXT: "character": 25,
CHECK-NEXT: "line": 0
CHECK: "start": {
CHECK-NEXT: "character": 19,
CHECK-NEXT: "line": 0
```

<-- nixd/waitForOptionsSettled(999)

```json
{
  "jsonrpc": "2.0",
  "id": 999,
  "method": "nixd/waitForOptionsSettled",
  "params": null
}
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
