# RUN: sh %S/imported-file-save-refresh.sh %t | FileCheck %s

```
CHECK: "code": "option-value-type"
CHECK: "message": "value for option `services.example.enable` has type `integer`, expected `bool boolean`"
CHECK: "uri": "file:///{{.*}}/configuration.nix"
CHECK: "code": "option-provider-eval"
CHECK: "message": "option provider `nixos` failed to evaluate:
CHECK-SAME: The option `services' does not exist.
```
