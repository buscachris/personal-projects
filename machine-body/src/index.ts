import Fastify from "fastify";
import {
  getAdbDevicesResult,
  getTailscaleStatusResult,
  pingHost,
  sshCheckNode,
} from "./actions.js";
import { collectAndPersist, type CollectedNodeStatus } from "./collector.js";
import { loadConfig, type NodeConfig } from "./config.js";
import {
  insertActionLog,
  listActionLogs,
  mergeNodesWithState,
  listNodeStates,
  openDatabase,
} from "./db.js";
import { runDoctor } from "./doctor.js";
import { registerRoutes } from "./routes.js";
import { serializeNodeRecord } from "./serializers.js";
import { buildSummary } from "./summary.js";

async function main(): Promise<void> {
  const command = process.argv[2] ?? "status";
  const args = process.argv.slice(3);
  const jsonOutput = args.includes("--json");
  const positionals = args.filter((arg) => arg !== "--json");
  const config = loadConfig();
  const db = openDatabase();

  switch (command) {
    case "nodes":
      printNodes(mergeNodesWithState(config.nodes, listNodeStates(db)), jsonOutput);
      break;
    case "status":
      await runStatus(config, db, jsonOutput);
      break;
    case "summary":
      await runSummary(config, db, jsonOutput);
      break;
    case "doctor":
      await runDoctorCommand(jsonOutput);
      break;
    case "show":
      await runShow(config.nodes, db, positionals[0]);
      break;
    case "logs":
      runLogs(db, positionals[0], jsonOutput);
      break;
    case "ping":
      await runPing(db, config.nodes, positionals[0]);
      break;
    case "ssh-check":
      await runSshCheck(db, config.nodes, positionals[0]);
      break;
    case "tailscale-status":
      await runCommandOutput(db, "tailscale-status", null, await getTailscaleStatusResult());
      break;
    case "adb-devices":
      await runCommandOutput(db, "adb-devices", null, await getAdbDevicesResult());
      break;
    case "serve":
      await serve(config, db);
      break;
    default:
      printUsage(command);
      process.exitCode = 1;
      break;
  }
}

async function serve(
  config: ReturnType<typeof loadConfig>,
  db: ReturnType<typeof openDatabase>,
): Promise<void> {
  const app = Fastify({ logger: true });
  await registerRoutes(app, { config, db });
  await app.listen({
    host: "127.0.0.1",
    port: 4111,
  });
}

function printNodes(
  rows: Array<{
    name: string;
    roles: string[];
    lan?: string;
    tailscale?: string;
    ssh_user?: string;
  }>,
  jsonOutput: boolean,
): void {
  if (jsonOutput) {
    console.log(JSON.stringify(rows.map((node) => serializeNodeRecord(node)), null, 2));
    return;
  }

  const output = rows.map((node) => ({
    name: node.name,
    roles: node.roles.join(","),
    sshUser: node.ssh_user ?? "-",
    lan: node.lan ?? "-",
    tailscale: node.tailscale ?? "-",
  }));

  console.table(output);
}

function printStatusTable(rows: CollectedNodeStatus[]): void {
  const table = rows.map((node) => ({
    name: node.name,
    roles: node.roles.join(","),
    reach: formatReach(node.reachable),
    lanPing: formatState(node.lanReachable),
    tail: formatState(node.tailscaleOnline),
    ssh: formatState(node.sshReachable),
    adb: formatState(node.adbPresent),
    target: node.preferredTarget ?? "-",
    updated: node.lastUpdatedAt,
  }));

  console.table(table);
}

function formatState(value: boolean | null): string {
  if (value === null) {
    return "-";
  }

  return value ? "ok" : "down";
}

function formatReach(value: boolean | null): string {
  if (value === null) {
    return "?";
  }

  return value ? "yes" : "no";
}

function printUsage(command: string): void {
  console.error(`Unknown command: ${command}`);
  console.error(
    "Usage: node --import tsx src/index.ts [nodes [--json]|status [--json]|summary [--json]|doctor [--json]|show <node>|logs [limit] [--json]|ping <node>|ssh-check <node>|tailscale-status|adb-devices|serve]",
  );
}

async function runShow(
  nodes: NodeConfig[],
  db: ReturnType<typeof openDatabase>,
  nodeName: string | undefined,
): Promise<void> {
  const node = requireNode(nodes, nodeName);
  const snapshot = await collectAndPersist({ nodes }, db);
  const current = snapshot.nodes.find((entry) => entry.name === node.name);

  if (!current) {
    throw new Error(`No collected status found for node: ${node.name}`);
  }

  const detail = {
    name: node.name,
    roles: node.roles,
    sshUser: node.ssh_user ?? null,
    lan: node.lan ?? null,
    tailscale: node.tailscale ?? null,
    preferredTarget: current.preferredTarget,
    lastUpdatedAt: current.lastUpdatedAt,
    reachability: {
      overall: formatReach(current.reachable),
      lanPing: formatState(current.lanReachable),
      tailscale: formatState(current.tailscaleOnline),
      ssh: formatState(current.sshReachable),
      adb: formatState(current.adbPresent),
    },
    details: current.details,
  };

  console.log(JSON.stringify(detail, null, 2));
}

async function runDoctorCommand(jsonOutput: boolean): Promise<void> {
  const report = await runDoctor();
  if (jsonOutput) {
    console.log(JSON.stringify(report, null, 2));
    return;
  }

  const table = report.checks.map((check) => ({
    name: check.name,
    status: check.status,
    ok: check.ok === null ? "?" : check.ok ? "yes" : "no",
    message: check.message,
  }));

  console.log(`doctor overall: ${report.overall}`);
  console.table(table);
}

async function runSummary(
  config: ReturnType<typeof loadConfig>,
  db: ReturnType<typeof openDatabase>,
  jsonOutput: boolean,
): Promise<void> {
  const summary = buildSummary(await collectAndPersist(config, db));
  if (jsonOutput) {
    console.log(JSON.stringify(summary, null, 2));
    return;
  }

  console.log(`summary collected: ${summary.collectedAt}`);
  console.table([
    {
      nodes: summary.totals.nodes,
      reachable: summary.totals.reachable.yes,
      down: summary.totals.reachable.no,
      unknown: summary.totals.reachable.unknown,
    },
  ]);

  const roles = Object.entries(summary.byRole)
    .sort(([left], [right]) => left.localeCompare(right))
    .map(([role, value]) => ({
      role,
      total: value.total,
      reachable: value.reachable.yes,
      down: value.reachable.no,
      unknown: value.reachable.unknown,
    }));

  console.table(roles);
}

function runLogs(
  db: ReturnType<typeof openDatabase>,
  limitRaw: string | undefined,
  jsonOutput: boolean,
): void {
  const limit = parseLimit(limitRaw);
  const logs = listActionLogs(db, limit);
  if (logs.length === 0 && jsonOutput) {
    console.log("[]");
    return;
  }

  if (logs.length === 0) {
    console.log("No action logs recorded yet.");
    return;
  }

  const output = logs.map((entry) => ({
    id: entry.id,
    createdAt: entry.createdAt,
    action: entry.action,
    node: entry.nodeName ?? "-",
    ok: entry.ok ? "yes" : "no",
    output: truncate(entry.output, 80),
  }));

  if (jsonOutput) {
    console.log(JSON.stringify(logs, null, 2));
    return;
  }

  console.table(output);
}

async function runPing(
  db: ReturnType<typeof openDatabase>,
  nodes: NodeConfig[],
  nodeName: string | undefined,
): Promise<void> {
  const node = requireNode(nodes, nodeName);
  if (!node.lan) {
    throw new Error(`Node ${node.name} has no LAN address configured.`);
  }

  const result = await pingHost(node.lan);
  printCommandResult(db, "ping", node.name, `ping ${node.name} (${node.lan})`, result);
}

async function runSshCheck(
  db: ReturnType<typeof openDatabase>,
  nodes: NodeConfig[],
  nodeName: string | undefined,
): Promise<void> {
  const node = requireNode(nodes, nodeName);
  const result = await sshCheckNode(node);
  printCommandResult(
    db,
    "ssh-check",
    node.name,
    `ssh-check ${node.name} (${result.target ?? "unconfigured"})`,
    result,
  );
}

async function runCommandOutput(
  db: ReturnType<typeof openDatabase>,
  action: string,
  nodeName: string | null,
  result: {
    ok: boolean;
    exitCode: number | null;
    stdout: string;
    stderr: string;
    durationMs: number;
  },
): Promise<void> {
  printCommandResult(db, action, nodeName, action, result);
}

function requireNode(nodes: NodeConfig[], nodeName: string | undefined): NodeConfig {
  if (!nodeName) {
    throw new Error("Node name is required.");
  }

  const node = nodes.find((entry) => entry.name === nodeName);
  if (!node) {
    throw new Error(`Unknown node: ${nodeName}`);
  }

  return node;
}

function printCommandResult(
  db: ReturnType<typeof openDatabase>,
  action: string,
  nodeName: string | null,
  label: string,
  result: {
    ok: boolean;
    exitCode: number | null;
    stdout: string;
    stderr: string;
    durationMs: number;
  },
): void {
  insertActionLog(db, {
    action,
    nodeName,
    ok: result.ok,
    output: [result.stdout, result.stderr].filter((part) => part.length > 0).join("\n"),
    createdAt: new Date().toISOString(),
  });
  console.log(`${label}: ${result.ok ? "ok" : "failed"} (${result.durationMs}ms)`);
  if (result.stdout.length > 0) {
    console.log(result.stdout);
  }
  if (result.stderr.length > 0) {
    console.error(result.stderr);
  }
  if (!result.ok) {
    process.exitCode = result.exitCode ?? 1;
  }
}

function parseLimit(raw: string | undefined): number {
  const parsed = Number.parseInt(raw ?? "20", 10);
  if (!Number.isFinite(parsed)) {
    return 20;
  }

  return parsed;
}

function truncate(value: string, maxLength: number): string {
  if (value.length <= maxLength) {
    return value;
  }

  return `${value.slice(0, maxLength - 3)}...`;
}

async function runStatus(
  config: ReturnType<typeof loadConfig>,
  db: ReturnType<typeof openDatabase>,
  jsonOutput: boolean,
): Promise<void> {
  const snapshot = await collectAndPersist(config, db);
  if (jsonOutput) {
    console.log(JSON.stringify(snapshot, null, 2));
    return;
  }

  printStatusTable(snapshot.nodes);
}

void main().catch((error) => {
  console.error(error);
  process.exit(1);
});
