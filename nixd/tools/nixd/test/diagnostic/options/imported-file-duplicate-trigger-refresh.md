# RUN: sh %S/imported-file-duplicate-trigger-refresh.sh %t | FileCheck %s

```
CHECK: "code": "option-value-type"
CHECK: "message": "value for option `services.example.enable` has type `integer`, expected `bool boolean`"
CHECK-COUNT-1: "code": "option-provider-eval"
CHECK: "message": "option provider `nixos` failed to evaluate:
CHECK-SAME: The option `services' does not exist.
```
