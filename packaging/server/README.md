# ArbiterAI Server

`arbiterAI-server` is an OpenAI-compatible HTTP server for cloud LLM providers (OpenAI, Anthropic,
DeepSeek, OpenRouter) and local llama.cpp models, with model management, telemetry and a live
dashboard. This archive is a release build for Linux x86-64.

## Contents

```
bin/arbiterAI-server                          the server
share/arbiterAI/install.json                  install marker: version, asset name, platform
share/arbiterAI/schemas/model_config.schema.json
README.md
LICENSE
```

Keep the layout as it is. `share/arbiterAI/install.json` marks the directory as an install of this
release, next to `bin/`.

## Requirements

- Linux x86-64 with **glibc 2.38 or newer** and the GCC 13 C++ runtime (`libstdc++6`): Ubuntu 24.04+,
  Debian 13+ or similar.
- `libvulkan1` and `libgomp1`:

  ```bash
  sudo apt install libvulkan1 libgomp1
  ```

- For GPU inference, a Vulkan driver (`mesa-vulkan-drivers` for AMD/Intel, the NVIDIA driver for
  NVIDIA). Without one the server runs on the CPU.

## Quick start

```bash
tar -xzf arbiterAI-server-vX.Y.Z-linux-x64.tar.gz
cd arbiterAI-server-vX.Y.Z-linux-x64

cat > server.json <<'EOF'
{
    "host": "0.0.0.0",
    "port": 8080,
    "models_dir": "./models"
}
EOF

./bin/arbiterAI-server --self-test -c server.json   # check the config first
./bin/arbiterAI-server -c server.json
```

Then open `http://localhost:8080/dashboard`, or call `http://localhost:8080/v1/chat/completions`
with any OpenAI client.

## Command line

| Option | Description |
|--------|-------------|
| `-c, --config <path>` | Server configuration file (required) |
| `--self-test` | With `-c`: check that the server would start (parse the config, load model configs, detect hardware), then exit `0`, or `1` with the reason on stderr. Binds no port, loads no models and writes nothing. |
| `--version` | Print the version and exit |
| `-h, --help` | Print usage |

## Configuration file

All settings live in the JSON file given with `-c`. A fuller example:

```json
{
    "host": "0.0.0.0",
    "port": 8080,
    "model_config_paths": ["config"],
    "models_dir": "/models",
    "override_path": "",
    "injected_config_dir": "",
    "max_concurrent_downloads": 2,
    "storage": {
        "limit": "0",
        "cleanup_enabled": true,
        "cleanup_max_age_days": 30,
        "cleanup_interval_hours": 24
    },
    "logging": {
        "level": "info",
        "directory": "",
        "rotate_hour": 0,
        "retain_days": 7
    },
    "updates": {
        "drain_timeout_s": 60
    }
}
```

The full reference is in
[docs/server.md](https://github.com/caseymcc/arbiterAI/blob/main/docs/server.md).

Things to know before you run it as a service:

- **The working directory is written to.** On every start the server clones, or fetches, the
  [arbiterAI_config](https://github.com/caseymcc/arbiterAI_config) repository of default model
  configs into `./arbiterAI_config` in its working directory. Run it from a directory its user can
  write, not from `/` or from the install directory. Relative paths in the config file
  (`model_config_paths` defaults to `config`) are relative to that directory too.
- **The config file is rewritten in place.** Saving on the dashboard's config page writes the
  server's config file. The user the server runs as must be able to write that file.
- **Writable paths belong outside the install directory.** Point `models_dir`, `override_path`,
  `injected_config_dir` and `logging.directory` at a data directory, not at this archive's
  directory: an update replaces the install directory.
- **Stopping.** On `SIGTERM` or `SIGINT` the server answers new requests with `503`, gives running
  requests, inference jobs and downloads up to `updates.drain_timeout_s` seconds, unloads its models,
  logs `shutdown complete` and exits `0`. A second signal exits at once.

## API keys

Cloud providers read their key from `<PROVIDER>_API_KEY` in the server's environment:

| Variable | Provider |
|----------|----------|
| `OPENAI_API_KEY` | OpenAI |
| `ANTHROPIC_API_KEY` | Anthropic |
| `DEEPSEEK_API_KEY` | DeepSeek |
| `OPENROUTER_API_KEY` | OpenRouter |

Local llama.cpp models need no key; their files are downloaded into `models_dir` when first loaded.
