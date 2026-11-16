// Centralised typed API client for the C++17 backend.
// No business logic or DSA here — only fetch + types. Every rule
// (transition table, queue invariant, assignment rules) lives in Store.

export const API_BASE = 'http://127.0.0.1:8080/api';

export class ApiError extends Error {
  status: number;
  constructor(status: number, message: string) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  let res: Response;
  try {
    res = await fetch(`${API_BASE}${path}`, {
      ...init,
      headers: { 'Content-Type': 'application/json', ...(init?.headers ?? {}) },
    });
  } catch {
    throw new ApiError(0, 'Cannot reach the C++ backend at 127.0.0.1:8080. Is the server running?');
  }
  const text = await res.text();
  let data: unknown = null;
  if (text) {
    try {
      data = JSON.parse(text) as unknown;
    } catch {
      throw new ApiError(res.status, `Backend returned invalid JSON (HTTP ${res.status}).`);
    }
  }
  if (!res.ok) {
    const msg =
      data && typeof data === 'object' && data !== null && 'error' in data && typeof (data as { error: unknown }).error === 'string'
        ? (data as { error: string }).error
        : `Request failed (HTTP ${res.status}).`;
    throw new ApiError(res.status, msg);
  }
  return data as T;
}

// ---- Types (mirror server.cpp orderToJson / logToJson / stats / analytics) ----

export type OrderStatus = 'Pending' | 'Processing' | 'Shipped' | 'Delivered' | 'Cancelled';

export const ALL_STATUSES: OrderStatus[] = ['Pending', 'Processing', 'Shipped', 'Delivered', 'Cancelled'];

export const CITIES = ['Lahore', 'Karachi', 'Islamabad', 'Rawalpindi', 'Faisalabad', 'Multan'] as const;

/** Next statuses allowed by the backend transition table (Store::isValidTransition). */
export const NEXT_STATUSES: Record<OrderStatus, OrderStatus[]> = {
  Pending: ['Processing', 'Cancelled'],
  Processing: ['Shipped', 'Cancelled'],
  Shipped: ['Delivered'],
  Delivered: [],
  Cancelled: [],
};

export interface Order {
  id: number;
  customer_name: string;
  product: string;
  category: string;
  price_pkr: number;
  quantity: number;
  order_date: string;
  city: string;
  status: OrderStatus;
}

export interface LogEntry {
  log_id: number;
  timestamp: string;
  actor: string;
  action_type: string;
  order_id: number;
  old_status: string;
  new_status: string;
  description: string;
}

export interface OrderDetail {
  order: Order;
  history: LogEntry[];
}

export interface Stats {
  total_orders: number;
  pending: number;
  processing: number;
  shipped: number;
  delivered: number;
  cancelled: number;
  total_sales_pkr: number;
  queue_size: number;
  log_entries: number;
  trips: number;
  active_trips: number;
}

export interface Analytics {
  status_counts: Record<'pending' | 'processing' | 'shipped' | 'delivered' | 'cancelled', number>;
  daily: { date: string; orders: number; sales_pkr: number }[];
}

export interface Health {
  status: string;
  orders: number;
  trips: number;
  logs: number;
  recovery: {
    leftover_tmp_files: number;
    corrupt_log_lines: number;
    corrupt_trip_lines: number;
    warnings: string[];
  };
}

/** GET /api/queue returns the Pending orders front → rear, as plain orders. */

// ---- Dashboard ----

export const getHealth = () => request<Health>('/health');
export const getStats = () => request<Stats>('/stats');
export const getAnalytics = () => request<Analytics>('/analytics');
export const getLogs = (params?: { order_id?: number; action_type?: string; from?: string; to?: string }) => {
  const q = new URLSearchParams();
  if (params?.order_id !== undefined) q.set('order_id', String(params.order_id));
  if (params?.action_type) q.set('action_type', params.action_type);
  if (params?.from) q.set('from', params.from);
  if (params?.to) q.set('to', params.to);
  const s = q.toString();
  return request<LogEntry[]>(`/logs${s ? `?${s}` : ''}`);
};

/** One log entry by ID — 404 if missing (GET /api/logs/:id). */
export const getLog = (id: number) => request<LogEntry>(`/logs/${id}`);

// ---- Trees + benchmark (Phase 4B-4) ----

export interface LogTreeNode {
  log_id: number;
  timestamp: string;
  action_type: string;
  order_id: number;
  left: LogTreeNode | null;
  right: LogTreeNode | null;
}

export interface LogTree {
  root: LogTreeNode | null;
  size: number;
  height: number;
}

export interface OrderTreeNode {
  order_id: number;
  customer_name: string;
  city: string;
  status: OrderStatus;
  left: OrderTreeNode | null;
  right: OrderTreeNode | null;
}

export interface OrderTree {
  root: OrderTreeNode | null;
  size: number;
  height: number;
}

export const getLogTree = () => request<LogTree>('/logs/tree');
export const getOrderTree = () => request<OrderTree>('/orders/tree');

export interface Benchmark {
  searches: number;
  linked_list_steps: number;
  bst_steps: number;
  bst_search_1024_steps: number;
  note?: string;
}

export const getBenchmark = () => request<Benchmark>('/benchmark');

// ---- Orders ----

export interface OrderFilters {
  status?: string;
  city?: string;
  date?: string;
}

export const getOrders = (filters?: OrderFilters) => {
  const q = new URLSearchParams();
  if (filters?.status) q.set('status', filters.status);
  if (filters?.city) q.set('city', filters.city);
  if (filters?.date) q.set('date', filters.date);
  const s = q.toString();
  return request<Order[]>(`/orders${s ? `?${s}` : ''}`);
};

/** BST search by ID on the backend (Store::findOrder). */
export const getOrder = (id: number) => request<OrderDetail>(`/orders/${id}`);

export interface NewOrder {
  customer_name: string;
  product: string;
  category: string;
  price_pkr: number;
  quantity: number;
  order_date: string;
  city: string;
}

export const createOrder = (body: NewOrder) =>
  request<Order>('/orders', { method: 'POST', body: JSON.stringify(body) });

export const updateOrderStatus = (id: number, status: OrderStatus) =>
  request<Order>(`/orders/${id}/status`, { method: 'PATCH', body: JSON.stringify({ status }) });

export const cancelOrder = (id: number) =>
  request<Order>(`/orders/${id}/cancel`, { method: 'POST' });

/** DELETE returns {"deleted": id}. */
export const deleteOrder = (id: number) =>
  request<{ deleted: number }>(`/orders/${id}`, { method: 'DELETE' });

// ---- Queue / Undo (client functions for later phases; no UI wires them yet) ----

export const getQueue = () => request<Order[]>('/queue');
export const processNext = () => request<Order>('/queue/process', { method: 'POST' });
export const undo = () => request<{ undone: boolean }>('/undo', { method: 'POST' });

// ---- Delivery (Phase 4B-3) ----

export type Dataset = 'production' | 'test';

export interface GraphEdge {
  from: string;
  to: string;
  time_min: number;
  distance_km: number;
}

export interface GraphData {
  dataset: string;
  cities: string[];
  edges: GraphEdge[];
}

export interface IntermediateMatch {
  city: string;
  time_to_city_min: number;
  distance_to_city_km: number;
  orders: Order[];
}

export interface PlanResult {
  source: string;
  destination: string;
  path: string[];
  total_time_min: number;
  total_distance_km: number;
  intermediate: IntermediateMatch[];
  destination_orders: Order[];
  log_warning?: string;
}

export interface DetourResult {
  detour_city: string;
  original: { time_min: number; distance_km: number };
  via: { time_min: number; distance_km: number };
  extra_time_min: number;
  extra_distance_km: number;
  eligible_orders: Order[];
  log_warning?: string;
}

export interface MultiStopResult {
  stop_order: string[];
  path: string[];
  total_time_min: number;
  total_distance_km: number;
  log_warning?: string;
}

export type TripStatus = 'Planned' | 'Completed' | 'Cancelled';

export interface Trip {
  trip_id: number;
  source: string;
  destination: string;
  route: string[];
  assigned_order_ids: number[];
  time_min: number;
  distance_km: number;
  status: TripStatus;
}

export const getGraph = (dataset: Dataset = 'production') =>
  request<GraphData>(`/delivery/graph?dataset=${dataset}`);

export const getPlan = (source: string, destination: string, dataset: Dataset = 'production', audit = true) =>
  request<PlanResult>(`/delivery/plan?source=${encodeURIComponent(source)}&destination=${encodeURIComponent(destination)}&dataset=${dataset}${audit ? '' : '&audit=false'}`);

export const getDetour = (source: string, destination: string, via: string, dataset: Dataset = 'production', audit = true) =>
  request<DetourResult>(`/delivery/detour?source=${encodeURIComponent(source)}&destination=${encodeURIComponent(destination)}&via=${encodeURIComponent(via)}&dataset=${dataset}${audit ? '' : '&audit=false'}`);

export const postMultiStop = (source: string, destination: string, stops: string[], dataset: Dataset = 'production', audit = true) =>
  request<MultiStopResult>('/delivery/multi-stop', {
    method: 'POST',
    body: JSON.stringify({ source, destination, stops, dataset, ...(audit ? {} : { audit: false }) }),
  });

export const getTrips = () => request<Trip[]>('/delivery/trips');

export const createTrip = (source: string, destination: string, stops: string[], orderIds: number[]) =>
  request<Trip>('/delivery/trips', {
    method: 'POST',
    body: JSON.stringify({ source, destination, stops, order_ids: orderIds }),
  });

export const completeTrip = (id: number) =>
  request<Trip>(`/delivery/trips/${id}/complete`, { method: 'POST' });

export const cancelTrip = (id: number) =>
  request<Trip>(`/delivery/trips/${id}/cancel`, { method: 'POST' });

// ---- Formatting helpers (presentation only) ----

export const formatPkr = (n: number) => `Rs. ${n.toLocaleString('en-PK')}`;
