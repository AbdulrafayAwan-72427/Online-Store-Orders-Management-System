// csvView.ts — pure helpers for the "How CSV becomes orders"
// educational view on the Orders page. No fetching here: these
// functions only interpret data the existing API endpoints already
// return (the CSV preview, the order list in linked-list order, the
// queue front -> rear, and the whole Order BST). Kept separate so
// they can be unit-tested without a backend.
import type { Order, OrderTreeNode } from './api';

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
