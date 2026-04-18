{ ... }:

{
  services.nixdLab.featureFlags.fromImportedProfile = true;

  services.nixdLab.workers.edge = {
    role = "web";
    replicas = 1;
    port = 8181;
    routes = [
      {
        path = "/edge";
        upstream = "http://127.0.0.1:8181";
      }
    ];
  };
}
