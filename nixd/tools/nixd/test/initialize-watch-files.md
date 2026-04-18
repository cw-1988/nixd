# RUN: nixd --lit-test < %s | FileCheck %s

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
         "workspace":{
            "didChangeWatchedFiles":{
               "dynamicRegistration":true
            }
         }
      },
      "trace":"off"
   }
}
```

<-- initialized

```json
{
   "jsonrpc":"2.0",
   "method":"initialized",
   "params":{

   }
}
```

```
CHECK: "method": "client/registerCapability"
CHECK: "registrations": [
CHECK: "id": "nixd-watch-nix-files"
CHECK: "method": "workspace/didChangeWatchedFiles"
CHECK: "globPattern": "**/*.nix"
CHECK: "kind": 7
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
