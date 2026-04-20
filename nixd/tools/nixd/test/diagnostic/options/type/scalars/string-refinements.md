# RUN: nixd --lit-test \
# RUN: --nixos-options-expr='{ services.example.nonEmptyGood = { _type = "option"; type = { name = "nonEmptyStr"; description = "non-empty string"; }; }; services.example.nonEmptyBad = { _type = "option"; type = { name = "nonEmptyStr"; description = "non-empty string"; }; }; services.example.passwdGood = { _type = "option"; type = { name = "passwdEntry str"; description = "string, not containing newlines or colons"; }; }; services.example.passwdBad = { _type = "option"; type = { name = "passwdEntry str"; description = "string, not containing newlines or colons"; }; }; services.example.unitTemplateGood = { _type = "option"; type = { name = "systemdUnitName"; description = "systemd unit name"; }; }; services.example.unitInstanceGood = { _type = "option"; type = { name = "systemdUnitName"; description = "systemd unit name"; }; }; services.example.unitNoSuffixBad = { _type = "option"; type = { name = "systemdUnitName"; description = "systemd unit name"; }; }; services.example.unitSlashBad = { _type = "option"; type = { name = "systemdUnitName"; description = "systemd unit name"; }; }; services.example.pattern = { _type = "option"; type = { name = "strMatching"; description = "string matching digits"; functor.payload = "\\d+"; }; }; services.example.literalPatternGood = { _type = "option"; type = { name = "strMatching"; description = "string matching abc"; functor.payload = "abc"; }; }; services.example.literalPatternBad = { _type = "option"; type = { name = "strMatching"; description = "string matching abc"; functor.payload = "abc"; }; }; }' \
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

```nix file:///basic.nix
{
  services.example.nonEmptyGood = "x";
  services.example.nonEmptyBad = "";
  services.example.passwdGood = "root";
  services.example.passwdBad = "root:x";
  services.example.unitTemplateGood = "backup@.service";
  services.example.unitInstanceGood = "backup@daily.service";
  services.example.unitNoSuffixBad = "nginx";
  services.example.unitSlashBad = "foo/bar.service";
  services.example.pattern = "abc";
  services.example.literalPatternGood = "abc";
  services.example.literalPatternBad = "abd";
}
```

```
CHECK-NOT: value for option `services.example.nonEmptyGood`
CHECK: "message": "value for option `services.example.nonEmptyBad` has type `string`, expected `nonEmptyStr non-empty string`"
CHECK-NOT: value for option `services.example.passwdGood`
CHECK: "message": "value for option `services.example.passwdBad` has type `string`, expected `passwdEntry str string, not containing newlines or colons`"
CHECK-NOT: value for option `services.example.unitTemplateGood`
CHECK-NOT: value for option `services.example.unitInstanceGood`
CHECK: "message": "value for option `services.example.unitNoSuffixBad` has type `string`, expected `systemdUnitName systemd unit name`"
CHECK: "message": "value for option `services.example.unitSlashBad` has type `string`, expected `systemdUnitName systemd unit name`"
CHECK-NOT: value for option `services.example.pattern`
CHECK-NOT: value for option `services.example.literalPatternGood`
CHECK: "message": "value for option `services.example.literalPatternBad` has type `string`, expected `strMatching string matching abc`"
```

<-- nixd/waitForOptionsSettled(999)

```json
{
  "jsonrpc": "2.0",
  "id": 999,
  "method": "nixd/waitForOptionsSettled",
  "params": null
}
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
