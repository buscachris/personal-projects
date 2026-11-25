import { type CollectionSnapshot } from "./collector.js";

export type ReachBucket = "yes" | "no" | "unknown";

export type SummaryReport = {
  collectedAt: string;
  totals: {
    nodes: number;
    reachable: Record<ReachBucket, number>;
  };
  byRole: Record<
    string,
    {
      total: number;
      reachable: Record<ReachBucket, number>;
    }
  >;
};

export function buildSummary(snapshot: CollectionSnapshot): SummaryReport {
  const report: SummaryReport = {
    collectedAt: snapshot.collectedAt,
    totals: {
      nodes: snapshot.nodes.length,
      reachable: {
        yes: 0,
        no: 0,
        unknown: 0,
      },
    },
    byRole: {},
  };

  for (const node of snapshot.nodes) {
    const reach = toReachBucket(node.reachable);
    report.totals.reachable[reach] += 1;

    for (const role of node.roles) {
      const bucket = report.byRole[role] ?? {
        total: 0,
        reachable: {
          yes: 0,
          no: 0,
          unknown: 0,
        },
      };

      bucket.total += 1;
      bucket.reachable[reach] += 1;
      report.byRole[role] = bucket;
    }
  }

  return report;
}

function toReachBucket(value: boolean | null): ReachBucket {
  if (value === true) {
    return "yes";
  }

  if (value === false) {
    return "no";
  }

  return "unknown";
}
