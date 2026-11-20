// csvView.ts — pure helpers for the "How CSV becomes orders"
// educational view on the Orders page. No fetching here: these
// functions only interpret data the existing API endpoints already
// return (the CSV preview, the order list in linked-list order, the
// queue front -> rear, and the whole Order BST). Kept separate so
// they can be unit-tested without a backend.
import type { Order, OrderStatus, OrderTreeNode } from './api';

/**
 * The order ID of one raw CSV data line (its first comma-separated
 * field), or null when that field is not a plain number. Mirrors
 * what parseOrder does with stoi on the backend, minus the throw.
 */
export function parseCsvRowId(line: string): number | null {
  const first = line.split(',')[0]?.trim() ?? '';
  if (!/^\d+$/.test(first)) return null;
  return Number(first);
}

/**
 * 1-based position of an order in the list returned by
 * GET /api/orders, which is the backend's linked-list order
 * (insertion/file order). Returns -1 when the order is not in the
 * given list (e.g. it was deleted after the list was fetched).
 */
export function listPosition(orders: Order[], id: number): number {
  const i = orders.findIndex((o) => o.id === id);
  return i === -1 ? -1 : i + 1;
}

/**
 * Is this order ID present in the Order BST returned by
 * GET /api/orders/tree? A plain walk over the returned nodes —
 * membership is all the endpoint needs to prove here; the tree
 * itself (size/height) comes straight from the same response.
 */
export function treeContains(node: OrderTreeNode | null, id: number): boolean {
  if (!node) return false;
  if (node.order_id === id) return true;
  return treeContains(node.left, id) || treeContains(node.right, id);
}

/**
 * Plain-English queue membership note for one order, given the live
 * queue from GET /api/queue (Pending orders, front -> rear).
 * The queue invariant (Store): exactly the Pending orders, in
 * arrival order — so a non-Pending order is never in it.
 */
export function queueNote(order: Order, queue: Order[]): string {
  if (order.status !== 'Pending') {
    return `Not in the queue — only Pending orders are in the processing queue. This order is ${order.status}.`;
  }
  const i = queue.findIndex((o) => o.id === order.id);
  if (i === -1) {
    return 'Marked Pending, but not found in the queue the API just returned — refresh to re-check.';
  }
  return `In the processing queue — position ${i + 1} of ${queue.length} (the front is processed next).`;
}

/**
 * The raw preview row belonging to an order ID, or null when the
 * order's row is beyond the bounded preview (or the file is
 * unavailable). Rows are matched by their first field, exactly as
 * the backend's parseOrder reads the ID.
 */
export function previewRowFor(rows: string[], id: number): string | null {
  return rows.find((r) => parseCsvRowId(r) === id) ?? null;
}

/**
 * The status field (9th comma-separated column) of one raw CSV
 * data row, or null when the row has fewer fields. Mirrors the
 * backend's parseOrder, which reads the status from column 9.
 */
export function rowStatus(row: string): string | null {
  const s = row.split(',')[8]?.trim();
  return s ? s : null;
}

/**
 * The most recent successful mutation made through the Orders page
 * controls, as the educational panel reports it. One record covers
 * every mutation kind, so a later action always replaces — never
 * contradicts — the report of an earlier one.
 */
export type LastChange =
  | { kind: 'status'; id: number; from: OrderStatus; to: OrderStatus }
  | { kind: 'cancelled'; id: number }
  | { kind: 'deleted'; id: number }
  | { kind: 'created'; id: number };

/** What the panel currently knows about the on-disk file preview. */
export interface PreviewState {
  /**
   * True only when the displayed preview was successfully re-read
   * after the change was made. A stale preview still shows the old
   * rows and must not be described as refreshed.
   */
  refreshed: boolean;
  /** False when the runtime file could not be read. */
  available: boolean;
  /** Raw data rows currently displayed (the bounded preview). */
  rows: string[];
  /** How many data rows the bounded preview shows. */
  previewRows: number;
}

/**
 * The order ID whose preview row should carry the "(just changed)"
 * highlight, or null. The highlight appears only when the preview
 * was successfully refreshed after the change — highlighting a
 * stale row would contradict the file. A deleted order has no row
 * left to highlight.
 */
export function changedRowId(change: LastChange | null, state: PreviewState): number | null {
  if (!change || change.kind === 'deleted') return null;
  if (!state.refreshed || !state.available) return null;
  return change.id;
}

/**
 * The one-line "last change" report shown under the file preview.
 * It claims the preview was refreshed only when it actually was,
 * and any final status it states is read from the refreshed row
 * itself — never assumed from the action that was requested.
 */
export function lastChangeMessage(change: LastChange, state: PreviewState): string {
  const base =
    change.kind === 'status'
      ? `Last change through the controls on this page: Order #${change.id} ${change.from} → ${change.to}.`
      : change.kind === 'cancelled'
        ? `Last change through the controls on this page: Order #${change.id} cancelled.`
        : change.kind === 'deleted'
          ? `Last change through the controls on this page: Order #${change.id} deleted.`
          : `Last change through the controls on this page: Order #${change.id} created (Pending).`;

  if (!state.refreshed) {
    return `${base} The backend rewrote orders.csv, but the preview above has not been refreshed yet, so it may not show the saved row — use Refresh preview.`;
  }
  if (!state.available) {
    return `${base} The preview above could not be re-read (the runtime file is unavailable), so the saved row cannot be shown here.`;
  }
  const row = previewRowFor(state.rows, change.id);
  if (change.kind === 'deleted') {
    return row
      ? `${base} The preview above was refreshed, but a row for Order #${change.id} is still shown — refresh again to re-check.`
      : `${base} The preview above was refreshed — Order #${change.id}'s row is no longer in the file.`;
  }
  if (row) {
    const status = rowStatus(row);
    return status
      ? `${base} The preview above was refreshed — the highlighted row now ends with ,${status}.`
      : `${base} The preview above was refreshed — the highlighted row is Order #${change.id}'s saved row.`;
  }
  if (change.kind === 'created') {
    return `${base} The preview above was refreshed — the new row is appended at the end of the file, beyond the first ${state.previewRows} rows shown.`;
  }
  return `${base} The preview above was refreshed — this order's row is beyond the first ${state.previewRows} rows shown, so it is not visible in the preview.`;
}
