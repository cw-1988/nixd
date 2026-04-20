#!/bin/sh

set -eu

TMPDIR_PATH="${1:-$(mktemp -d)}"
mkdir -p "$TMPDIR_PATH"

BAD="$TMPDIR_PATH/bad-provider.nix"
OTHER="$TMPDIR_PATH/unrelated.nix"

cat > "$BAD" <<'EOF'
{
  ok = true;
  first = 1;
  second = 2;
  third = 3;
  A
  services.example.enable = true;
}
EOF

cat > "$OTHER" <<'EOF'
{ networking.hostName = "demo"; }
EOF

BAD_URI="file://$BAD"
OTHER_URI="file://$OTHER"

{
  cat <<EOF
<-- initialize(0)

\`\`\`json
{"jsonrpc":"2.0","id":0,"method":"initialize","params":{"processId":123,"rootPath":"","capabilities":{},"trace":"off"}}
\`\`\`

<-- textDocument/didOpen

\`\`\`nix $BAD_URI
{
  ok = true;
  first = 1;
  second = 2;
  third = 3;
  A
  services.example.enable = true;
}
\`\`\`

<-- textDocument/didOpen

\`\`\`nix $OTHER_URI
{ networking.hostName = "demo"; }
\`\`\`

EOF

  sleep 1

  cat <<'EOF'
\`\`\`json
{"jsonrpc":"2.0","method":"exit"}
\`\`\`
EOF
} | nixd --lit-test --nixos-options-expr="import $BAD" \
  | sed 's/}Content-Length:/}\
Content-Length:/g' \
  | awk '
      /^Content-Length:/ {
        if (Block ~ /"code": "option-provider-eval"/) {
          print Block
        }
        Block = ""
        next
      }
      {
        Block = Block $0 "\n"
      }
      END {
        if (Block ~ /"code": "option-provider-eval"/) {
          print Block
        }
      }
    ' \
  | sed '/"version": [0-9][0-9]*/d; s/\("uri": "file:\/\/.*"\),/\1/' \
  | awk 'BEGIN { RS = ""; ORS = "\n\n" } !Seen[$0]++'
