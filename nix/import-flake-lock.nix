let
  types = {
    tarball = locked: builtins.fetchTarball locked.url;
    github = locked: builtins.fetchTarball "https://github.com/${locked.owner}/${locked.repo}/archive/${locked.rev}.tar.gz";
  };
  load = locked: types.${locked.type} locked;
  json = builtins.fromJSON (builtins.readFile ../flake.lock);
  nixpkgs = load json.nodes.nixpkgs.locked;
in
  import nixpkgs {}
