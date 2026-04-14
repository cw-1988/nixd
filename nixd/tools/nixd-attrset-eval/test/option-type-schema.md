# RUN: nixd-attrset-eval --lit-test < %s | FileCheck %s

```nix
let
  intType = {
    name = "int";
    description = "signed integer";
  };
  strType = {
    name = "str";
    description = "string";
  };
  submoduleType = {
    name = "submodule";
    description = "submodule";
    nestedTypes.freeformType = strType;
    getSubOptions = _: {
      known = {
        _type = "option";
        type = intType;
        default = 1;
      };
      required = {
        _type = "option";
        type = strType;
        required = true;
      };
    };
  };
in
{
  services.example = {
    _type = "option";
    type = {
      name = "attrsOf";
      description = "attribute set of submodule";
      nestedTypes.elemType = submoduleType;
    };
  };
  services.mode = {
    _type = "option";
    type = {
      name = "enum";
      description = "one of fast, 1, true, or null";
      functor.payload.values = [ "fast" 1 true null ];
    };
  };
  services.storePath = {
    _type = "option";
    type = {
      name = "pathInStore";
      description = "store path";
      functor.payload = {};
    };
  };
}
```

```json
{
   "jsonrpc":"2.0",
   "id":0,
   "method":"attrset/evalExpr",
   "params": "let\n  intType = {\n    name = \"int\";\n    description = \"signed integer\";\n  };\n  strType = {\n    name = \"str\";\n    description = \"string\";\n  };\n  submoduleType = {\n    name = \"submodule\";\n    description = \"submodule\";\n    nestedTypes.freeformType = strType;\n    getSubOptions = _: {\n      known = {\n        _type = \"option\";\n        type = intType;\n        default = 1;\n      };\n      required = {\n        _type = \"option\";\n        type = strType;\n        required = true;\n      };\n    };\n  };\nin\n{\n  services.example = {\n    _type = \"option\";\n    type = {\n      name = \"attrsOf\";\n      description = \"attribute set of submodule\";\n      nestedTypes.elemType = submoduleType;\n    };\n  };\n  services.mode = {\n    _type = \"option\";\n    type = {\n      name = \"enum\";\n      description = \"one of fast, 1, true, or null\";\n      functor.payload.values = [ \"fast\" 1 true null ];\n    };\n  };\n  services.storePath = {\n    _type = \"option\";\n    type = {\n      name = \"pathInStore\";\n      description = \"store path\";\n      functor.payload = {};\n    };\n  };\n}"
}
```

```json
{
   "jsonrpc":"2.0",
   "id":1,
   "method":"attrset/optionInfo",
   "params": [ "services", "example" ]
}
```

```
CHECK:      "Name": "attrsOf",
CHECK:      "NestedTypes": {
CHECK:        "elemType": {
CHECK:          "KnownSubOptions": {
CHECK:            "known": {
CHECK:              "HasDefault": true,
CHECK:              "Required": false,
CHECK:            "required": {
CHECK:              "HasDefault": false,
CHECK:              "Required": true,
CHECK:          "Name": "submodule",
CHECK:          "NestedTypes": {
CHECK:            "freeformType": {
CHECK:              "Name": "str"
CHECK:            "known": {
CHECK:              "Name": "int"
CHECK:            "required": {
CHECK:              "Name": "str"
```

```json
{
   "jsonrpc":"2.0",
   "id":2,
   "method":"attrset/optionInfo",
   "params": [ "services", "mode" ]
}
```

```
CHECK:      "EnumValues": [
CHECK-NEXT:   "fast",
CHECK-NEXT:   1,
CHECK-NEXT:   true,
CHECK-NEXT:   null
CHECK:      "Name": "enum"
```

```json
{
   "jsonrpc":"2.0",
   "id":3,
   "method":"attrset/optionInfo",
   "params": [ "services", "storePath" ]
}
```

```
CHECK:      "Name": "pathInStore",
CHECK:      "PathConstraint": {
CHECK:        "Absolute": true,
CHECK:        "AcceptsStringLike": true,
CHECK:        "InStore": true
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
