import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import YAML from "yaml";
import { z } from "zod";

const nodeConfigSchema = z.object({
  ssh_user: z.string().min(1).optional(),
  lan: z.string().min(1).optional(),
  tailscale: z.string().min(1).optional(),
  roles: z.array(z.string().min(1)).default([]),
});

const configSchema = z.object({
  nodes: z.record(nodeConfigSchema),
});

export type NodeConfig = z.infer<typeof nodeConfigSchema> & {
  name: string;
};

export type MachineBodyConfig = {
  nodes: NodeConfig[];
};

const srcDir = path.dirname(fileURLToPath(import.meta.url));
export const projectRoot = path.resolve(srcDir, "..");
export const nodesConfigPath = path.join(projectRoot, "nodes.yaml");
export const dataDir = path.join(projectRoot, "data");
export const databasePath = path.join(dataDir, "machine-body.sqlite");

export function loadConfig(): MachineBodyConfig {
  const raw = fs.readFileSync(nodesConfigPath, "utf8");
  const parsed = configSchema.parse(YAML.parse(raw));

  return {
    nodes: Object.entries(parsed.nodes).map(([name, node]) => ({
      name,
      ...node,
    })),
  };
}
