# RUN: nixd-attrset-eval --lit-test < %s | FileCheck %s

```json
{
   "jsonrpc":"2.0",
   "id":0,
   "method":"attrset/evalExpr",
   "params": "{ services.passwd = { _type = \"option\"; type = { name = \"passwdEntry str\"; description = \"string, not containing newlines or colons\"; }; }; services.absolute = { _type = \"option\"; type = { name = \"path\"; description = \"absolute path\"; functor.payload = { absolute = true; inStore = null; }; }; }; services.store = { _type = \"option\"; type = { name = \"path\"; description = \"path in the Nix store\"; functor.payload = { absolute = null; inStore = true; }; }; }; }"
}
```

```json
{
   "jsonrpc":"2.0",
   "id":1,
   "method":"attrset/optionInfo",
   "params": [ "services", "passwd" ]
}
```

```
CHECK:      "Name": "passwdEntry str",
CHECK:      "StringConstraint": {
CHECK:        "PasswdEntry": true,
```

```json
{
   "jsonrpc":"2.0",
   "id":2,
   "method":"attrset/optionInfo",
   "params": [ "services", "absolute" ]
}
```

```
CHECK:      "Description": "absolute path",
CHECK:      "Name": "path",
CHECK:      "PathConstraint": {
CHECK:        "Absolute": true,
CHECK:        "AcceptsStringLike": true
```

```json
{
   "jsonrpc":"2.0",
   "id":3,
   "method":"attrset/optionInfo",
   "params": [ "services", "store" ]
}
```

```
CHECK:      "Description": "path in the Nix store",
CHECK:      "Name": "path",
CHECK:      "PathConstraint": {
CHECK:        "Absolute": true,
CHECK:        "AcceptsStringLike": true,
CHECK:        "InStore": true
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
