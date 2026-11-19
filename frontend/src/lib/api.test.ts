// api.test.ts — unit tests for the typed API client (src/lib/api.ts).
// No backend needed: fetch is stubbed. Covers the error mapping the
// whole UI relies on: backend {"error"} -> ApiError with status +
// message, network failure -> ApiError status 0, plus the pure
// helpers formatPkr and NEXT_STATUSES.
import { describe, expect, it, vi, beforeEach, afterEach } from 'vitest';
import {
  ApiError, API_BASE, formatPkr, getCsvPreview, getOrder, getPlan, getStats, NEXT_STATUSES,
} from './api';

function mockFetch(ok: boolean, status: number, body: string) {
  return vi.fn().mockResolvedValue({
    ok,
    status,
    text: async () => body,
  } as Response);
}

describe('ApiError mapping', () => {
  beforeEach(() => { vi.stubGlobal('fetch', mockFetch(true, 200, '{}')); });
  afterEach(() => { vi.unstubAllGlobals(); vi.restoreAllMocks(); });

  it('passes a successful JSON body through', async () => {
    vi.stubGlobal('fetch', mockFetch(true, 200, '{"total_orders":30}'));
    await expect(getStats()).resolves.toEqual({ total_orders: 30 });
  });

  it('turns a backend 400 {"error"} into an ApiError with status + message', async () => {
    vi.stubGlobal(
      'fetch',
      mockFetch(false, 404, '{"error":"Order 9999 not found."}'),
    );
    await expect(getOrder(9999)).rejects.toMatchObject({
      status: 404,
      message: 'Order 9999 not found.',
    });
    try {
      await getOrder(9999);
      expect.unreachable('should have thrown');
    } catch (e) {
      expect(e).toBeInstanceOf(ApiError);
    }
  });

  it('turns a network failure into ApiError status 0', async () => {
    vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new TypeError('fetch failed')));
    await expect(getStats()).rejects.toMatchObject({ status: 0 });
    await expect(getStats()).rejects.toThrow('Cannot reach the C++ backend');
  });

  it('falls back to a generic message when the error body has no error field', async () => {
    vi.stubGlobal('fetch', mockFetch(false, 500, '{}'));
    await expect(getStats()).rejects.toThrow('Request failed (HTTP 500).');
  });

  it('appends audit=false to the plan URL when requested', async () => {
    const fetchMock = mockFetch(true, 200, '{"total_time_min":900}');
    vi.stubGlobal('fetch', fetchMock);
    await getPlan('Lahore', 'Karachi', 'production', false);
    expect(fetchMock).toHaveBeenCalledWith(
      `${API_BASE}/delivery/plan?source=Lahore&destination=Karachi&dataset=production&audit=false`,
      expect.anything(),
    );
  });

  it('does not append audit=false by default', async () => {
    const fetchMock = mockFetch(true, 200, '{"total_time_min":900}');
    vi.stubGlobal('fetch', fetchMock);
    await getPlan('Lahore', 'Karachi');
    expect(fetchMock).toHaveBeenCalledWith(
      `${API_BASE}/delivery/plan?source=Lahore&destination=Karachi&dataset=production`,
      expect.anything(),
    );
  });
});

describe('getCsvPreview ("How CSV becomes orders")', () => {
  beforeEach(() => { vi.stubGlobal('fetch', mockFetch(true, 200, '{}')); });
  afterEach(() => { vi.unstubAllGlobals(); vi.restoreAllMocks(); });

  it('calls the fixed endpoint with no query string (no file selection)', async () => {
    const fetchMock = mockFetch(true, 200, '{"available":true,"rows":[]}');
    vi.stubGlobal('fetch', fetchMock);
    await getCsvPreview();
    expect(fetchMock).toHaveBeenCalledWith(
      `${API_BASE}/orders/csv-preview`,
      expect.anything(),
    );
  });

  it('passes an available preview through unchanged', async () => {
    const body = JSON.stringify({
      available: true,
      path: 'data/runtime/orders.csv',
      header: 'order_id,customer_name,product,category,price_pkr,quantity,order_date,city,status',
      rows: ['1001,Ahmed Raza,Wireless Headphones,Electronics,4500,1,2026-10-01,Lahore,Delivered'],
      preview_rows: 1,
      total_rows: 30,
      truncated: true,
    });
    vi.stubGlobal('fetch', mockFetch(true, 200, body));
    const pv = await getCsvPreview();
    expect(pv.available).toBe(true);
    expect(pv.path).toBe('data/runtime/orders.csv');
    expect(pv.rows[0]).toContain('1001,Ahmed Raza');
    expect(pv.total_rows).toBe(30);
    expect(pv.truncated).toBe(true);
  });

  it('passes an unavailable (missing file) preview through with its message', async () => {
    const body = JSON.stringify({
      available: false,
      path: 'data/runtime/orders.csv',
      header: '',
      rows: [],
      preview_rows: 0,
      total_rows: 0,
      truncated: false,
      message: 'The runtime orders.csv was not found or could not be read.',
    });
    vi.stubGlobal('fetch', mockFetch(true, 200, body));
    const pv = await getCsvPreview();
    expect(pv.available).toBe(false);
    expect(pv.rows).toEqual([]);
    expect(pv.message).toContain('not found');
  });

  it('turns a backend error into an ApiError like every other call', async () => {
    vi.stubGlobal('fetch', mockFetch(false, 500, '{}'));
    await expect(getCsvPreview()).rejects.toThrow('Request failed (HTTP 500).');
  });
});

describe('pure helpers', () => {
  it('formatPkr formats with Rs. and thousands separators', () => {
    expect(formatPkr(182300)).toBe('Rs. 182,300');
    expect(formatPkr(0)).toBe('Rs. 0');
    expect(formatPkr(9800)).toBe('Rs. 9,800');
  });

  it('NEXT_STATUSES matches the backend transition table', () => {
    expect(NEXT_STATUSES.Pending).toEqual(['Processing', 'Cancelled']);
    expect(NEXT_STATUSES.Processing).toEqual(['Shipped', 'Cancelled']);
    expect(NEXT_STATUSES.Shipped).toEqual(['Delivered']);
    expect(NEXT_STATUSES.Delivered).toEqual([]);
    expect(NEXT_STATUSES.Cancelled).toEqual([]);
  });
});
