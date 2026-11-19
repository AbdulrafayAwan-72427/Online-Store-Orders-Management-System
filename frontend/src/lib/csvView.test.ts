// csvView.test.ts — unit tests for the pure helpers behind the
// "How CSV becomes orders" panel (src/lib/csvView.ts). No backend
// needed: the helpers only interpret API-shaped data.
import { describe, expect, it } from 'vitest';
import { listPosition, parseCsvRowId, previewRowFor, queueNote, treeContains } from './csvView';
import type { Order, OrderTreeNode } from './api';

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
