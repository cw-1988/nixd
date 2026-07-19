# Agent notes

## Local build workflow

Use the Meson build directory for local nixd development. Do not use
`nix build .#nixd` as the normal inner-loop build; it rebuilds through the Nix
package path and is much slower for small C++ edits.

Initial setup, if `build/` does not exist:

```sh
nix develop -c meson setup build
```

Build the language server and test worker:

```sh
nix develop -c ninja -C build nixd/tools/nixd nixd/tools/nixd-attrset-eval
```

Run focused lit tests against the Meson-built binaries:

```sh
env PATH="$PWD/build/nixd/tools:$PATH" \
  MESON_BUILD_ROOT="$PWD/build/nixd/tools" \
  NIXD_ATTRSET_EVAL="$PWD/build/nixd/tools/nixd-attrset-eval" \
  nix develop -c lit -vv <test-files>
```

For the infra-test workspace, NixIDE should point at:

```text
/home/nixos/projects/nixd/build/nixd/tools/nixd
```
