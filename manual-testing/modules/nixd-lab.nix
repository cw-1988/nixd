{
  config,
  lib,
  pkgs,
  ...
}:

let
  inherit (lib)
    mkEnableOption
    mkIf
    mkOption
    types
    ;

  scalarValue = types.oneOf [
    types.bool
    types.int
    types.str
    (types.listOf types.str)
  ];

  freeformModule = types.submodule {
    freeformType = types.attrsOf types.anything;
    options.known = mkOption {
      type = types.int;
      default = 0;
      description = "Known integer inside an otherwise open submodule.";
    };
  };

  routeModule = types.submodule {
    options = {
      path = mkOption {
        type = types.strMatching "^/";
        description = "HTTP route path.";
      };

      upstream = mkOption {
        type = types.str;
        description = "Backend URL.";
      };

      methods = mkOption {
        type = types.listOf (
          types.enum [
            "GET"
            "POST"
            "PUT"
            "PATCH"
            "DELETE"
            "HEAD"
          ]
        );
        default = [ "GET" ];
        description = "Allowed HTTP methods.";
      };
    };
  };

  workerModule = types.submodule (
    { name, ... }:
    {
      options = {
        name = mkOption {
          type = types.str;
          default = name;
          description = "Stable worker name.";
        };

        role = mkOption {
          type = types.enum [
            "web"
            "queue"
            "cron"
            "database"
          ];
          description = "Worker role.";
        };

        replicas = mkOption {
          type = types.ints.positive;
          default = 1;
          description = "Number of replicas.";
        };

        port = mkOption {
          type = types.port;
          description = "Worker listen port.";
        };

        routes = mkOption {
          type = types.listOf routeModule;
          default = [ ];
          description = "Routes served by this worker.";
        };
      };
    }
  );

  rolloutModule = types.submodule {
    options = {
      name = mkOption {
        type = types.nonEmptyStr;
        description = "Rollout lane name.";
      };

      weight = mkOption {
        type = types.ints.between 0 100;
        description = "Traffic weight.";
      };

      zones = mkOption {
        type = types.nonEmptyListOf types.nonEmptyStr;
        description = "Availability zones for this lane.";
      };
    };
  };

  strictModule = types.submodule {
    options = {
      required = mkOption {
        type = types.str;
        description = "Required string for required-option diagnostics.";
      };

      known = mkOption {
        type = types.int;
        default = 1;
        description = "Known integer in a strict module.";
      };

      settings = mkOption {
        type = types.attrsOf types.int;
        default = { };
        description = "Dynamic integer settings.";
      };
    };
  };

  cfg = config.services.nixdLab;
in
{
  options.services.nixdLab = {
    enable = mkEnableOption "the nixd complex fixture";

    mode = mkOption {
      type = types.enum [
        "dev"
        "staging"
        "prod"
      ];
      default = "dev";
      description = "Fixture mode for enum completion and diagnostics.";
    };

    package = mkOption {
      type = types.package;
      default = pkgs.hello;
      description = "Primary package option.";
    };

    pluginPackages = mkOption {
      type = types.listOf types.package;
      default = [ ];
      description = "List of package values.";
    };

    packageOrChart = mkOption {
      type = types.either types.package types.path;
      description = "Either a package or a chart path.";
    };

    storeThing = mkOption {
      type = types.pathInStore;
      description = "Path-like value that must live in the Nix store.";
    };

    absolutePath = mkOption {
      type = types.pathWith {
        absolute = true;
        inStore = false;
      };
      description = "Absolute non-store path.";
    };

    relativePath = mkOption {
      type = types.path;
      description = "Path option that accepts a normal Nix path literal.";
    };

    tokenFile = mkOption {
      type = types.nullOr types.path;
      default = null;
      description = "Nullable secret file path.";
    };

    listenPort = mkOption {
      type = types.coercedTo types.str lib.toInt types.port;
      default = 8080;
      description = "Port accepting either an integer or a string coercible to one.";
    };

    launchOrder = mkOption {
      type = types.listOf (
        types.nullOr (
          types.enum [
            "database"
            "api"
            "worker"
            "metrics"
          ]
        )
      );
      default = [ ];
      description = "List navigation over nullOr enum values.";
    };

    featureFlags = mkOption {
      type = types.attrsOf types.bool;
      default = { };
      description = "Dynamic boolean feature flags.";
    };

    # env = mkOption {
    #   type = types.attrsOf types.str;
    #   default = { };
    #   description = "Environment variables.";
    # };

    enumByHost = mkOption {
      type = types.attrsOf (
        types.enum [
          "fast"
          "safe"
          "experimental"
        ]
      );
      default = { };
      description = "Dynamic attributes whose values are enums.";
    };

    workers = mkOption {
      type = types.attrsOf workerModule;
      default = { };
      description = "Dynamic worker submodules.";
    };

    matrix = mkOption {
      type = types.listOf rolloutModule;
      default = [ ];
      description = "Plain list of submodules.";
    };

    objectMatrix = mkOption {
      type = types.loaOf rolloutModule;
      default = { };
      description = "List-or-attrs-of submodules.";
    };

    nested = mkOption {
      type = strictModule;
      description = "Strict submodule with a required field.";
    };

    openSettings = mkOption {
      type = freeformModule;
      default = { };
      description = "Submodule with freeform dynamic leaves.";
    };

    lazySettings = mkOption {
      type = types.lazyAttrsOf scalarValue;
      default = { };
      description = "Lazy dynamic settings.";
    };

    eitherListOrString = mkOption {
      type = types.either (types.listOf types.str) types.str;
      default = [ ];
      description = "Either list-of-string or string.";
    };

    oneOfValue = mkOption {
      type = types.oneOf [
        freeformModule
        types.str
        (types.attrsOf types.int)
      ];
      default = { };
      description = "oneOf wrapper that includes a freeform submodule.";
    };

    nullOrFreeform = mkOption {
      type = types.nullOr freeformModule;
      default = null;
      description = "Nullable freeform submodule.";
    };

    uniquePorts = mkOption {
      type = types.uniq (types.listOf types.port);
      default = [ ];
      description = "Unique wrapper around a list of ports.";
    };

    functionResult = mkOption {
      type = types.functionTo types.int;
      default = args: args.base;
      description = "Function option returning an integer.";
    };

    moduleAsLambda = mkOption {
      type = strictModule;
      description = "Submodule provided as a lambda.";
    };
  };

  config = mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ] ++ cfg.pluginPackages;

    systemd.tmpfiles.rules = [
      "d /run/nixd-lab 0755 root root -"
    ];
  };
}
