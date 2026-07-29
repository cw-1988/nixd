# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='let strType = { name = "str"; description = "string"; }; listType = { name = "listOf"; description = "list of string"; nestedTypes.elemType = strType; }; filler = builtins.listToAttrs (builtins.genList (n: { name = "field${toString n}"; value = { _type = "option"; type = strType; }; }) 31); submoduleType = { name = "submodule"; description = "submodule"; getSubOptions = _: filler // { wants = { _type = "option"; description = "Units this service wants."; type = listType; }; unitConfig = { _type = "option"; type = { name = "attrsOf"; description = "attribute set of systemd settings"; nestedTypes.elemType = strType; }; }; }; }; in { systemd.services = { _type = "option"; type = { name = "attrsOf"; description = "attribute set of submodule"; nestedTypes.elemType = submoduleType; }; }; }' \
# RUN: < %s | FileCheck %s

<-- initialize(0)

```json
{
  "jsonrpc": "2.0",
  "id": 0,
  "method": "initialize",
  "params": {
    "processId": 123,
    "rootPath": "",
    "capabilities": {},
    "trace": "off"
  }
}
```

<-- textDocument/didOpen

```nix file:///completion-blank-large-submodule.nix
{
  systemd.services.authentik-worker = {

    unitConfig.ConditionPathExists = true;
  };
}
```

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "textDocument/completion",
  "params": {
    "textDocument": {
      "uri": "file:///completion-blank-large-submodule.nix"
    },
    "position": {
      "line": 2,
      "character": 4
    },
    "context": {
      "triggerKind": 1
    }
  }
}
```

```
     CHECK: "id": 1,
CHECK:      "label": "wants",
```

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "completionItem/resolve",
  "params": {
    "label": "wants",
    "data": "{\"Kind\":\"option\",\"Provider\":\"nixos\",\"Scope\":[\"systemd\",\"services\",\"authentik-worker\"]}"
  }
}
```

```
     CHECK: "id": 2,
CHECK-NEXT: "jsonrpc": "2.0",
CHECK-NEXT: "result": {
CHECK-NEXT:   "data": "{\"Kind\":\"option\",\"Provider\":\"nixos\",\"Scope\":[\"systemd\",\"services\",\"authentik-worker\"]}",
CHECK-NEXT:   "detail": "nixos | listOf (list of string)",
CHECK-NEXT:   "documentation": {
CHECK-NEXT:     "kind": "markdown",
CHECK-NEXT:     "value": "Units this service wants."
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
