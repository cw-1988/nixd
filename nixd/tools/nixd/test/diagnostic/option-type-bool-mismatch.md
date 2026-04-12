# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.k3s.enable = { _type = "option"; type = { name = "bool"; }; }; }' \
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
{ services.k3s.enable = ""; }
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
      "character": 18
    }
  }
}
```

```
     CHECK: "code": "option-value-type"
CHECK: "message": "value for option `services.k3s.enable` has type `string`, expected `bool`"
CHECK: "source": "nixd"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
