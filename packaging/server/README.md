# ArbiterAI Server

`arbiterAI-server` is an OpenAI-compatible HTTP server for cloud LLM providers (OpenAI, Anthropic,
DeepSeek, OpenRouter) and local llama.cpp models, with model management, telemetry and a live
dashboard. This archive is a release build for Linux x86-64.

## Contents

```
bin/arbiterAI-server                          the server
share/arbiterAI/install.json                  install marker: version, asset name, platform
share/arbiterAI/schemas/model_config.schema.json
share/arbiterAI/systemd/arbiterai-server.service   systemd unit (see "Running under systemd")
share/arbiterAI/examples/server.systemd.json  example config for the systemd unit
share/arbiterAI/examples/server.docker.json   example config for the Docker image
share/arbiterAI/docker/compose.yaml           Docker Compose file (see "Running under Docker")
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

## Running under systemd

The unit in `share/arbiterAI/systemd/arbiterai-server.service` runs
`/opt/arbiterAI/bin/arbiterAI-server -c /etc/arbiterAI/server.json` as the `arbiterai` user in
`/var/lib/arbiterAI`, restarts it when it crashes (`Restart=on-failure`) and gives a stop up to
120 s, longer than the server's drain. On Ubuntu 24.04+ / Debian 13+, as root:

1. Install the runtime libraries and create the service user:

   ```bash
   apt install libvulkan1 libgomp1
   useradd --system --home-dir /var/lib/arbiterAI --shell /usr/sbin/nologin arbiterai
   ```

2. Unpack the release into `/opt/arbiterAI`. Keep this path for every version: updates replace its
   contents, they never rename it.

   ```bash
   mkdir -p /opt/arbiterAI
   tar -xzf arbiterAI-server-vX.Y.Z-linux-x64.tar.gz --strip-components=1 -C /opt/arbiterAI
   ```

3. Give the install directory to the service user; self-update writes there.

   ```bash
   chown -R arbiterai:arbiterai /opt/arbiterAI
   ```

4. Write the config file, writable by the service user (the dashboard's config page saves into it).
   The example keeps every writable path (`models_dir`, `override_path`, `injected_config_dir`)
   under `/var/lib/arbiterAI`, never inside `/opt/arbiterAI`:

   ```bash
   mkdir -p /etc/arbiterAI
   install -m 0644 -o arbiterai -g arbiterai \
       /opt/arbiterAI/share/arbiterAI/examples/server.systemd.json /etc/arbiterAI/server.json
   ```

   API keys go in `/etc/arbiterAI/server.env` (one `NAME=value` per line, see [API keys](#api-keys));
   systemd reads it as root, so it can be `chmod 600` and owned by root.

5. Install and start the unit:

   ```bash
   cp /opt/arbiterAI/share/arbiterAI/systemd/arbiterai-server.service /etc/systemd/system/
   systemctl daemon-reload
   systemctl enable --now arbiterai-server
   ```

Logs go to the journal: `journalctl -u arbiterai-server -f`. `systemctl stop arbiterai-server`
drains and logs `shutdown complete`. The unit adds the user to the `video` and `render` groups for GPU
access; on a system without a `render` group, remove it from `SupplementaryGroups`, or the unit will
not start.

The first start clones `arbiterAI_config` into `/var/lib/arbiterAI`. Until then, a self-test of the
config (`sudo -u arbiterai sh -c 'cd /var/lib/arbiterAI && /opt/arbiterAI/bin/arbiterAI-server
--self-test -c /etc/arbiterAI/server.json'`) finds no model configs and fails, because the self-test
never fetches.

## Running under Docker

The image `ghcr.io/caseymcc/arbiterai-server` holds this same release, unpacked at `/opt/arbiterAI`,
on Ubuntu 24.04 with the runtime libraries and the Mesa Vulkan drivers. Tags: `vX.Y.Z` per release
and `latest`.

- PID 1 is `tini`, which forwards `SIGTERM` to the server for its drain and reaps child processes.
- The working directory is `/config`, a volume: the `arbiterAI_config` clone, `server.json`, model
  overrides and injected configs live there and survive a new container. Model files go in the
  `/models` volume.
- The container runs as root.

With `share/arbiterAI/docker/compose.yaml`:

```bash
mkdir -p arbiterai && cd arbiterai
cp /path/to/share/arbiterAI/docker/compose.yaml .
mkdir -p config models
cp /path/to/share/arbiterAI/examples/server.docker.json config/server.json
docker compose up -d
docker compose logs -f
```

The compose file publishes port 8080, sets `restart: unless-stopped` (a crashed server is
restarted) and `ARBITERAI_SUPERVISOR=docker`, and gives `docker stop` 90 s
(`stop_grace_period`), longer than the server's drain. API keys go in its `environment:` block.

GPU access is off until you uncomment one of the blocks at the end of the compose file:

- **NVIDIA:** `gpus: all`, and `NVIDIA_DRIVER_CAPABILITIES: compute,utility,graphics` in
  `environment:` (Vulkan and NVML come in through the NVIDIA Container Toolkit, which the host
  needs).
- **AMD / Intel:** `devices: [/dev/dri:/dev/dri]`.

Without compose:

```bash
docker run -d --name arbiterai-server --restart unless-stopped --stop-timeout 90 \
    -e ARBITERAI_SUPERVISOR=docker -p 8080:8080 \
    -v "$PWD/config:/config" -v "$PWD/models:/models" \
    ghcr.io/caseymcc/arbiterai-server:latest
```

The self-test works in the image too
(`docker run --rm -v "$PWD/config:/config:ro" ghcr.io/caseymcc/arbiterai-server --self-test -c
/config/server.json`), once a first start has cloned `arbiterAI_config` into `config/`.

## API keys

Cloud providers read their key from `<PROVIDER>_API_KEY` in the server's environment:

| Variable | Provider |
|----------|----------|
| `OPENAI_API_KEY` | OpenAI |
| `ANTHROPIC_API_KEY` | Anthropic |
| `DEEPSEEK_API_KEY` | DeepSeek |
| `OPENROUTER_API_KEY` | OpenRouter |

Local llama.cpp models need no key; their files are downloaded into `models_dir` when first loaded.
