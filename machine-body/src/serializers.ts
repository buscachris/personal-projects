import { type CommandResult } from "./actions.js";
import { type NodeConfig } from "./config.js";
import { type NodeStateRecord } from "./db.js";

export type ParsedNodeState = Omit<NodeStateRecord, "detailsJson"> & {
  details: unknown;
};

export function parseDetailsJson(detailsJson: string | null): unknown {
  if (!detailsJson) {
    return null;
  }

  try {
    return JSON.parse(detailsJson) as unknown;
  } catch {
    return {
      parseError: "Unable to parse detailsJson",
      raw: detailsJson,
    };
  }
}

export function serializeNodeState(state: NodeStateRecord | null): ParsedNodeState | null {
  if (!state) {
    return null;
  }

  return {
    name: state.name,
    lanReachable: state.lanReachable,
    tailscaleOnline: state.tailscaleOnline,
    sshReachable: state.sshReachable,
    adbPresent: state.adbPresent,
    adbSerial: state.adbSerial,
    preferredTarget: state.preferredTarget,
    lastUpdatedAt: state.lastUpdatedAt,
    details: parseDetailsJson(state.detailsJson),
  };
}

export function serializeNodeRecord(node: NodeConfig & { state?: NodeStateRecord | null }) {
  return {
    name: node.name,
    ssh_user: node.ssh_user,
    lan: node.lan,
    tailscale: node.tailscale,
    roles: node.roles,
    state: serializeNodeState(node.state ?? null),
  };
}

export function summarizeCommandResult(result: CommandResult) {
  return {
    ok: result.ok,
    exitCode: result.exitCode,
    durationMs: result.durationMs,
    stderr: result.stderr,
    stdout: result.stdout,
  };
}
