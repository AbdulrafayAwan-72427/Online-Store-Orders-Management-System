// csvView.test.ts — unit tests for the pure helpers behind the
// "How CSV becomes orders" panel (src/lib/csvView.ts). No backend
// needed: the helpers only interpret API-shaped data.
import { describe, expect, it } from 'vitest';
import {
  PanelRequestGuard, SelectionRequestGuard, changedRowId, lastChangeMessage, listPosition, loadPanelData, loadSelectionData, parseCsvRowId, previewRowFor, queueNote, rowStatus, treeContains,
  type LastChange, type PanelData, type PanelFetchers, type PreviewState,
} from './csvView';
import type { CsvPreview, Order, OrderDetail, OrderTreeNode } from './api';

function order(id: number, status: Order['status']): Order {
  return {
    id,
    customer_name: `Customer ${id}`,
    product: 'Item',
    category: 'Misc',
    price_pkr: 1000,
    quantity: 1,
    order_date: '2026-10-01',
    city: 'Lahore',
    status,
  };
}

describe('parseCsvRowId', () => {
  it('reads the first field as the order ID', () => {
    expect(parseCsvRowId('1006,Sana Iqbal,Handbag,Accessories,5400,1,2026-10-03,Faisalabad,Pending')).toBe(1006);
  });

  it('returns null for a header or a non-numeric first field', () => {
    expect(parseCsvRowId('order_id,customer_name,product')).toBeNull();
    expect(parseCsvRowId('abc,foo')).toBeNull();
    expect(parseCsvRowId('')).toBeNull();
  });
});

describe('listPosition', () => {
  const orders = [order(1001, 'Delivered'), order(1006, 'Pending'), order(1030, 'Processing')];

  it('gives the 1-based position in linked-list order', () => {
    expect(listPosition(orders, 1001)).toBe(1);
    expect(listPosition(orders, 1006)).toBe(2);
    expect(listPosition(orders, 1030)).toBe(3);
  });

  it('returns -1 for an order that is not in the list', () => {
    expect(listPosition(orders, 9999)).toBe(-1);
    expect(listPosition([], 1001)).toBe(-1);
  });
});

describe('treeContains', () => {
  const leaf = (id: number): OrderTreeNode => ({
    order_id: id, customer_name: 'X', city: 'Lahore', status: 'Pending', left: null, right: null,
  });
  // Root 1015 with a left subtree (1005 -> 1001) and right child 1024,
  // shaped like the balanced load of the sample data.
  const tree: OrderTreeNode = {
    ...leaf(1015),
    left: { ...leaf(1005), left: leaf(1001) },
    right: leaf(1024),
  };

  it('finds the root and deep nodes', () => {
    expect(treeContains(tree, 1015)).toBe(true);
    expect(treeContains(tree, 1001)).toBe(true);
    expect(treeContains(tree, 1024)).toBe(true);
  });

  it('returns false for missing IDs and an empty tree', () => {
    expect(treeContains(tree, 9999)).toBe(false);
    expect(treeContains(null, 1015)).toBe(false);
  });
});

describe('queueNote', () => {
  const queue = [order(1006, 'Pending'), order(1012, 'Pending'), order(1017, 'Pending')];

  it('reports the position of a Pending order in the queue', () => {
    expect(queueNote(order(1006, 'Pending'), queue)).toBe(
      'In the processing queue — position 1 of 3 (the front is processed next).',
    );
    expect(queueNote(order(1017, 'Pending'), queue)).toContain('position 3 of 3');
  });

  it('states clearly that a non-Pending order is not in the queue', () => {
    const note = queueNote(order(1005, 'Processing'), queue);
    expect(note).toContain('Not in the queue');
    expect(note).toContain('only Pending orders are in the processing queue');
    expect(note).toContain('This order is Processing.');
    expect(queueNote(order(1001, 'Delivered'), queue)).toContain('This order is Delivered.');
  });

  it('handles a Pending order missing from the returned queue', () => {
    expect(queueNote(order(1099, 'Pending'), queue)).toContain('not found in the queue');
  });
});

describe('previewRowFor', () => {
  const rows = [
    '1001,Ahmed Raza,Wireless Headphones,Electronics,4500,1,2026-10-01,Lahore,Delivered',
    '1006,Sana Iqbal,Handbag,Accessories,5400,1,2026-10-03,Faisalabad,Pending',
  ];

  it('finds the raw row for an order inside the preview', () => {
    expect(previewRowFor(rows, 1006)).toBe(rows[1]);
  });

  it('returns null for an order beyond the preview', () => {
    expect(previewRowFor(rows, 1030)).toBeNull();
    expect(previewRowFor([], 1001)).toBeNull();
  });
});

describe('rowStatus', () => {
  it('reads the status from the 9th column, like parseOrder', () => {
    expect(rowStatus('1006,Sana Iqbal,Handbag,Accessories,5400,1,2026-10-03,Faisalabad,Processing')).toBe('Processing');
    expect(rowStatus('1001,Ahmed Raza,Wireless Headphones,Electronics,4500,1,2026-10-01,Lahore,Delivered')).toBe('Delivered');
  });

  it('returns null for a short or empty row', () => {
    expect(rowStatus('1006,Sana Iqbal')).toBeNull();
    expect(rowStatus('')).toBeNull();
  });
});

describe('changedRowId', () => {
  const fresh: PreviewState = { refreshed: true, available: true, rows: [], previewRows: 10 };
  const stale: PreviewState = { refreshed: false, available: true, rows: [], previewRows: 10 };
  const unavailable: PreviewState = { refreshed: true, available: false, rows: [], previewRows: 10 };

  it('highlights the changed row only after a successful refresh', () => {
    const change: LastChange = { kind: 'status', id: 1006, from: 'Pending', to: 'Processing' };
    expect(changedRowId(change, fresh)).toBe(1006);
    expect(changedRowId(change, stale)).toBeNull();
    expect(changedRowId(change, unavailable)).toBeNull();
  });

  it('highlights cancelled and created orders after a refresh', () => {
    expect(changedRowId({ kind: 'cancelled', id: 1006 }, fresh)).toBe(1006);
    expect(changedRowId({ kind: 'created', id: 1031 }, fresh)).toBe(1031);
  });

  it('never highlights a deleted order or a missing change record', () => {
    expect(changedRowId({ kind: 'deleted', id: 1006, wasInPreview: true }, fresh)).toBeNull();
    expect(changedRowId({ kind: 'deleted', id: 1030, wasInPreview: false }, fresh)).toBeNull();
    expect(changedRowId(null, fresh)).toBeNull();
  });
});

describe('lastChangeMessage', () => {
  const row1006 = (status: string) =>
    `1006,Sana Iqbal,Handbag,Accessories,5400,1,2026-10-03,Faisalabad,${status}`;
  const state = (over: Partial<PreviewState>): PreviewState => ({
    refreshed: true, available: true, rows: [row1006('Processing')], previewRows: 10, ...over,
  });

  it('states the final status read from the refreshed row itself', () => {
    const msg = lastChangeMessage(
      { kind: 'status', id: 1006, from: 'Pending', to: 'Processing' },
      state({}),
    );
    expect(msg).toContain('Order #1006 Pending → Processing');
    expect(msg).toContain('was refreshed');
    expect(msg).toContain('now ends with ,Processing');
  });

  it('reads the row even when it disagrees with the requested status', () => {
    // A later cancel rewrote the row: the message must follow the
    // file, not the earlier requested transition.
    const msg = lastChangeMessage(
      { kind: 'status', id: 1006, from: 'Pending', to: 'Processing' },
      state({ rows: [row1006('Cancelled')] }),
    );
    expect(msg).toContain('now ends with ,Cancelled');
    expect(msg).not.toContain('now ends with ,Processing');
  });

  it('does not claim a refresh before one has succeeded', () => {
    const msg = lastChangeMessage(
      { kind: 'status', id: 1006, from: 'Pending', to: 'Processing' },
      state({ refreshed: false }),
    );
    expect(msg).toContain('has not been refreshed yet');
    expect(msg).not.toContain('was refreshed');
    expect(msg).not.toContain('now ends with');
  });

  it('reports a cancellation from the refreshed row', () => {
    const msg = lastChangeMessage(
      { kind: 'cancelled', id: 1006 },
      state({ rows: [row1006('Cancelled')] }),
    );
    expect(msg).toContain('Order #1006 cancelled');
    expect(msg).toContain('now ends with ,Cancelled');
  });

  it('verifies a deletion from the preview when the row was shown before the delete', () => {
    const msg = lastChangeMessage(
      { kind: 'deleted', id: 1006, wasInPreview: true },
      state({ rows: ['1001,Ahmed Raza,Wireless Headphones,Electronics,4500,1,2026-10-01,Lahore,Delivered'] }),
    );
    expect(msg).toContain('Order #1006 deleted');
    expect(msg).toContain('was refreshed');
    expect(msg).toContain('shown in the preview before the delete');
    expect(msg).toContain('no longer in the file');
  });

  it('does not claim preview verification for a deletion beyond the preview', () => {
    // Order 1030's row was never among the displayed rows, so its
    // absence from the refreshed preview proves nothing — the
    // message must say the delete succeeded via the API instead.
    const msg = lastChangeMessage(
      { kind: 'deleted', id: 1030, wasInPreview: false },
      state({}),
    );
    expect(msg).toContain('Order #1030 deleted');
    expect(msg).toContain('delete succeeded through the API');
    expect(msg).toContain('not shown in the preview before the delete');
    expect(msg).toContain('first 10 rows');
    expect(msg).toContain('cannot show the removal');
    expect(msg).not.toContain('no longer in the file');
  });

  it('warns when a deleted order is still shown after the refresh', () => {
    const msg = lastChangeMessage(
      { kind: 'deleted', id: 1006, wasInPreview: true },
      state({}),
    );
    expect(msg).toContain('still shown');
    expect(msg).toContain('refresh again to re-check');
    expect(msg).not.toContain('no longer in the file');
  });

  it('does not claim a refresh for a deletion that has not been re-read', () => {
    const msg = lastChangeMessage({ kind: 'deleted', id: 1006, wasInPreview: true }, state({ refreshed: false }));
    expect(msg).toContain('has not been refreshed yet');
    expect(msg).not.toContain('no longer in the file');
    const beyond = lastChangeMessage({ kind: 'deleted', id: 1030, wasInPreview: false }, state({ refreshed: false }));
    expect(beyond).toContain('has not been refreshed yet');
    expect(beyond).not.toContain('no longer in the file');
  });

  it('says honestly when the row is beyond the bounded preview', () => {
    const msg = lastChangeMessage(
      { kind: 'status', id: 1030, from: 'Pending', to: 'Processing' },
      state({}),
    );
    expect(msg).toContain('beyond the first 10 rows');
    expect(msg).not.toContain('now ends with');
  });

  it('explains a created order whose row is at the end of the file', () => {
    const msg = lastChangeMessage({ kind: 'created', id: 1031 }, state({}));
    expect(msg).toContain('Order #1031 created (Pending)');
    expect(msg).toContain('appended at the end of the file');
  });

  it('says the file could not be re-read when it is unavailable', () => {
    const msg = lastChangeMessage(
      { kind: 'status', id: 1006, from: 'Pending', to: 'Processing' },
      state({ available: false, rows: [] }),
    );
    expect(msg).toContain('could not be re-read');
    expect(msg).not.toContain('now ends with');
  });
});

describe('loadPanelData (out-of-order safety)', () => {
  // A manually resolvable promise: the test decides exactly when
  // each load's responses "arrive", so two loads can be made to
  // finish in the opposite order to the one they started in.
  function deferred<T>() {
    let resolve!: (value: T) => void;
    const promise = new Promise<T>((res) => { resolve = res; });
    return { promise, resolve };
  }

  function previewWith(status: string): CsvPreview {
    return {
      available: true,
      path: 'data/runtime/orders.csv',
      header: 'order_id,customer_name,product,category,price_pkr,quantity,order_date,city,status',
      rows: [`1006,Sana Iqbal,Handbag,Accessories,5400,1,2026-10-03,Faisalabad,${status}`],
      preview_rows: 1,
      total_rows: 30,
      truncated: true,
    };
  }

  // All four fetchers wait on the same gate, so resolving the gate
  // delivers this load's whole response at a moment the test picks.
  function fetchersFor(pv: CsvPreview, gate: ReturnType<typeof deferred<void>>): PanelFetchers {
    return {
      getCsvPreview: async () => { await gate.promise; return pv; },
      getOrders: async () => { await gate.promise; return [order(1006, 'Pending')]; },
      getQueue: async () => { await gate.promise; return []; },
      getOrderTree: async () => { await gate.promise; return { root: null, size: 30, height: 5 }; },
    };
  }

  it('applies the response when nothing newer has started', async () => {
    const guard = new PanelRequestGuard();
    const applied: PanelData[] = [];
    const gate = deferred<void>();
    gate.resolve();
    const token = guard.begin();
    await expect(
      loadPanelData(fetchersFor(previewWith('Pending'), gate), guard, token, (d) => applied.push(d)),
    ).resolves.toBe('applied');
    expect(applied).toHaveLength(1);
    expect(applied[0].preview.rows[0]).toContain(',Pending');
  });

  it('discards an older response that finishes after a newer one', async () => {
    const guard = new PanelRequestGuard();
    const applied: PanelData[] = [];
    const apply = (d: PanelData) => applied.push(d);

    const gateA = deferred<void>();
    const gateB = deferred<void>();
    // Load A starts first, carrying older data (order still Pending)...
    const tokenA = guard.begin();
    const loadA = loadPanelData(fetchersFor(previewWith('Pending'), gateA), guard, tokenA, apply);
    // ...then load B starts, carrying newer data (order now Processing).
    const tokenB = guard.begin();
    const loadB = loadPanelData(fetchersFor(previewWith('Processing'), gateB), guard, tokenB, apply);

    // B finishes first and is applied.
    gateB.resolve();
    await expect(loadB).resolves.toBe('applied');
    // A finishes late with stale rows: discarded entirely — its
    // data must never reach the panel state.
    gateA.resolve();
    await expect(loadA).resolves.toBe('discarded');

    expect(applied).toHaveLength(1);
    expect(applied[0].preview.rows[0]).toContain(',Processing');
    // And the message built from what was applied follows B's data,
    // not A's stale rows.
    const state: PreviewState = {
      refreshed: true, available: true, rows: applied[0].preview.rows, previewRows: 10,
    };
    const msg = lastChangeMessage({ kind: 'status', id: 1006, from: 'Pending', to: 'Processing' }, state);
    expect(msg).toContain('now ends with ,Processing');
  });

  it('discards a response that started before a mutation', async () => {
    const guard = new PanelRequestGuard();
    const applied: PanelData[] = [];
    const gate = deferred<void>();
    const token = guard.begin();
    const load = loadPanelData(fetchersFor(previewWith('Pending'), gate), guard, token, (d) => applied.push(d));
    guard.invalidate(); // a mutation happened while the read was in flight
    gate.resolve();
    await expect(load).resolves.toBe('discarded');
    expect(applied).toHaveLength(0);
    expect(guard.isCurrent(token)).toBe(false);
  });

  it('propagates fetch errors so the caller can gate them on the token', async () => {
    const guard = new PanelRequestGuard();
    const token = guard.begin();
    const failing: PanelFetchers = {
      getCsvPreview: async () => { throw new Error('boom'); },
      getOrders: async () => [],
      getQueue: async () => [],
      getOrderTree: async () => ({ root: null, size: 0, height: 0 }),
    };
    await expect(loadPanelData(failing, guard, token, () => {})).rejects.toThrow('boom');
    // Once a newer load has begun, the failed token is no longer
    // current, so the caller must not surface its error.
    const token2 = guard.begin();
    expect(guard.isCurrent(token)).toBe(false);
    expect(guard.isCurrent(token2)).toBe(true);
  });
});

describe('loadSelectionData (out-of-order safety)', () => {
  // Same deferred-promise trick as the panel tests: each detail
  // request's response "arrives" exactly when the test resolves
  // its gate, so two selections can be made to finish in the
  // opposite order to the one they started in.
  function deferred<T>() {
    let resolve!: (value: T) => void;
    const promise = new Promise<T>((res) => { resolve = res; });
    return { promise, resolve };
  }

  function detail(id: number, status: Order['status']): OrderDetail {
    return { order: order(id, status), history: [] };
  }

  // The fetcher waits on the gate, so resolving the gate delivers
  // this request's response at a moment the test picks.
  function fetcherFor(d: OrderDetail, gate: ReturnType<typeof deferred<void>>) {
    return async (_id: number): Promise<OrderDetail> => { await gate.promise; return d; };
  }

  it('applies the response while the order is still selected', async () => {
    const guard = new SelectionRequestGuard();
    const applied: OrderDetail[] = [];
    const gate = deferred<void>();
    gate.resolve();
    guard.selectionChanged(1006);
    const token = guard.begin(1006);
    await expect(
      loadSelectionData(fetcherFor(detail(1006, 'Pending'), gate), guard, token, 1006, (d) => applied.push(d)),
    ).resolves.toBe('applied');
    expect(applied).toHaveLength(1);
    expect(applied[0].order.id).toBe(1006);
  });

  it('keeps showing B when A\'s late response arrives after B\'s', async () => {
    const guard = new SelectionRequestGuard();
    const applied: OrderDetail[] = [];
    const apply = (d: OrderDetail) => applied.push(d);

    const gateA = deferred<void>();
    const gateB = deferred<void>();
    // Order A is selected first; its request starts...
    guard.selectionChanged(1006);
    const tokenA = guard.begin(1006);
    const loadA = loadSelectionData(fetcherFor(detail(1006, 'Pending'), gateA), guard, tokenA, 1006, apply);
    // ...then order B is selected; its request starts.
    guard.selectionChanged(1012);
    const tokenB = guard.begin(1012);
    const loadB = loadSelectionData(fetcherFor(detail(1012, 'Processing'), gateB), guard, tokenB, 1012, apply);

    // B's response arrives first and is applied: inspector shows B.
    gateB.resolve();
    await expect(loadB).resolves.toBe('applied');
    // A's response arrives last: discarded entirely, so the
    // inspector continues showing B.
    gateA.resolve();
    await expect(loadA).resolves.toBe('discarded');

    expect(applied).toHaveLength(1);
    expect(applied[0].order.id).toBe(1012);
    expect(applied[0].order.status).toBe('Processing');
  });

  it('discards a response that arrives after the selection was cleared', async () => {
    const guard = new SelectionRequestGuard();
    const applied: OrderDetail[] = [];
    const gate = deferred<void>();
    guard.selectionChanged(1006);
    const token = guard.begin(1006);
    const load = loadSelectionData(fetcherFor(detail(1006, 'Pending'), gate), guard, token, 1006, (d) => applied.push(d));
    // The user clears the selection (or the selected order is
    // deleted) while the request is still in flight.
    guard.selectionChanged(null);
    gate.resolve();
    await expect(load).resolves.toBe('discarded');
    expect(applied).toHaveLength(0);
    expect(guard.isCurrent(token, 1006)).toBe(false);
  });

  it('discards an older request for the same order when a newer one started', async () => {
    // e.g. a status change on the selected order triggers a fresh
    // detail load while the first is still in flight.
    const guard = new SelectionRequestGuard();
    const applied: OrderDetail[] = [];
    const apply = (d: OrderDetail) => applied.push(d);
    const gateOld = deferred<void>();
    const gateNew = deferred<void>();
    guard.selectionChanged(1006);
    const tokenOld = guard.begin(1006);
    const loadOld = loadSelectionData(fetcherFor(detail(1006, 'Pending'), gateOld), guard, tokenOld, 1006, apply);
    const tokenNew = guard.begin(1006); // refresh after a mutation on the selected order
    const loadNew = loadSelectionData(fetcherFor(detail(1006, 'Processing'), gateNew), guard, tokenNew, 1006, apply);

    gateNew.resolve();
    await expect(loadNew).resolves.toBe('applied');
    gateOld.resolve();
    await expect(loadOld).resolves.toBe('discarded');

    expect(applied).toHaveLength(1);
    expect(applied[0].order.status).toBe('Processing');
  });

  it('keeps an in-flight request when the same order is re-selected', async () => {
    const guard = new SelectionRequestGuard();
    const applied: OrderDetail[] = [];
    const gate = deferred<void>();
    guard.selectionChanged(1006);
    const token = guard.begin(1006);
    const load = loadSelectionData(fetcherFor(detail(1006, 'Pending'), gate), guard, token, 1006, (d) => applied.push(d));
    guard.selectionChanged(1006); // same order clicked again: no-op
    gate.resolve();
    await expect(load).resolves.toBe('applied');
    expect(applied).toHaveLength(1);
    expect(applied[0].order.id).toBe(1006);
  });

  it('propagates fetch errors so the caller can gate them on the token', async () => {
    const guard = new SelectionRequestGuard();
    guard.selectionChanged(1006);
    const token = guard.begin(1006);
    const failing = async (): Promise<OrderDetail> => { throw new Error('boom'); };
    await expect(loadSelectionData(failing, guard, token, 1006, () => {})).rejects.toThrow('boom');
    // Once a newer selection has begun, the failed token is no
    // longer current, so the caller must not surface its error.
    guard.selectionChanged(1012);
    const token2 = guard.begin(1012);
    expect(guard.isCurrent(token, 1006)).toBe(false);
    expect(guard.isCurrent(token2, 1012)).toBe(true);
  });
});
