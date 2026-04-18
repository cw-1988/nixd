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
  services.example.enable = 123;
}
EOF

MODULE_URI="file://$MODULE"
CONFIG_URI="file://$CONFIG"
EXPR="(let pkgs = import <nixpkgs> { }; lib = pkgs.lib; in (lib.evalModules { modules = [ $CONFIG ]; }).options)"

{
  cat <<EOF
<-- initialize(0)

\`\`\`json
{"jsonrpc":"2.0","id":0,"method":"initialize","params":{"processId":123,"rootPath":"","capabilities":{"workspace":{"didChangeWatchedFiles":{"dynamicRegistration":true}}},"trace":"off"}}
\`\`\`

<-- initialized

\`\`\`json
{"jsonrpc":"2.0","method":"initialized","params":{}}
\`\`\`

<-- textDocument/didOpen

\`\`\`nix $CONFIG_URI
{ ... }: {
  imports = [ $MODULE ];
  services.example.enable = 123;
}
\`\`\`
EOF

  sleep 1

  cat > "$MODULE" <<'EOF'
{ ... }: { }
EOF

  cat <<EOF

<-- textDocument/didSave

\`\`\`json
{"jsonrpc":"2.0","method":"textDocument/didSave","params":{"textDocument":{"uri":"$MODULE_URI"}}}
\`\`\`

<-- workspace/didChangeWatchedFiles

\`\`\`json
{"jsonrpc":"2.0","method":"workspace/didChangeWatchedFiles","params":{"changes":[{"uri":"$MODULE_URI","type":2}]}}
\`\`\`
EOF

  sleep 1

  cat <<'EOF'

\`\`\`json
{"jsonrpc":"2.0","method":"exit"}
\`\`\`
EOF
} | nixd --lit-test --nixos-options-expr="$EXPR"
