# These snippets are intentionally not imported. Paste them into configuration.nix
# when you want nixd diagnostics to fire without breaking the default fixture.
{ pkgs, ... }:

{
  services.nixdLab.mode = "banana";
  services.nixdLab.package = "not-a-package";
  services.nixdLab.pluginPackages = [ true ];
  services.nixdLab.packageOrChart = "not-a-path";
  services.nixdLab.storeThing = "/etc";
  services.nixdLab.absolutePath = ./payloads/service.json;
  services.nixdLab.listenPort = "not-a-port";
  services.nixdLab.launchOrder = [ "database" "sidecar" ];
  services.nixdLab.featureFlags.rollout = "yes";
  services.nixdLab.enumByHost.control = "reckless";
  services.nixdLab.workers.api.role = "frontend";
  services.nixdLab.workers.api.replicas = 0;
  services.nixdLab.workers.api.routes = { path = "/bad"; };
  services.nixdLab.matrix = { not = "a-list"; };
  services.nixdLab.objectMatrix.bad.weight = 200;
  services.nixdLab.nested.mystery = 1;
  services.nixdLab.openSettings.known = "wrong";
  services.nixdLab.lazySettings.badPackage = pkgs.hello;
  services.nixdLab.eitherListOrString = true;
  services.nixdLab.oneOfValue.deep.bad = pkgs.hello;
  services.nixdLab.uniquePorts = "not-a-list";
  services.k3s.charts.badString = "banana";
  services.k3s.charts.badBool = true;
}
