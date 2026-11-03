// T6 real adapter for T3 contract (AC-CANVAS-003 / FR-CANVAS-001).
// Declared per-surface budgets are overflow rails: exactly-at-budget emits,
// one-over fails closed with a named error AND a caller-supplied log entry —
// never silent truncation, never OOM.

export class CanvasBudgetError extends Error {
  constructor(budget: string, actual: number, limit: number) {
    super(`canvas budget exceeded: ${budget} (${actual} > ${limit})`);
    this.name = 'CanvasBudgetError';
  }
}

export interface CanvasOpts {
  maxCmds: number;
  maxDraws: number;
  maxInstances: number;
  loop: boolean;
}

export interface BudgetDescriptor {
  kind: string;
  [field: string]: unknown;
}

export interface LogSink {
  log: unknown[];
}

export interface Surface {
  emitFrame(descriptors: BudgetDescriptor[], sink?: LogSink): BudgetDescriptor[];
  isIdle(): boolean;
}

export function createCanvas(opts: CanvasOpts): Surface {
  let painted = false;

  function logTo(sink: LogSink | undefined, entry: unknown): void {
    if (sink !== undefined && Array.isArray(sink.log)) {
      sink.log.push(entry);
    }
  }

  return {
    emitFrame(descriptors: BudgetDescriptor[], sink?: LogSink): BudgetDescriptor[] {
      const n = descriptors.length;
      if (n > opts.maxCmds) {
        const err = new CanvasBudgetError('maxCmds', n, opts.maxCmds);
        logTo(sink, { message: err.message, budget: 'maxCmds' });
        throw err;
      }
      if (n > opts.maxDraws) {
        const err = new CanvasBudgetError('maxDraws', n, opts.maxDraws);
        logTo(sink, { message: err.message, budget: 'maxDraws' });
        throw err;
      }
      // NOTE: maxInstances counts emitted instances 1:1 with descriptors — emitFrame maps one descriptor to one instance/DrawCmd, so descriptors.length IS the instance count by definition (no separate instancing field exists).
      if (n > opts.maxInstances) {
        const err = new CanvasBudgetError('maxInstances', n, opts.maxInstances);
        logTo(sink, { message: err.message, budget: 'maxInstances' });
        throw err;
      }
      painted = true;
      return descriptors.map((d) => ({ ...d }));
    },
    isIdle(): boolean {
      return !opts.loop && painted;
    },
  };
}
