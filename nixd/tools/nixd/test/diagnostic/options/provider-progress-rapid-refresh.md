# RUN: sh %S/provider-progress-rapid-refresh.sh %t | FileCheck %s

```
CHECK: "title": "evaluating nixos"
CHECK: "message": "evaluated nixos"
```
