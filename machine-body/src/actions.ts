import { execa } from "execa";
import { type NodeConfig } from "./config.js";

export type CommandResult = {
  ok: boolean;
  exitCode: number | null;
  stdout: string;
  stderr: string;
  durationMs: number;
};

export type TailscalePeer = {
  name: string;
  online: boolean;
  tailscaleIps: string[];
  os: string | null;
};

export type PeerSnapshot = {
  peers: TailscalePeer[];
  available: boolean;
  error: string | null;
};

export type AdbDevice = {
  serial: string;
  state: string;
  details: Record<string, string>;
};

export type AdbSnapshot = {
  devices: AdbDevice[];
  available: boolean;
  error: string | null;
};

export type SshCheckResult = CommandResult & {
  target: string | null;
};

export async function pingHost(host: string): Promise<CommandResult> {
  return runCommand("ping", ["-c", "1", "-W", "1", host]);
}

export async function sshCheckNode(node: NodeConfig): Promise<SshCheckResult> {
  if (!node.ssh_user) {
    return {
      ok: false,
      exitCode: null,
      stdout: "",
      stderr: "Node has no ssh_user configured.",
      durationMs: 0,
      target: null,
    };
  }

  const target = node.tailscale ?? node.lan ?? null;
  if (!target) {
    return {
      ok: false,
      exitCode: null,
      stdout: "",
      stderr: "Node has no SSH target configured.",
      durationMs: 0,
      target: null,
    };
  }

  const result = await runCommand("ssh", [
    "-o",
    "BatchMode=yes",
    "-o",
    "ConnectTimeout=3",
    `${node.ssh_user}@${target}`,
    "true",
  ]);

  return {
    ...result,
    target,
  };
}

export async function getTailscalePeers(): Promise<PeerSnapshot> {
  const result = await runCommand("tailscale", ["status", "--json"]);
  if (!result.ok) {
    return {
      peers: [],
      available: false,
      error: result.stderr || result.stdout || "tailscale status failed",
    };
  }

  try {
    const parsed = JSON.parse(result.stdout) as {
      Peer?: Record<string, { HostName?: string; Online?: boolean; TailscaleIPs?: string[]; OS?: string }>;
    };

    return {
      peers: Object.values(parsed.Peer ?? {})
        .map((peer) => ({
          name: peer.HostName ?? "",
          online: peer.Online ?? false,
          tailscaleIps: peer.TailscaleIPs ?? [],
          os: peer.OS ?? null,
        }))
        .filter((peer) => peer.name.length > 0),
      available: true,
      error: null,
    };
  } catch {
    return {
      peers: [],
      available: false,
      error: "Unable to parse tailscale status JSON.",
    };
  }
}

export async function getAdbDevices(): Promise<AdbSnapshot> {
  const result = await runCommand("adb", ["devices", "-l"]);
  if (!result.ok) {
    return {
      devices: [],
      available: false,
      error: result.stderr || result.stdout || "adb devices failed",
    };
  }

  return {
    devices: result.stdout
      .split("\n")
      .slice(1)
      .map((line) => line.trim())
      .filter((line) => line.length > 0 && !line.startsWith("*"))
      .map(parseAdbDeviceLine)
      .filter((device): device is AdbDevice => device !== null),
    available: true,
    error: null,
  };
}

export async function getTailscaleStatusResult(): Promise<CommandResult> {
  return runCommand("tailscale", ["status"]);
}

export async function getAdbDevicesResult(): Promise<CommandResult> {
  return runCommand("adb", ["devices", "-l"]);
}

export function findMatchingAdbDevice(node: NodeConfig, devices: AdbDevice[]): AdbDevice | null {
  const target = normalizeToken(node.name);
  for (const device of devices) {
    const haystacks = [
      device.serial,
      device.details.model ?? "",
      device.details.device ?? "",
      device.details.product ?? "",
    ].map(normalizeToken);

    if (haystacks.some((value) => value.length > 0 && (value.includes(target) || target.includes(value)))) {
      return device;
    }
  }

  return null;
}

export function interpretPingResult(result: CommandResult): boolean | null {
  if (result.ok) {
    return true;
  }

  if (result.exitCode === 1) {
    return false;
  }

  return null;
}

export function interpretSshResult(result: CommandResult): boolean | null {
  if (result.ok) {
    return true;
  }

  if (/operation not permitted/i.test(result.stderr)) {
    return null;
  }

  if (result.exitCode === 255) {
    return false;
  }

  return null;
}

async function runCommand(command: string, args: string[]): Promise<CommandResult> {
  const start = Date.now();

  try {
    const result = await execa(command, args, {
      reject: false,
      timeout: 5_000,
      all: false,
    });

    return {
      ok: result.exitCode === 0,
      exitCode: result.exitCode ?? null,
      stdout: result.stdout.trim(),
      stderr: result.stderr.trim(),
      durationMs: Date.now() - start,
    };
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    return {
      ok: false,
      exitCode: null,
      stdout: "",
      stderr: message,
      durationMs: Date.now() - start,
    };
  }
}

function parseAdbDeviceLine(line: string): AdbDevice | null {
  const [prefix, ...rest] = line.split(/\s+/);
  if (!prefix || rest.length === 0) {
    return null;
  }

  const state = rest[0] ?? "unknown";
  const details: Record<string, string> = {};

  for (const token of rest.slice(1)) {
    const [key, value] = token.split(":");
    if (key && value) {
      details[key] = value;
    }
  }

  return {
    serial: prefix,
    state,
    details,
  };
}

function normalizeToken(value: string): string {
  return value.toLowerCase().replace(/[^a-z0-9]/g, "");
}
