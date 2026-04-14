# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.k3s.charts = { _type = "option"; type = { name = "attrsOf"; description = "attribute set of (absolute path or package)"; nestedTypes.elemType = { name = "either"; description = "absolute path or package"; nestedTypes.left = { name = "path"; description = "absolute path"; }; nestedTypes.right = { name = "package"; description = "package"; }; }; }; }; }' \
# RUN: < %s | FileCheck %s

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

```nix file:///charts-dotted.nix
{
  services.k3s.charts.goodPath = ../charts/my-chart.tgz;
  services.k3s.charts.badString = "banana";
  services.k3s.charts.badBool = true;
}
```

```
CHECK-NOT: value for option `services.k3s.charts.goodPath`
CHECK: "message": "value for option `services.k3s.charts.badString` has type `string`, expected `path absolute path`"
CHECK: "message": "value for option `services.k3s.charts.badBool` has type `boolean`, expected `path absolute path`"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
