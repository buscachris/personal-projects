import net from "node:net";
import {
  getAdbDevicesResult,
  getTailscaleStatusResult,
  pingHost,
  type CommandResult,
} from "./actions.js";

export type DoctorCheck = {
  name: string;
  ok: boolean | null;
  status: "ok" | "degraded" | "blocked";
  message: string;
  details?: Record<string, unknown>;
};

export type DoctorReport = {
  checkedAt: string;
  overall: "ok" | "degraded" | "blocked";
  checks: DoctorCheck[];
};

export async function runDoctor(): Promise<DoctorReport> {
  const checks = await Promise.all([
    checkPing(),
    checkTailscale(),
    checkAdb(),
    checkTcpConnect(),
    checkApiListen(),
  ]);

  return {
    checkedAt: new Date().toISOString(),
    overall: summarizeOverall(checks),
    checks,
  };
}

async function checkPing(): Promise<DoctorCheck> {
  const result = await pingHost("127.0.0.1");
  if (result.ok) {
    return {
      name: "ping",
      ok: true,
      status: "ok",
      message: "ICMP probe works on loopback.",
      details: summarize(result),
    };
  }

  if (/operation not permitted/i.test(result.stderr)) {
    return {
      name: "ping",
      ok: null,
      status: "blocked",
      message: "ICMP probe is blocked on this host.",
      details: summarize(result),
    };
  }

  return {
    name: "ping",
    ok: false,
    status: "degraded",
    message: "ICMP probe failed for a non-permission reason.",
    details: summarize(result),
  };
}

async function checkTailscale(): Promise<DoctorCheck> {
  const result = await getTailscaleStatusResult();
  if (result.ok) {
    return {
      name: "tailscale",
      ok: true,
      status: "ok",
      message: "tailscaled is reachable locally.",
      details: summarize(result),
    };
  }

  if (/doesn't appear to be running/i.test(result.stderr) || /failed to connect to local tailscaled/i.test(result.stderr)) {
    return {
      name: "tailscale",
      ok: false,
      status: "degraded",
      message: "tailscaled is not available on this host.",
      details: summarize(result),
    };
  }

  return {
    name: "tailscale",
    ok: false,
    status: "degraded",
    message: "tailscale status failed.",
    details: summarize(result),
  };
}

async function checkAdb(): Promise<DoctorCheck> {
  const result = await getAdbDevicesResult();
  if (result.ok) {
    return {
      name: "adb",
      ok: true,
      status: "ok",
      message: "ADB daemon is reachable.",
      details: summarize(result),
    };
  }

  if (/operation not permitted/i.test(result.stderr) || /could not install \*smartsocket\* listener/i.test(result.stderr)) {
    return {
      name: "adb",
      ok: null,
      status: "blocked",
      message: "ADB daemon cannot bind locally on this host.",
      details: summarize(result),
    };
  }

  return {
    name: "adb",
    ok: false,
    status: "degraded",
    message: "ADB probe failed.",
    details: summarize(result),
  };
}

async function checkTcpConnect(): Promise<DoctorCheck> {
  try {
    const outcome = await testTcpConnect("127.0.0.1", 22, 1_000);
    if (outcome === "connected") {
      return {
        name: "tcp-connect",
        ok: true,
        status: "ok",
        message: "Outbound TCP connections work.",
      };
    }

    if (outcome === "refused") {
      return {
        name: "tcp-connect",
        ok: true,
        status: "ok",
        message: "Outbound TCP sockets work; loopback port 22 is just closed.",
      };
    }

    return {
      name: "tcp-connect",
      ok: null,
      status: "blocked",
      message: "Outbound TCP sockets are blocked on this host.",
      details: { outcome },
    };
  } catch (error) {
    return {
      name: "tcp-connect",
      ok: false,
      status: "degraded",
      message: error instanceof Error ? error.message : String(error),
    };
  }
}

async function checkApiListen(): Promise<DoctorCheck> {
  try {
    await testListen("127.0.0.1");
    return {
      name: "api-listen",
      ok: true,
      status: "ok",
      message: "Local HTTP listener binding works.",
    };
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    if (/operation not permitted/i.test(message) || /eperm/i.test(message)) {
      return {
        name: "api-listen",
        ok: null,
        status: "blocked",
        message: "Local listener binding is blocked on this host.",
        details: { error: message },
      };
    }

    return {
      name: "api-listen",
      ok: false,
      status: "degraded",
      message: "Local listener binding failed.",
      details: { error: message },
    };
  }
}

function summarizeOverall(checks: DoctorCheck[]): DoctorReport["overall"] {
  if (checks.some((check) => check.status === "blocked")) {
    return "blocked";
  }

  if (checks.some((check) => check.status === "degraded")) {
    return "degraded";
  }

  return "ok";
}

function summarize(result: CommandResult) {
  return {
    exitCode: result.exitCode,
    durationMs: result.durationMs,
    stdout: result.stdout,
    stderr: result.stderr,
  };
}

async function testTcpConnect(host: string, port: number, timeoutMs: number): Promise<"connected" | "refused" | "blocked" | "timeout"> {
  return new Promise((resolve, reject) => {
    const socket = net.connect({ host, port });
    const cleanup = () => {
      socket.removeAllListeners();
      socket.destroy();
    };

    socket.setTimeout(timeoutMs);

    socket.on("connect", () => {
      cleanup();
      resolve("connected");
    });

    socket.on("timeout", () => {
      cleanup();
      resolve("timeout");
    });

    socket.on("error", (error: NodeJS.ErrnoException) => {
      cleanup();
      if (error.code === "ECONNREFUSED") {
        resolve("refused");
        return;
      }
      if (error.code === "EPERM" || error.code === "EACCES") {
        resolve("blocked");
        return;
      }
      reject(error);
    });
  });
}

async function testListen(host: string): Promise<void> {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.on("error", (error) => {
      server.close();
      reject(error);
    });
    server.listen(0, host, () => {
      server.close((error) => {
        if (error) {
          reject(error);
          return;
        }
        resolve();
      });
    });
  });
}
