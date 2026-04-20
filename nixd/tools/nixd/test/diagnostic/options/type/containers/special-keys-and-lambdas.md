# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let intType = { name = "int"; description = "signed integer"; }; strType = { name = "str"; description = "string"; }; moduleType = { name = "submodule"; description = "submodule"; getSubOptions = _: { known = { _type = "option"; type = intType; }; required = { _type = "option"; type = strType; required = true; }; }; }; in { services.example.specialKeys = { _type = "option"; type = moduleType; }; services.example.importsModule = { _type = "option"; type = moduleType; }; services.example.configWrapped = { _type = "option"; type = moduleType; }; services.example.lambdaModule = { _type = "option"; type = moduleType; }; services.example.fnElem = { _type = "option"; type = { name = "functionTo"; description = "function that evaluates to signed integer"; nestedTypes.elemType = intType; }; }; }' \
# RUN: < %s | FileCheck \
# RUN: --implicit-check-not='unknown option `services.example.specialKeys.options`' \
# RUN: --implicit-check-not='unknown option `services.example.specialKeys.disabledModules`' \
# RUN: --implicit-check-not='unknown option `services.example.specialKeys._module`' \
# RUN: --implicit-check-not='unknown option `services.example.specialKeys.meta`' \
# RUN: --implicit-check-not='unknown option `services.example.importsModule.typo`' \
# RUN: --implicit-check-not='required option `services.example.importsModule.required`' \
# RUN: --implicit-check-not='services.example.configWrapped.config' \
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

```nix file:///special-keys-and-lambdas.nix
{
  services.example.specialKeys = {
    options = {};
    disabledModules = [];
    _module = {};
    meta = {};
    known = 1;
    mystery = true;
  };
  services.example.importsModule = {
    imports = [];
    known = 1;
    typo = true;
  };
  services.example.configWrapped = {
    config = {
      known = "bad";
      required = "ok";
      mystery = true;
    };
  };
  services.example.lambdaModule = name: {
    known = "bad";
    required = "ok";
    mystery = true;
  };
  services.example.fnElem = x: "bad";
}
```

<-- textDocument/didChange

```json
{
  "jsonrpc": "2.0",
  "method": "textDocument/didChange",
  "params": {
    "textDocument": {
      "uri": "file:///special-keys-and-lambdas.nix",
      "version": 2
    },
    "contentChanges": [
      {
        "text": "{\n  services.example.specialKeys = {\n    options = {};\n    disabledModules = [];\n    _module = {};\n    meta = {};\n    known = 1;\n    mystery = true;\n  };\n  services.example.importsModule = {\n    imports = [];\n    known = 1;\n    typo = true;\n  };\n  services.example.configWrapped = {\n    config = {\n      known = \"bad\";\n      required = \"ok\";\n      mystery = true;\n    };\n  };\n  services.example.lambdaModule = name: {\n    known = \"bad\";\n    required = \"ok\";\n    mystery = true;\n  };\n  services.example.fnElem = x: \"bad\";\n}\n"
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
      "uri": "file:///special-keys-and-lambdas.nix"
    },
    "position": {
      "line": 1,
      "character": 20
    }
  }
}
```

```
CHECK-DAG: "message": "unknown option `services.example.specialKeys.mystery`"
CHECK-DAG: "message": "required option `services.example.specialKeys.required` is missing"
CHECK-DAG: "message": "value for option `services.example.configWrapped.known` has type `string`, expected `int signed integer`"
CHECK-DAG: "message": "unknown option `services.example.configWrapped.mystery`"
CHECK-DAG: "message": "value for option `services.example.lambdaModule.known` has type `string`, expected `int signed integer`"
CHECK-DAG: "message": "unknown option `services.example.lambdaModule.mystery`"
CHECK-DAG: "message": "value for option `services.example.fnElem.<return>` has type `string`, expected `int signed integer`"
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
