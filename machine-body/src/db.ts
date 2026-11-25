import fs from "node:fs";
import Database from "better-sqlite3";
import { dataDir, databasePath, type NodeConfig } from "./config.js";

export type NodeStateRecord = {
  name: string;
  lanReachable: boolean | null;
  tailscaleOnline: boolean | null;
  sshReachable: boolean | null;
  adbPresent: boolean | null;
  adbSerial: string | null;
  preferredTarget: string | null;
  lastUpdatedAt: string;
  detailsJson: string;
};

export type ActionLogRecord = {
  action: string;
  nodeName: string | null;
  ok: boolean;
  output: string;
  createdAt: string;
};

export type ActionLogRow = ActionLogRecord & {
  id: number;
};

export function openDatabase(): Database.Database {
  fs.mkdirSync(dataDir, { recursive: true });
  const db = new Database(databasePath);
  db.pragma("journal_mode = WAL");
  db.exec(`
    CREATE TABLE IF NOT EXISTS node_state (
      name TEXT PRIMARY KEY,
      lan_reachable INTEGER,
      tailscale_online INTEGER,
      ssh_reachable INTEGER,
      adb_present INTEGER,
      adb_serial TEXT,
      preferred_target TEXT,
      last_updated_at TEXT NOT NULL,
      details_json TEXT NOT NULL
    );

    CREATE TABLE IF NOT EXISTS action_log (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      action TEXT NOT NULL,
      node_name TEXT,
      ok INTEGER NOT NULL,
      output TEXT NOT NULL,
      created_at TEXT NOT NULL
    );
  `);
  return db;
}

export function upsertNodeState(db: Database.Database, record: NodeStateRecord): void {
  db.prepare(`
    INSERT INTO node_state (
      name,
      lan_reachable,
      tailscale_online,
      ssh_reachable,
      adb_present,
      adb_serial,
      preferred_target,
      last_updated_at,
      details_json
    ) VALUES (
      @name,
      @lanReachable,
      @tailscaleOnline,
      @sshReachable,
      @adbPresent,
      @adbSerial,
      @preferredTarget,
      @lastUpdatedAt,
      @detailsJson
    )
    ON CONFLICT(name) DO UPDATE SET
      lan_reachable = excluded.lan_reachable,
      tailscale_online = excluded.tailscale_online,
      ssh_reachable = excluded.ssh_reachable,
      adb_present = excluded.adb_present,
      adb_serial = excluded.adb_serial,
      preferred_target = excluded.preferred_target,
      last_updated_at = excluded.last_updated_at,
      details_json = excluded.details_json
  `).run({
    name: record.name,
    lanReachable: toSqlBoolean(record.lanReachable),
    tailscaleOnline: toSqlBoolean(record.tailscaleOnline),
    sshReachable: toSqlBoolean(record.sshReachable),
    adbPresent: toSqlBoolean(record.adbPresent),
    adbSerial: record.adbSerial,
    preferredTarget: record.preferredTarget,
    lastUpdatedAt: record.lastUpdatedAt,
    detailsJson: record.detailsJson,
  });
}

export function insertActionLog(db: Database.Database, record: ActionLogRecord): void {
  db.prepare(`
    INSERT INTO action_log (action, node_name, ok, output, created_at)
    VALUES (@action, @nodeName, @ok, @output, @createdAt)
  `).run({
    action: record.action,
    nodeName: record.nodeName,
    ok: record.ok ? 1 : 0,
    output: record.output,
    createdAt: record.createdAt,
  });
}

export function listNodeStates(db: Database.Database): NodeStateRecord[] {
  const rows = db.prepare(`
    SELECT
      name,
      lan_reachable,
      tailscale_online,
      ssh_reachable,
      adb_present,
      adb_serial,
      preferred_target,
      last_updated_at,
      details_json
    FROM node_state
    ORDER BY name
  `).all() as Array<{
    name: string;
    lan_reachable: number | null;
    tailscale_online: number | null;
    ssh_reachable: number | null;
    adb_present: number | null;
    adb_serial: string | null;
    preferred_target: string | null;
    last_updated_at: string;
    details_json: string;
  }>;

  return rows.map((row) => ({
    name: row.name,
    lanReachable: fromSqlBoolean(row.lan_reachable),
    tailscaleOnline: fromSqlBoolean(row.tailscale_online),
    sshReachable: fromSqlBoolean(row.ssh_reachable),
    adbPresent: fromSqlBoolean(row.adb_present),
    adbSerial: row.adb_serial,
    preferredTarget: row.preferred_target,
    lastUpdatedAt: row.last_updated_at,
    detailsJson: row.details_json,
  }));
}

export function getNodeState(db: Database.Database, name: string): NodeStateRecord | null {
  const row = db.prepare(`
    SELECT
      name,
      lan_reachable,
      tailscale_online,
      ssh_reachable,
      adb_present,
      adb_serial,
      preferred_target,
      last_updated_at,
      details_json
    FROM node_state
    WHERE name = ?
  `).all([name])[0] as
    | {
        name: string;
        lan_reachable: number | null;
        tailscale_online: number | null;
        ssh_reachable: number | null;
        adb_present: number | null;
        adb_serial: string | null;
        preferred_target: string | null;
        last_updated_at: string;
        details_json: string;
      }
    | undefined;

  if (!row) {
    return null;
  }

  return {
    name: row.name,
    lanReachable: fromSqlBoolean(row.lan_reachable),
    tailscaleOnline: fromSqlBoolean(row.tailscale_online),
    sshReachable: fromSqlBoolean(row.ssh_reachable),
    adbPresent: fromSqlBoolean(row.adb_present),
    adbSerial: row.adb_serial,
    preferredTarget: row.preferred_target,
    lastUpdatedAt: row.last_updated_at,
    detailsJson: row.details_json,
  };
}

export function listActionLogs(db: Database.Database, limit = 20): ActionLogRow[] {
  const safeLimit = clampLimit(limit);
  const rows = db.prepare(`
    SELECT
      id,
      action,
      node_name,
      ok,
      output,
      created_at
    FROM action_log
    ORDER BY id DESC
    LIMIT ${safeLimit}
  `).all() as Array<{
    id: number;
    action: string;
    node_name: string | null;
    ok: number;
    output: string;
    created_at: string;
  }>;

  return rows.map((row) => ({
    id: row.id,
    action: row.action,
    nodeName: row.node_name,
    ok: row.ok === 1,
    output: row.output,
    createdAt: row.created_at,
  }));
}

export function mergeNodesWithState(nodes: NodeConfig[], states: NodeStateRecord[]) {
  const stateByName = new Map(states.map((state) => [state.name, state]));
  return nodes.map((node) => ({
    ...node,
    state: stateByName.get(node.name) ?? null,
  }));
}

function toSqlBoolean(value: boolean | null): number | null {
  if (value === null) {
    return null;
  }

  return value ? 1 : 0;
}

function fromSqlBoolean(value: number | null): boolean | null {
  if (value === null) {
    return null;
  }

  return value === 1;
}

function clampLimit(value: number): number {
  return Math.min(Math.max(Math.trunc(value), 1), 200);
}
