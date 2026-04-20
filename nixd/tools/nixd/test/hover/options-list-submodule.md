# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let strType = { name = "str"; description = "string"; }; intType = { name = "int"; description = "signed integer"; }; listStrType = { name = "listOf"; description = "list of string"; nestedTypes.elemType = strType; }; matrixType = { name = "listOf"; description = "list of submodule"; getSubOptions = _: { name = { _type = "option"; type = strType; required = true; }; weight = { _type = "option"; type = intType; default = 100; }; zones = { _type = "option"; type = listStrType; }; }; }; in { services.example.matrix = { _type = "option"; description = "Plain list of submodules."; type = matrixType; }; }' \
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

```nix file:///hover-options-list-submodule.nix
{
  services.example = {
    matrix = [
      {
        name = "blue";
        weight = 80;
        zones = [ "us-east-1a" ];
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
      "uri": "file:///hover-options-list-submodule.nix"
    },
    "position": {
      "line": 2,
      "character": 6
    }
  }
}
```

```
CHECK:      "id": 1,
CHECK:      "kind": "markdown",
CHECK:      "value": "**Type** `listOf` - list of submodule  \n**Description** Plain list of submodules."
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
