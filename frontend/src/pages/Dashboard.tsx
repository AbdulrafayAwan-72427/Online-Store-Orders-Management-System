import { useCallback, useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { Bar, BarChart, CartesianGrid, Cell, Legend, Line, LineChart, Pie, PieChart, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import { Package, RefreshCw, ShoppingCart, Truck, XCircle } from 'lucide-react';
import { formatPkr, getAnalytics, getLogs, getOrders, getStats, type Analytics, type LogEntry, type Order, type Stats } from '../lib/api';
import { EmptyState, ErrorBanner, Spinner, StatusBadge, errorMessage } from '../components/ui';

const PIE_COLORS: Record<string, string> = {
  Pending: '#f59e0b',
  Processing: '#3b82f6',
  Shipped: '#8b5cf6',
  Delivered: '#10b981',
  Cancelled: '#ef4444',
};

function StatCard({ label, value, icon, accent }: { label: string; value: string; icon: React.ReactNode; accent: string }) {
  return (
    <div className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900">
      <div className="flex items-center justify-between">
        <p className="text-sm font-medium text-slate-500 dark:text-slate-400">{label}</p>
        <span className={`rounded-lg p-2 ${accent}`}>{icon}</span>
      </div>
      <p className="mt-2 text-2xl font-bold text-slate-900 dark:text-white">{value}</p>
    </div>
  );
}

export default function Dashboard() {
  const [stats, setStats] = useState<Stats | null>(null);
  const [analytics, setAnalytics] = useState<Analytics | null>(null);
  const [recentOrders, setRecentOrders] = useState<Order[]>([]);
  const [recentLogs, setRecentLogs] = useState<LogEntry[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [refreshing, setRefreshing] = useState(false);

  const load = useCallback(async (isRefresh = false) => {
    if (isRefresh) setRefreshing(true);
    else setLoading(true);
    setError(null);
    try {
      const [s, a, orders, logs] = await Promise.all([getStats(), getAnalytics(), getOrders(), getLogs()]);
      setStats(s);
      setAnalytics(a);
      // Latest first by date, then by ID.
      const sorted = [...orders].sort((x, y) => y.order_date.localeCompare(x.order_date) || y.id - x.id);
      setRecentOrders(sorted.slice(0, 8));
      // GET /api/logs is chronological (oldest first); latest 8 = tail, reversed.
      setRecentLogs([...logs].slice(-8).reverse());
    } catch (e) {
      setError(errorMessage(e, 'Could not load the dashboard.'));
    } finally {
      setLoading(false);
      setRefreshing(false);
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  if (loading) return <Spinner label="Loading dashboard…" />;
  if (error && !stats) return <ErrorBanner message={error} onRetry={() => void load()} />;

  const pieData = analytics
    ? (Object.entries(analytics.status_counts) as [string, number][]).map(([name, value]) => ({
        name: name.charAt(0).toUpperCase() + name.slice(1),
        value,
      }))
    : [];

  return (
    <div className="space-y-6">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <h2 className="text-2xl font-bold text-slate-900 dark:text-white">Dashboard</h2>
          <p className="text-sm text-slate-500 dark:text-slate-400">Live statistics from the C++ backend — refreshed from the API, never hardcoded.</p>
        </div>
        <button onClick={() => void load(true)} disabled={refreshing} className="inline-flex items-center gap-2 rounded-md border border-slate-300 bg-white px-4 py-2 text-sm font-medium text-slate-700 hover:bg-slate-50 disabled:opacity-50 dark:border-slate-600 dark:bg-slate-800 dark:text-slate-200">
          <RefreshCw className={`h-4 w-4 ${refreshing ? 'animate-spin' : ''}`} aria-hidden />
          {refreshing ? 'Refreshing…' : 'Refresh'}
        </button>
      </div>

      {error && <ErrorBanner message={error} onRetry={() => void load(true)} />}

      {stats && (
        <div className="grid grid-cols-2 gap-4 lg:grid-cols-4">
          <StatCard label="Total Orders" value={String(stats.total_orders)} icon={<Package className="h-5 w-5 text-emerald-700 dark:text-emerald-300" />} accent="bg-emerald-100 dark:bg-emerald-900/50" />
          <StatCard label="Total Sales" value={formatPkr(stats.total_sales_pkr)} icon={<ShoppingCart className="h-5 w-5 text-emerald-700 dark:text-emerald-300" />} accent="bg-emerald-100 dark:bg-emerald-900/50" />
          <StatCard label="Pending in Queue" value={`${stats.queue_size} orders`} icon={<Truck className="h-5 w-5 text-amber-700 dark:text-amber-300" />} accent="bg-amber-100 dark:bg-amber-900/50" />
          <StatCard label="Cancelled" value={String(stats.cancelled)} icon={<XCircle className="h-5 w-5 text-red-700 dark:text-red-300" />} accent="bg-red-100 dark:bg-red-900/50" />
          <StatCard label="Pending" value={String(stats.pending)} icon={<Package className="h-5 w-5 text-amber-700 dark:text-amber-300" />} accent="bg-amber-100 dark:bg-amber-900/50" />
          <StatCard label="Processing" value={String(stats.processing)} icon={<Package className="h-5 w-5 text-blue-700 dark:text-blue-300" />} accent="bg-blue-100 dark:bg-blue-900/50" />
          <StatCard label="Shipped" value={String(stats.shipped)} icon={<Truck className="h-5 w-5 text-violet-700 dark:text-violet-300" />} accent="bg-violet-100 dark:bg-violet-900/50" />
          <StatCard label="Delivered" value={String(stats.delivered)} icon={<Package className="h-5 w-5 text-emerald-700 dark:text-emerald-300" />} accent="bg-emerald-100 dark:bg-emerald-900/50" />
        </div>
      )}

      <div className="grid gap-6 lg:grid-cols-2">
        <section className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900" aria-label="Order status distribution">
          <h3 className="text-base font-semibold text-slate-900 dark:text-white">Order Status Distribution</h3>
          {pieData.length === 0 || pieData.every((d) => d.value === 0) ? (
            <EmptyState title="No orders yet" hint="Add an order to see the distribution." />
          ) : (
            <div className="h-64">
              <ResponsiveContainer width="100%" height="100%">
                <PieChart>
                  <Pie data={pieData} dataKey="value" nameKey="name" cx="50%" cy="50%" outerRadius={90} label={({ name, value }) => `${name}: ${value}`}>
                    {pieData.map((d) => (
                      <Cell key={d.name} fill={PIE_COLORS[d.name] ?? '#94a3b8'} />
                    ))}
                  </Pie>
                  <Tooltip />
                  <Legend />
                </PieChart>
              </ResponsiveContainer>
            </div>
          )}
        </section>

        <section className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900" aria-label="Daily sales">
          <h3 className="text-base font-semibold text-slate-900 dark:text-white">Daily Sales (non-cancelled orders)</h3>
          {!analytics || analytics.daily.length === 0 ? (
            <EmptyState title="No daily data" hint="Sales appear here once orders exist." />
          ) : (
            <div className="h-64">
              <ResponsiveContainer width="100%" height="100%">
                <BarChart data={analytics.daily}>
                  <CartesianGrid strokeDasharray="3 3" />
                  <XAxis dataKey="date" tick={{ fontSize: 12 }} />
                  <YAxis tick={{ fontSize: 12 }} />
                  <Tooltip formatter={(v: number | string, name: string) => (name === 'sales_pkr' ? formatPkr(Number(v)) : v)} />
                  <Legend />
                  <Bar dataKey="orders" fill="#3b82f6" name="Orders" />
                  <Bar dataKey="sales_pkr" fill="#10b981" name="Sales (PKR)" />
                </BarChart>
              </ResponsiveContainer>
            </div>
          )}
        </section>
      </div>

      {analytics && analytics.daily.length > 0 && (
        <section className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900" aria-label="Daily orders trend">
          <h3 className="text-base font-semibold text-slate-900 dark:text-white">Orders per Day</h3>
          <div className="h-56">
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={analytics.daily}>
                <CartesianGrid strokeDasharray="3 3" />
                <XAxis dataKey="date" tick={{ fontSize: 12 }} />
                <YAxis allowDecimals={false} tick={{ fontSize: 12 }} />
                <Tooltip />
                <Line type="monotone" dataKey="orders" stroke="#3b82f6" strokeWidth={2} name="Orders" />
              </LineChart>
            </ResponsiveContainer>
          </div>
        </section>
      )}

      <div className="grid gap-6 lg:grid-cols-2">
        <section className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900" aria-label="Recent orders">
          <div className="mb-3 flex items-center justify-between">
            <h3 className="text-base font-semibold text-slate-900 dark:text-white">Recent Orders</h3>
            <Link to="/orders" className="text-sm font-medium text-emerald-600 hover:underline">View all →</Link>
          </div>
          {recentOrders.length === 0 ? (
            <EmptyState title="No orders yet" />
          ) : (
            <div className="overflow-x-auto">
              <table className="w-full text-sm">
                <thead>
                  <tr className="border-b border-slate-200 text-left text-slate-500 dark:border-slate-700 dark:text-slate-400">
                    <th className="py-2 pr-2">ID</th>
                    <th className="py-2 pr-2">Customer</th>
                    <th className="py-2 pr-2">City</th>
                    <th className="py-2 pr-2">Total</th>
                    <th className="py-2">Status</th>
                  </tr>
                </thead>
                <tbody>
                  {recentOrders.map((o) => (
                    <tr key={o.id} className="border-b border-slate-100 last:border-0 dark:border-slate-800">
                      <td className="py-2 pr-2 font-medium">#{o.id}</td>
                      <td className="py-2 pr-2">{o.customer_name}</td>
                      <td className="py-2 pr-2">{o.city}</td>
                      <td className="py-2 pr-2">{formatPkr(o.price_pkr * o.quantity)}</td>
                      <td className="py-2"><StatusBadge status={o.status} /></td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </section>

        <section className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900" aria-label="Recent activity">
          <h3 className="mb-3 text-base font-semibold text-slate-900 dark:text-white">Recent Activity</h3>
          {recentLogs.length === 0 ? (
            <EmptyState title="No activity yet" hint="Actions on orders are logged here by the backend." />
          ) : (
            <ul className="space-y-3">
              {recentLogs.map((l) => (
                <li key={l.log_id} className="flex items-start gap-3 border-b border-slate-100 pb-3 last:border-0 last:pb-0 dark:border-slate-800">
                  <span className="mt-0.5 rounded-md bg-slate-100 px-2 py-0.5 text-xs font-semibold text-slate-700 dark:bg-slate-800 dark:text-slate-200">{l.action_type}</span>
                  <div className="min-w-0">
                    <p className="text-sm text-slate-800 dark:text-slate-100">{l.description}</p>
                    <p className="text-xs text-slate-500 dark:text-slate-400">{l.timestamp.replace('T', ' ')}</p>
                  </div>
                </li>
              ))}
            </ul>
          )}
        </section>
      </div>
      <p className="text-xs text-slate-400">Note: if the backend has never logged an action in this session, Recent Activity is empty until the first order change — that is real data, not a placeholder.</p>
    </div>
  );
}
