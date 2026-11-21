# RUN: nixd --lit-test < %s | FileCheck --implicit-check-not='flake-unknown-attr' %s

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

The check keys off the file name, so an attribute set that happens to look like
a flake is not diagnosed unless it really is `flake.nix`.

```nix file:///default.nix
{
  description = "example";
  outputs = { ... }: { };
  packages.x86_64-linux.default = 1;
}
```

```
CHECK: "uri": "file:///default.nix"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
