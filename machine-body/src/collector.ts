import Database from "better-sqlite3";
import {
  findMatchingAdbDevice,
  getAdbDevices,
  getTailscalePeers,
  interpretPingResult,
  interpretSshResult,
  pingHost,
  sshCheckNode,
  type AdbDevice,
  type AdbSnapshot,
  type PeerSnapshot,
  type TailscalePeer,
} from "./actions.js";
import { type MachineBodyConfig, type NodeConfig } from "./config.js";
import { upsertNodeState, type NodeStateRecord } from "./db.js";

export type CollectedNodeStatus = {
  name: string;
  roles: string[];
  lan: string | null;
  tailscale: string | null;
  lanReachable: boolean | null;
  tailscaleOnline: boolean | null;
  sshReachable: boolean | null;
  adbPresent: boolean | null;
  adbSerial: string | null;
  preferredTarget: string | null;
  lastUpdatedAt: string;
  reachable: boolean | null;
  details: {
    tailscalePeer: TailscalePeer | null;
    adbDevice: AdbDevice | null;
    probeErrors: {
      lanPing: string | null;
      tailscale: string | null;
      ssh: string | null;
      adb: string | null;
    };
  };
};

export type CollectionSnapshot = {
  nodes: CollectedNodeStatus[];
  tailscalePeers: TailscalePeer[];
  adbDevices: AdbDevice[];
  collectedAt: string;
};

export async function collectAndPersist(
  config: MachineBodyConfig,
  db: Database.Database,
): Promise<CollectionSnapshot> {
  const [tailscaleSnapshot, adbSnapshot] = await Promise.all([
    getTailscalePeers(),
    getAdbDevices(),
  ]);

  const nodes: CollectedNodeStatus[] = [];
  for (const node of config.nodes) {
    const status = await collectNodeStatus(node, tailscaleSnapshot, adbSnapshot);
    nodes.push(status);
    upsertNodeState(db, toStateRecord(status));
  }

  return {
    nodes,
    tailscalePeers: tailscaleSnapshot.peers,
    adbDevices: adbSnapshot.devices,
    collectedAt: new Date().toISOString(),
  };
}

async function collectNodeStatus(
  node: NodeConfig,
  tailscaleSnapshot: PeerSnapshot,
  adbSnapshot: AdbSnapshot,
): Promise<CollectedNodeStatus> {
  const tailscalePeer = findTailscalePeer(node, tailscaleSnapshot.peers);
  const adbDevice = node.roles.includes("adb") ? findMatchingAdbDevice(node, adbSnapshot.devices) : null;

  const pingResult = node.lan ? await pingHost(node.lan) : null;
  const sshResult = node.roles.includes("ssh") ? await sshCheckNode(node) : null;
  const lanReachable = pingResult ? interpretPingResult(pingResult) : null;
  const tailscaleOnline = tailscaleSnapshot.available
    ? tailscalePeer
      ? tailscalePeer.online
      : node.tailscale
        ? false
        : null
    : null;
  const sshReachable = sshResult ? interpretSshResult(sshResult) : null;
  const preferredTarget = pickPreferredTarget(node, tailscaleOnline);
  const adbPresent = node.roles.includes("adb")
    ? adbSnapshot.available
      ? adbDevice
        ? adbDevice.state === "device"
        : false
      : null
    : null;
  const lastUpdatedAt = new Date().toISOString();

  return {
    name: node.name,
    roles: node.roles,
    lan: node.lan ?? null,
    tailscale: node.tailscale ?? null,
    lanReachable,
    tailscaleOnline,
    sshReachable,
    adbPresent,
    adbSerial: adbDevice?.serial ?? null,
    preferredTarget,
    lastUpdatedAt,
    reachable: summarizeReachability(lanReachable, tailscaleOnline, sshReachable, adbPresent),
    details: {
      tailscalePeer,
      adbDevice,
      probeErrors: {
        lanPing: pingResult && lanReachable === null ? pingResult.stderr || pingResult.stdout || "ping failed" : null,
        tailscale:
          node.tailscale !== undefined && !tailscaleSnapshot.available ? tailscaleSnapshot.error : null,
        ssh: sshResult && sshReachable === null ? sshResult.stderr || sshResult.stdout || "ssh check failed" : null,
        adb: node.roles.includes("adb") && !adbSnapshot.available ? adbSnapshot.error : null,
      },
    },
  };
}

function toStateRecord(status: CollectedNodeStatus): NodeStateRecord {
  return {
    name: status.name,
    lanReachable: status.lanReachable,
    tailscaleOnline: status.tailscaleOnline,
    sshReachable: status.sshReachable,
    adbPresent: status.adbPresent,
    adbSerial: status.adbSerial,
    preferredTarget: status.preferredTarget,
    lastUpdatedAt: status.lastUpdatedAt,
    detailsJson: JSON.stringify(status.details),
  };
}

function findTailscalePeer(node: NodeConfig, peers: TailscalePeer[]): TailscalePeer | null {
  return (
    peers.find((peer) => peer.name === node.name) ??
    peers.find((peer) => node.tailscale !== undefined && peer.tailscaleIps.includes(node.tailscale)) ??
    null
  );
}

function pickPreferredTarget(node: NodeConfig, tailscaleOnline: boolean | null): string | null {
  if (tailscaleOnline && node.tailscale) {
    return node.tailscale;
  }

  return node.lan ?? node.tailscale ?? null;
}

function summarizeReachability(...values: Array<boolean | null>): boolean | null {
  if (values.some((value) => value === true)) {
    return true;
  }

  if (values.some((value) => value === false)) {
    return false;
  }

  return null;
}
