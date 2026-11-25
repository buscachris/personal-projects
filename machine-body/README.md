# machine-body

CLI-first control plane scaffold for a mixed Linux and Android cluster.

## Goals

- Keep the first version read-only.
- Maintain one canonical inventory in `nodes.yaml`.
- Collect reachability and transport status without pretending unknown probes are failures.
- Expose the same state through a CLI and a small HTTP API.

## Commands

Run from the repo root:

```bash
npm run nodes
npm run status
node --import tsx src/index.ts summary
node --import tsx src/index.ts doctor
node --import tsx src/index.ts show workstation
node --import tsx src/index.ts logs 20
node --import tsx src/index.ts ping workstation
node --import tsx src/index.ts ssh-check workstation
node --import tsx src/index.ts tailscale-status
node --import tsx src/index.ts adb-devices
node --import tsx src/index.ts serve
```

JSON output is available for inventory and logs:

```bash
node --import tsx src/index.ts nodes --json
node --import tsx src/index.ts status --json
node --import tsx src/index.ts summary --json
node --import tsx src/index.ts doctor --json
node --import tsx src/index.ts logs 20 --json
```

## HTTP API

When `serve` is running:

- `GET /nodes`
- `GET /nodes/:node`
- `GET /status`
- `GET /summary`
- `GET /doctor`
- `GET /logs?limit=20`
- `POST /actions/ping/:node`
- `POST /actions/ssh-check/:node`
- `POST /actions/tailscale-status`
- `POST /actions/adb-devices`

## Storage

- Inventory: `nodes.yaml`
- SQLite state: `data/machine-body.sqlite`

## Notes

- The collector distinguishes `down` from `unknown`.
- If `ping`, `ssh`, `tailscale`, or `adb` are blocked locally, probe results stay `null` and the CLI shows `?` or `-` instead of inventing outages.
