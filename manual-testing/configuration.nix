{
  config,
  lib,
  pkgs,
  ...
}:

let
  mkHttpWorker = name: port: {
    inherit name port;
    role = "web";
    replicas = 2;
    routes = [
      {
        path = "/";
        upstream = "http://127.0.0.1:${toString port}";
      }
      {
        path = "/healthz";
        upstream = "http://127.0.0.1:${toString port}/healthz";
        methods = [
          "GET"
          "HEAD"
        ];
      }
    ];
  };
in
{
  imports = [
    ./modules/nixd-lab.nix
    ./profiles/edge-node.nix
  ];

  networking.hostName = "nixd-lab-control";

  users.users.root.openssh.authorizedKeys.keys = [
    "ssh-ed25519 AAAA...your-key..."
  ];

  services.k3s = {
    enable = true;
    role = "server";
    clusterInit = true;

    charts = {
      nginx = ./charts/my-nginx-chart.tgz;
      redis = ./charts/my-redis-chart.tgz;
      "observability.prometheus" = pkgs.hello;
      "dynamic-${config.networking.hostName or "control"}" = ./charts/dynamic-chart.tgz;
    };
  };

  services.nixdLab = {
    enable = true;
    mode = "staging";
    package = pkgs.curl;
    pluginPackages = with pkgs; [
      jq
      ripgrep
      fd
    ];
    packageOrChart = ./charts/my-nginx-chart.tgz;
    storeThing = "${pkgs.hello}";
    absolutePath = "/etc/hosts";
    relativePath = ./payloads/service.json;
    tokenFile = ./secrets/token.txt;
    listenPort = 9443;
    launchOrder = [
      "database"
      null
      "api"
      "worker"
    ];

    featureFlags = {
      rollout = true;
      shadowTraffic = false;
      "dynamic-name-${toString 7}" = true;
    };

    env = {
      LOG_LEVEL = "debug";
      NIXD_COMPLEX_FIXTURE = "true";
    };

    enumByHost = {
      control = "fast";
      edge-a = "safe";
      edge-b = "experimental";
    };

    workers = {
      api = mkHttpWorker "api" 8080;
      queue = {
        name = "queue";
        role = "queue";
        replicas = 3;
        port = 9090;
        routes = [
          {
            path = "/jobs";
            upstream = "http://127.0.0.1:9090/jobs";
            methods = [ "POST" ];
          }
        ];
      };
      cron = {
        name = "cron";
        role = "cron";
        replicas = 1;
        port = 7070;
        routes = [ ];
      };
    };

    matrix = [
      {
        name = "blue";
        weight = 80;
        zones = [
          "us-east-1a"
          "us-east-1b"
        ];
      }
      {
        name = "green";
        weight = 20;
        zones = [ "us-west-2a" ];
      }
    ];

    objectMatrix = {
      canary = {
        name = "canary";
        weight = 5;
        zones = [ "eu-central-1a" ];
      };
      baseline = {
        name = "baseline";
        weight = 95;
        zones = [
          "eu-central-1b"
          "eu-central-1c"
        ];
      };
    };

    nested = {
      required = "present";
      known = 42;
      settings = {
        retries = 4;
        timeout = 15;
      };
    };

    openSettings = {
      known = 100;
      arbitrary.deep.value = "accepted by freeformType";
      another.dynamic.leaf = "also accepted";
    };

    lazySettings = {
      answer = 42;
      enabled = true;
      message = "hello from lazy attrs";
      tags = [
        "nixd"
        "schema"
      ];
    };

    eitherListOrString = [
      "alpha"
      "beta"
    ];

    oneOfValue = {
      known = 77;
      random.extra = "accepted through oneOf freeform submodule";
    };

    nullOrFreeform = {
      known = 12;
      still.dynamic = "accepted";
    };

    uniquePorts = [
      8080
      9090
      9443
    ];

    functionResult = args: args.base + 5;

    moduleAsLambda =
      { config, ... }:
      {
        required = "lambda module";
        known = config.settings.retries + 1;
        settings.retries = 6;
      };
  };

  system.stateVersion = "24.11";

  # Paste snippets from ./bad-examples.nix here when you want diagnostics.
  assertions = [
    {
      assertion = lib.length config.services.nixdLab.uniquePorts >= 3;
      message = "the lab keeps several ports around for list navigation tests";
    }
  ];
}
