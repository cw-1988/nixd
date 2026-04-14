# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let intType = { name = "int"; description = "signed integer"; }; strType = { name = "str"; description = "string"; }; attrsType = { name = "attrsOf"; description = "attribute set of signed integer"; nestedTypes.elemType = intType; }; freeModule = { name = "submodule"; description = "submodule"; nestedTypes.freeformType = strType; getSubOptions = _: { known = { _type = "option"; type = intType; }; }; }; strictModule = { name = "submodule"; description = "submodule"; getSubOptions = _: { known = { _type = "option"; type = intType; }; }; }; in { services.example.closed = { _type = "option"; type = strictModule; }; services.example.eitherAttrs = { _type = "option"; type = { name = "either"; description = "attrs or string"; nestedTypes.left = attrsType; nestedTypes.right = strType; }; }; services.example.oneOfFree = { _type = "option"; type = { name = "oneOf"; description = "freeform module or string"; nestedTypes.left = freeModule; nestedTypes.right = strType; }; }; services.example.coercedFree = { _type = "option"; type = { name = "coercedTo"; description = "string coerced to freeform module"; nestedTypes.coercedType = strType; nestedTypes.finalType = freeModule; }; }; services.example.nullFree = { _type = "option"; type = { name = "nullOr"; description = "null or freeform module"; nestedTypes.elemType = freeModule; }; }; services.example.uniqueAttrs = { _type = "option"; type = { name = "unique"; description = "unique attrs"; nestedTypes.elemType = attrsType; }; }; }' \
# RUN: < %s | FileCheck \
# RUN: --implicit-check-not='unknown option `services.example.eitherAttrs.anything.deep`' \
# RUN: --implicit-check-not='unknown option `services.example.oneOfFree.arbitrary.deep`' \
# RUN: --implicit-check-not='unknown option `services.example.coercedFree.arbitrary.deep`' \
# RUN: --implicit-check-not='unknown option `services.example.nullFree.arbitrary.deep`' \
# RUN: --implicit-check-not='unknown option `services.example.uniqueAttrs.anything.deep`' \
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

```nix file:///dynamic-wrappers.nix
{
  services.example.closed.mystery = true;
  services.example.eitherAttrs.anything.deep = 1;
  services.example.oneOfFree.arbitrary.deep = "ok";
  services.example.coercedFree.arbitrary.deep = "ok";
  services.example.nullFree.arbitrary.deep = "ok";
  services.example.uniqueAttrs.anything.deep = 1;
}
```

<-- textDocument/didChange

```json
{
  "jsonrpc": "2.0",
  "method": "textDocument/didChange",
  "params": {
    "textDocument": {
      "uri": "file:///dynamic-wrappers.nix",
      "version": 2
    },
    "contentChanges": [
      {
        "text": "{\n  services.example.closed.mystery = true;\n  services.example.eitherAttrs.anything.deep = 1;\n  services.example.oneOfFree.arbitrary.deep = \"ok\";\n  services.example.coercedFree.arbitrary.deep = \"ok\";\n  services.example.nullFree.arbitrary.deep = \"ok\";\n  services.example.uniqueAttrs.anything.deep = 1;\n}\n"
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
      "uri": "file:///dynamic-wrappers.nix"
    },
    "position": {
      "line": 1,
      "character": 20
    }
  }
}
```

```
CHECK: "message": "unknown option `services.example.closed.mystery`"
CHECK-NOT: unknown option `services.example.eitherAttrs.anything.deep`
CHECK-NOT: unknown option `services.example.oneOfFree.arbitrary.deep`
CHECK-NOT: unknown option `services.example.coercedFree.arbitrary.deep`
CHECK-NOT: unknown option `services.example.nullFree.arbitrary.deep`
CHECK-NOT: unknown option `services.example.uniqueAttrs.anything.deep`
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
