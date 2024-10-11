# To learn more about how to use Nix to configure your environment
# see: https://developers.google.com/idx/guides/customize-idx-env
{ pkgs, ... }: {
  # Which nixpkgs channel to use.
  channel = "stable-24.05"; # or "unstable"

  # Use https://search.nixos.org/packages to find packages
  packages = [
    pkgs.go
    pkgs.jdk17
    pkgs.python311
    pkgs.python311Packages.pip
    pkgs.nodejs_20
    pkgs.nodePackages.nodemon
    pkgs.sqlite
    pkgs.pkg-config
    pkgs.gcc
    pkgs.emscripten
    pkgs.wabt
    pkgs.tcl
    pkgs.zlib.out
    pkgs.zlib.dev
    pkgs.cmake
    pkgs.gnumake
    pkgs.rustup
    pkgs.envsubst
  ];

  # Sets environment variables in the workspace
  env = {};
  idx = {
    # Search for the extensions you want on https://open-vsx.org/ and use "publisher.id"
    extensions = [
      # "vscodevim.vim"
      "Dart-Code.flutter"
      "Dart-Code.dart-code"
    ];

    # Enable previews
     previews = {
      enable = true;
      previews = {
        # web = {
        #   command = ["flutter" "run" "--machine" "-d" "web-server" "--web-hostname" "0.0.0.0" "--web-port" "$PORT"];
        #   manager = "flutter";
        #   cwd = "bindings/dart/example";
        # };
        android = {
          command = ["flutter" "run" "--machine" "-d" "android" "-d" "localhost:5555"];
          manager = "flutter";
          cwd = "bindings/dart/example";
        };
      };
    };

    # Workspace lifecycle hooks
    workspace = {
      # Runs when a workspace is first created
      onCreate = {
        # Example: install JS dependencies from NPM
        # npm-install = "npm install";
        vendor = "./scripts/vendor.sh ";
        make-header = "make sqlite-vec.h";
        make-all = "make all";
      };
      # Runs when the workspace is (re)started
      onStart = {
        # Example: start a background task to watch and re-build backend code
        # watch-backend = "npm run watch-backend";
      };
    };
  };
}
