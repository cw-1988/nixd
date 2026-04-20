# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.k3s.enable = { _type = "option"; type = { name = "bool"; description = "boolean"; }; }; }' \
# RUN: < %s | FileCheck \
# RUN: --implicit-check-not='unknown option `name`' \
# RUN: --implicit-check-not='unknown option `extraBinFlags`' \
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

```nix file:///helper-call-attrset.nix
let
  mkHelper = args: args;
  baseModule = mkHelper {
    name = "k3s";
    extraBinFlags = [ "--bind-address=0.0.0.0" ];
  };
in {
  config.services.k3s.enable = "bad";
}
```

<-- textDocument/didChange

```json
{
  "jsonrpc": "2.0",
  "method": "textDocument/didChange",
  "params": {
    "textDocument": {
      "uri": "file:///helper-call-attrset.nix",
      "version": 2
    },
    "contentChanges": [
      {
        "text": "let\n  mkHelper = args: args;\n  baseModule = mkHelper {\n    name = \"k3s\";\n    extraBinFlags = [ \"--bind-address=0.0.0.0\" ];\n  };\nin {\n  config.services.k3s.enable = \"bad\";\n}\n"
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
      "uri": "file:///helper-call-attrset.nix"
    },
    "position": {
      "line": 7,
      "character": 22
    }
  }
}
```

```
CHECK: "message": "value for option `services.k3s.enable` has type `string`, expected `bool boolean`"
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
