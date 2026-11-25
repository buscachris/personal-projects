import { type FastifyInstance } from "fastify";
import Database from "better-sqlite3";
import {
  getAdbDevicesResult,
  getTailscaleStatusResult,
  pingHost,
  sshCheckNode,
} from "./actions.js";
import { collectAndPersist } from "./collector.js";
import { type MachineBodyConfig, type NodeConfig } from "./config.js";
import { runDoctor } from "./doctor.js";
import { getNodeState, insertActionLog, listActionLogs, listNodeStates, mergeNodesWithState } from "./db.js";
import { serializeNodeRecord, serializeNodeState } from "./serializers.js";
import { buildSummary } from "./summary.js";

export type RouteContext = {
  config: MachineBodyConfig;
  db: Database.Database;
};

export async function registerRoutes(app: FastifyInstance, context: RouteContext): Promise<void> {
  app.get("/nodes", async () => {
    return {
      nodes: mergeNodesWithState(context.config.nodes, listNodeStates(context.db)).map((node) =>
        serializeNodeRecord(node),
      ),
    };
  });

  app.get("/nodes/:node", async (request, reply) => {
    const node = findNode(context.config.nodes, (request.params as { node: string }).node);
    if (!node) {
      reply.code(404);
      return { error: "Node not found." };
    }

    const state = getNodeState(context.db, node.name);
    return {
      node: {
        ...node,
        state: serializeNodeState(state),
      },
    };
  });

  app.get("/status", async () => {
    return collectAndPersist(context.config, context.db);
  });

  app.get("/summary", async () => {
    return buildSummary(await collectAndPersist(context.config, context.db));
  });

  app.get("/doctor", async () => {
    return runDoctor();
  });

  app.get("/logs", async (request) => {
    const limit = parseLimit((request.query as { limit?: string }).limit);
    return {
      logs: listActionLogs(context.db, limit),
    };
  });

  app.post("/actions/ping/:node", async (request, reply) => {
    const node = findNode(context.config.nodes, (request.params as { node: string }).node);
    if (!node || !node.lan) {
      reply.code(404);
      return { error: "Node not found or has no LAN address." };
    }

    const result = await pingHost(node.lan);
    logAction(context.db, "ping", node.name, result.ok, combineOutput(result));
    return { node: node.name, host: node.lan, result };
  });

  app.post("/actions/ssh-check/:node", async (request, reply) => {
    const node = findNode(context.config.nodes, (request.params as { node: string }).node);
    if (!node) {
      reply.code(404);
      return { error: "Node not found." };
    }

    const result = await sshCheckNode(node);
    logAction(context.db, "ssh-check", node.name, result.ok, combineOutput(result));
    return { node: node.name, result };
  });

  app.post("/actions/tailscale-status", async () => {
    const result = await getTailscaleStatusResult();
    logAction(context.db, "tailscale-status", null, result.ok, combineOutput(result));
    return { result };
  });

  app.post("/actions/adb-devices", async () => {
    const result = await getAdbDevicesResult();
    logAction(context.db, "adb-devices", null, result.ok, combineOutput(result));
    return { result };
  });
}

function findNode(nodes: NodeConfig[], name: string): NodeConfig | undefined {
  return nodes.find((node) => node.name === name);
}

function logAction(
  db: Database.Database,
  action: string,
  nodeName: string | null,
  ok: boolean,
  output: string,
): void {
  insertActionLog(db, {
    action,
    nodeName,
    ok,
    output,
    createdAt: new Date().toISOString(),
  });
}

function combineOutput(result: { stdout: string; stderr: string }): string {
  return [result.stdout, result.stderr].filter((part) => part.length > 0).join("\n");
}

function parseLimit(raw: string | undefined): number {
  const parsed = Number.parseInt(raw ?? "20", 10);
  if (!Number.isFinite(parsed)) {
    return 20;
  }

  return parsed;
}
