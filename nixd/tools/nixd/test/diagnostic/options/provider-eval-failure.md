# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='builtins.throw "nixos provider boom"' \
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
      "character": 11
    }
  }
}
```

```
CHECK: "code": "option-provider-eval"
CHECK: "message": "option provider `nixos` failed to evaluate:
CHECK-SAME: nixos provider boom
CHECK: "source": "nixd"
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
