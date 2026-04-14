# RUN: nixd-attrset-eval --lit-test < %s | FileCheck %s

```json
{
   "jsonrpc":"2.0",
   "id":0,
   "method":"attrset/evalExpr",
   "params": "let intType = { name = \"int\"; description = \"signed integer\"; }; nestedModule = { name = \"submodule\"; description = \"submodule\"; getSubOptions = _: { settings = { bar = { _type = \"option\"; type = intType; }; }; }; }; largeModule = { name = \"submodule\"; description = \"submodule\"; getSubOptions = _: builtins.listToAttrs (builtins.genList (n: { name = \"opt${toString (n + 100)}\"; value = { _type = \"option\"; type = intType; }; }) 70); }; in { services.nested = { _type = \"option\"; type = nestedModule; }; services.large = { _type = \"option\"; type = largeModule; }; }"
}
```

```json
{
   "jsonrpc":"2.0",
   "id":1,
   "method":"attrset/optionInfo",
   "params": [ "services", "nested" ]
}
```

```
CHECK:      "Description": "submodule",
CHECK:      "KnownSubOptions": {
CHECK:        "settings": {
CHECK:          "Required": false,
CHECK:      "Name": "submodule",
CHECK:      "NestedTypes": {
CHECK:        "settings": {
CHECK:          "KnownSubOptions": {
CHECK:            "bar": {
CHECK:              "Required": false,
CHECK:          "NestedTypes": {
CHECK:            "bar": {
CHECK:              "Name": "int"
```

```json
{
   "jsonrpc":"2.0",
   "id":2,
   "method":"attrset/optionInfo",
   "params": [ "services", "large" ]
}
```

```
CHECK:      "KnownSubOptionsComplete": false
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
