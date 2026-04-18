{
  description = "nixd issue 805 complex option-schema repro";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";

  outputs =
    { nixpkgs, ... }:
    let
      system = "x86_64-linux";
    in
    {
      nixosConfigurations.control = nixpkgs.lib.nixosSystem {
        inherit system;
        modules = [
          ./configuration.nix
        ];
      };

      nixosConfigurations.controlUnchecked = nixpkgs.lib.nixosSystem {
        inherit system;
        modules = [
          ./configuration.nix
          {
            # Keep option metadata available to nixd even when the fixture
            # intentionally contains unknown options like services.nixdLab.env.
            _module.check = false;
          }
        ];
      };
    };
}
