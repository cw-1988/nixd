# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.example.enable = { _type = "option"; type = { name = "bool"; description = "boolean"; }; }; services.example.mode = { _type = "option"; type = { name = "enum"; description = "one of fast or slow"; functor.payload.values = [ "fast" "slow" ]; }; }; }' \
# RUN: < %s | FileCheck \
# RUN: --implicit-check-not='unknown option `services.example`' \
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

```nix file:///namespaces.nix
{
  services.example = {
    enable = true;
    mode = "fast";
  };
  services.example.typo = true;
}
```

<-- textDocument/didChange

```json
{
  "jsonrpc": "2.0",
  "method": "textDocument/didChange",
  "params": {
    "textDocument": {
      "uri": "file:///namespaces.nix",
      "version": 2
    },
    "contentChanges": [
      {
        "text": "{\n  services.example = {\n    enable = true;\n    mode = \"fast\";\n  };\n  services.example.typo = true;\n}\n"
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
      "uri": "file:///namespaces.nix"
    },
    "position": {
      "line": 1,
      "character": 12
    }
  }
}
```

```
CHECK: "message": "unknown option `services.example.typo`"
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
