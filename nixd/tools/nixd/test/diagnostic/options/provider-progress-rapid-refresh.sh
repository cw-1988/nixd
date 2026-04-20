#!/bin/sh

set -eu

TMPDIR_PATH="${1:-$(mktemp -d)}"
mkdir -p "$TMPDIR_PATH"

CONFIG="$TMPDIR_PATH/configuration.nix"
MODULE="$TMPDIR_PATH/module.nix"

cat > "$MODULE" <<'EOF'
{ lib, ... }: {
  options.services.example.enable = lib.mkOption {
    type = lib.types.bool;
  };
}
EOF

cat > "$CONFIG" <<EOF
{ ... }: {
  imports = [ $MODULE ];
  services.example.enable = true;
}
EOF

MODULE_URI="file://$MODULE"
CONFIG_URI="file://$CONFIG"
EXPR="(let pkgs = import <nixpkgs> { }; lib = pkgs.lib; in (lib.evalModules { modules = [ $CONFIG ]; }).options)"

{
  cat <<EOF
<-- initialize(0)

\`\`\`json
{"jsonrpc":"2.0","id":0,"method":"initialize","params":{"processId":123,"rootPath":"","capabilities":{"window":{"workDoneProgress":true}},"trace":"off"}}
\`\`\`

<-- textDocument/didOpen

\`\`\`nix $CONFIG_URI
{ ... }: {
  imports = [ $MODULE ];
  services.example.enable = true;
}
\`\`\`
EOF

  cat > "$MODULE" <<'EOF'
{ lib, ... }: {
  options.services.example.enable = lib.mkOption {
    type = lib.types.bool;
  };
  options.services.example.extra = lib.mkOption {
    type = lib.types.bool;
  };
}
EOF

  cat <<EOF

<-- textDocument/didSave

\`\`\`json
{"jsonrpc":"2.0","method":"textDocument/didSave","params":{"textDocument":{"uri":"$MODULE_URI"}}}
\`\`\`
EOF

  sleep 2

  cat <<'EOF'

```json
{"jsonrpc":"2.0","method":"exit"}
```
EOF
} | nixd --lit-test --nixos-options-expr="$EXPR"
