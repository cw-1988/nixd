# RUN: sh %S/provider-eval-syntax-location.sh %t | FileCheck %s

```
CHECK-NOT: "code": "option-value-type"
CHECK-NOT: unrelated.nix
CHECK-COUNT-1: "code": "option-provider-eval"
CHECK: "message": "option provider `nixos` failed to evaluate: -32001: syntax error, unexpected ID, expecting '.' or '='"
CHECK: "end": {
CHECK-NEXT: "character": 3,
CHECK-NEXT: "line": 5
CHECK: "start": {
CHECK-NEXT: "character": 2,
CHECK-NEXT: "line": 5
CHECK: "uri": "file://{{.*}}/bad-provider.nix"
CHECK-NOT: unrelated.nix
```
