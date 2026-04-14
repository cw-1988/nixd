# RUN: nixd-attrset-eval --lit-test < %s | FileCheck %s

```json
{
   "jsonrpc":"2.0",
   "id":0,
   "method":"attrset/evalExpr",
   "params": "{ services.mode = { _type = \"option\"; type = { name = \"enum\"; description = \"zero or false\"; functor.payload.values = [ 0 false ]; }; }; }"
}
```

```json
{
   "jsonrpc":"2.0",
   "id":1,
   "method":"attrset/optionInfo",
   "params": [ "services", "mode" ]
}
```

```
CHECK:      "EnumValues": [
CHECK-NEXT:   0,
CHECK-NEXT:   false
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
