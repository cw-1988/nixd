# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let intType = { name = "int"; description = "signed integer"; }; strType = { name = "str"; description = "string"; }; moduleType = { name = "submodule"; description = "submodule"; getSubOptions = _: { known = { _type = "option"; type = intType; }; required = { _type = "option"; type = strType; required = true; }; settings = { _type = "option"; type = { name = "attrsOf"; description = "attribute set of signed integer"; nestedTypes.elemType = intType; }; }; }; }; in { services.example.moduleAsLambda = { _type = "option"; type = moduleType; }; }' \
# RUN: < %s | FileCheck \
# RUN: --implicit-check-not='unknown option `required`' \
# RUN: --implicit-check-not='unknown option `known`' \
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
      "capabilities":{
      },
      "trace":"off"
   }
}
```

<-- textDocument/didOpen

```nix file:///module-lambda-body.nix
{
  services.example.moduleAsLambda =
    { config, ... }:
    {
      required = "lambda module";
      known = config.settings.retries + 1;
      settings.retries = 6;
      mystery = true;
    };
}
```

<-- textDocument/didChange

```json
{
  "jsonrpc": "2.0",
  "method": "textDocument/didChange",
  "params": {
    "textDocument": {
      "uri": "file:///module-lambda-body.nix",
      "version": 2
    },
    "contentChanges": [
      {
        "text": "{\n  services.example.moduleAsLambda =\n    { config, ... }:\n    {\n      required = \"lambda module\";\n      known = config.settings.retries + 1;\n      settings.retries = 6;\n      mystery = true;\n    };\n}\n"
      }
    ]
  }
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
      "uri": "file:///module-lambda-body.nix"
    },
    "position": {
      "line": 4,
      "character": 8
    }
  }
}
```

```
CHECK: "message": "unknown option `services.example.moduleAsLambda.mystery`"
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
