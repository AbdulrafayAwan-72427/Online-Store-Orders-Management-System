// api.test.ts — unit tests for the typed API client (src/lib/api.ts).
// No backend needed: fetch is stubbed. Covers the error mapping the
// whole UI relies on: backend {"error"} -> ApiError with status +
// message, network failure -> ApiError status 0, plus the pure
// helpers formatPkr and NEXT_STATUSES.
import { describe, expect, it, vi, beforeEach, afterEach } from 'vitest';
import {
  ApiError, API_BASE, formatPkr, getOrder, getPlan, getStats, NEXT_STATUSES,
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
