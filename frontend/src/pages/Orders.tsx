import { useCallback, useEffect, useState } from 'react';
import { Eye, Plus, RotateCcw, Search, Trash2, XCircle } from 'lucide-react';
import {
  ALL_STATUSES, CITIES, NEXT_STATUSES, cancelOrder, createOrder, deleteOrder, formatPkr,
  getOrder, getOrders, updateOrderStatus,
  type NewOrder, type Order, type OrderDetail, type OrderStatus,
} from '../lib/api';
import { ConfirmDialog, EmptyState, ErrorBanner, Modal, Spinner, StatusBadge, buttonPrimary, buttonSecondary, errorMessage, inputClass } from '../components/ui';

const EMPTY_FORM: NewOrder = {
  customer_name: '',
  product: '',
  category: '',
  price_pkr: 0,
  quantity: 1,
  order_date: new Date().toISOString().slice(0, 10),
  city: 'Lahore',
};

export default function Orders() {
  const [orders, setOrders] = useState<Order[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);

  const [statusFilter, setStatusFilter] = useState('');
  const [cityFilter, setCityFilter] = useState('');
  const [dateFilter, setDateFilter] = useState('');
  const [searchId, setSearchId] = useState('');
  const [searchActive, setSearchActive] = useState(false);
  const [searchError, setSearchError] = useState<string | null>(null);

  const [detail, setDetail] = useState<OrderDetail | null>(null);
  const [detailLoading, setDetailLoading] = useState(false);
  const [showAdd, setShowAdd] = useState(false);
  const [form, setForm] = useState<NewOrder>(EMPTY_FORM);
  const [formError, setFormError] = useState<string | null>(null);
  const [formBusy, setFormBusy] = useState(false);

  const [confirm, setConfirm] = useState<{ kind: 'cancel' | 'delete'; order: Order } | null>(null);
  const [actionBusy, setActionBusy] = useState(false);
  const [statusBusyId, setStatusBusyId] = useState<number | null>(null);

  const load = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const data = await getOrders({
        status: statusFilter || undefined,
        city: cityFilter || undefined,
        date: dateFilter || undefined,
      });
      setOrders(data);
    } catch (e) {
      setError(errorMessage(e, 'Could not load orders.'));
    } finally {
      setLoading(false);
    }
  }, [statusFilter, cityFilter, dateFilter]);

  useEffect(() => {
    if (!searchActive) void load();
  }, [load, searchActive]);

  const refresh = () => {
    setSearchActive(false);
    setSearchId('');
    setSearchError(null);
    void load();
  };

  const runSearch = async () => {
    const id = Number(searchId.trim());
    if (!searchId.trim() || !Number.isInteger(id) || id <= 0) {
      setSearchError('Enter a valid order ID (a positive number).');
      return;
    }
    setSearchError(null);
    setLoading(true);
    try {
      // BST search on the backend.
      const d = await getOrder(id);
      setOrders([d.order]);
      setSearchActive(true);
    } catch (e) {
      setSearchError(errorMessage(e, 'Order not found.'));
      setOrders([]);
      setSearchActive(true);
    } finally {
      setLoading(false);
    }
  };

  const openDetail = async (id: number) => {
    setDetailLoading(true);
    setDetail(null);
    try {
      const d = await getOrder(id);
      setDetail(d);
    } catch (e) {
      setError(errorMessage(e, 'Could not load order details.'));
    } finally {
      setDetailLoading(false);
    }
  };

  const submitAdd = async (e: React.FormEvent) => {
    e.preventDefault();
    setFormError(null);
    if (!form.customer_name.trim() || !form.product.trim() || !form.category.trim() || !form.city.trim() || !form.order_date.trim()) {
      setFormError('Customer, product, category, city and date must not be empty.');
      return;
    }
    if (!/^\d{4}-\d{2}-\d{2}$/.test(form.order_date)) {
      setFormError('Date must be in YYYY-MM-DD format.');
      return;
    }
    if (form.price_pkr <= 0) {
      setFormError('Price must be greater than 0.');
      return;
    }
    if (form.quantity < 1) {
      setFormError('Quantity must be 1 or more.');
      return;
    }
    setFormBusy(true);
    try {
      const created = await createOrder({ ...form, price_pkr: Number(form.price_pkr), quantity: Number(form.quantity) });
      setNotice(`Order #${created.id} created (Pending).`);
      setShowAdd(false);
      setForm(EMPTY_FORM);
      refresh();
    } catch (err) {
      // Server 400 message shown verbatim; UI is NOT updated on failure.
      setFormError(errorMessage(err, 'Could not create the order.'));
    } finally {
      setFormBusy(false);
    }
  };

  const changeStatus = async (order: Order, next: OrderStatus) => {
    setStatusBusyId(order.id);
    setError(null);
    try {
      await updateOrderStatus(order.id, next);
      setNotice(`Order #${order.id} is now ${next}.`);
      refresh();
    } catch (e) {
      setError(errorMessage(e, 'Status change failed.'));
    } finally {
      setStatusBusyId(null);
    }
  };

  const runConfirm = async () => {
    if (!confirm) return;
    setActionBusy(true);
    try {
      if (confirm.kind === 'cancel') {
        await cancelOrder(confirm.order.id);
        setNotice(`Order #${confirm.order.id} cancelled.`);
      } else {
        await deleteOrder(confirm.order.id);
        setNotice(`Order #${confirm.order.id} deleted.`);
      }
      setConfirm(null);
      refresh();
    } catch (e) {
      setError(errorMessage(e, 'That action failed — nothing was changed.'));
      setConfirm(null);
    } finally {
      setActionBusy(false);
    }
  };

  return (
    <div className="space-y-4">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <h2 className="text-2xl font-bold text-slate-900 dark:text-white">Orders</h2>
          <p className="text-sm text-slate-500 dark:text-slate-400">
            {searchActive ? 'Showing the order found by ID search (BST).' : `${orders.length} order${orders.length === 1 ? '' : 's'} shown.`}
          </p>
        </div>
        <button onClick={() => { setShowAdd(true); setFormError(null); }} className={`inline-flex items-center gap-2 ${buttonPrimary}`}>
          <Plus className="h-4 w-4" aria-hidden /> Add Order
        </button>
      </div>

      {notice && (
        <div className="rounded-lg border border-emerald-200 bg-emerald-50 px-4 py-3 text-sm text-emerald-800 dark:border-emerald-900 dark:bg-emerald-950/60 dark:text-emerald-200" role="status">
          {notice}
          <button onClick={() => setNotice(null)} className="float-right font-semibold" aria-label="Dismiss">×</button>
        </div>
      )}
      {error && <ErrorBanner message={error} onRetry={refresh} />}

      {/* Filters */}
      <div className="flex flex-wrap items-end gap-3 rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900">
        <label className="flex flex-col gap-1 text-xs font-medium text-slate-600 dark:text-slate-300">
          Status
          <select value={statusFilter} onChange={(e) => setStatusFilter(e.target.value)} disabled={searchActive} className={inputClass} aria-label="Filter by status">
            <option value="">All statuses</option>
            {ALL_STATUSES.map((s) => <option key={s} value={s}>{s}</option>)}
          </select>
        </label>
        <label className="flex flex-col gap-1 text-xs font-medium text-slate-600 dark:text-slate-300">
          City
          <select value={cityFilter} onChange={(e) => setCityFilter(e.target.value)} disabled={searchActive} className={inputClass} aria-label="Filter by city">
            <option value="">All cities</option>
            {CITIES.map((c) => <option key={c} value={c}>{c}</option>)}
          </select>
        </label>
        <label className="flex flex-col gap-1 text-xs font-medium text-slate-600 dark:text-slate-300">
          Date
          <input type="date" value={dateFilter} onChange={(e) => setDateFilter(e.target.value)} disabled={searchActive} className={inputClass} aria-label="Filter by date" />
        </label>
        {(statusFilter || cityFilter || dateFilter) && !searchActive && (
          <button onClick={() => { setStatusFilter(''); setCityFilter(''); setDateFilter(''); }} className={buttonSecondary}>
            Clear filters
          </button>
        )}
        <div className="ml-auto flex items-end gap-2">
          <label className="flex flex-col gap-1 text-xs font-medium text-slate-600 dark:text-slate-300">
            Search by ID (BST)
            <span className="flex gap-2">
              <input
                value={searchId}
                onChange={(e) => setSearchId(e.target.value)}
                onKeyDown={(e) => { if (e.key === 'Enter') void runSearch(); }}
                placeholder="e.g. 1024"
                inputMode="numeric"
                className={inputClass}
                aria-label="Search order by ID"
              />
              <button onClick={() => void runSearch()} className={buttonPrimary} aria-label="Search">
                <Search className="h-4 w-4" aria-hidden />
              </button>
            </span>
          </label>
          {searchActive && (
            <button onClick={refresh} className={`inline-flex items-center gap-1 ${buttonSecondary}`}>
              <RotateCcw className="h-4 w-4" aria-hidden /> Clear search
            </button>
          )}
        </div>
      </div>
      {searchError && <ErrorBanner message={searchError} />}

      {/* Table */}
      {loading ? (
        <Spinner label="Loading orders…" />
      ) : orders.length === 0 ? (
        <div className="rounded-xl border border-slate-200 bg-white dark:border-slate-700 dark:bg-slate-900">
          <EmptyState title={searchActive ? 'No order with that ID' : 'No orders match these filters'} hint={searchActive ? 'Check the ID and try again, or clear the search.' : 'Try clearing a filter.'} />
        </div>
      ) : (
        <div className="overflow-x-auto rounded-xl border border-slate-200 bg-white shadow-sm dark:border-slate-700 dark:bg-slate-900">
          <table className="w-full min-w-[900px] text-sm">
            <thead>
              <tr className="border-b border-slate-200 text-left text-slate-500 dark:border-slate-700 dark:text-slate-400">
                <th className="px-4 py-3">ID</th>
                <th className="px-4 py-3">Customer</th>
                <th className="px-4 py-3">Product</th>
                <th className="px-4 py-3">City</th>
                <th className="px-4 py-3">Date</th>
                <th className="px-4 py-3">Total</th>
                <th className="px-4 py-3">Status</th>
                <th className="px-4 py-3">Actions</th>
              </tr>
            </thead>
            <tbody>
              {orders.map((o) => {
                const nexts = NEXT_STATUSES[o.status];
                return (
                  <tr key={o.id} className="border-b border-slate-100 last:border-0 hover:bg-slate-50 dark:border-slate-800 dark:hover:bg-slate-800/50">
                    <td className="px-4 py-3 font-medium">#{o.id}</td>
                    <td className="px-4 py-3">{o.customer_name}</td>
                    <td className="px-4 py-3">{o.product} <span className="text-slate-400">({o.category})</span></td>
                    <td className="px-4 py-3">{o.city}</td>
                    <td className="px-4 py-3">{o.order_date}</td>
                    <td className="px-4 py-3">{formatPkr(o.price_pkr * o.quantity)}</td>
                    <td className="px-4 py-3"><StatusBadge status={o.status} /></td>
                    <td className="px-4 py-3">
                      <div className="flex flex-wrap items-center gap-2">
                        <button onClick={() => void openDetail(o.id)} className="inline-flex items-center gap-1 rounded-md border border-slate-300 px-2 py-1 text-xs font-medium hover:bg-slate-50 dark:border-slate-600 dark:hover:bg-slate-700" aria-label={`View order ${o.id}`}>
                          <Eye className="h-3.5 w-3.5" aria-hidden /> View
                        </button>
                        {nexts.length > 0 && (
                          <select
                            value=""
                            disabled={statusBusyId === o.id}
                            onChange={(e) => { if (e.target.value) void changeStatus(o, e.target.value as OrderStatus); }}
                            className="rounded-md border border-slate-300 bg-white px-2 py-1 text-xs dark:border-slate-600 dark:bg-slate-700"
                            aria-label={`Change status of order ${o.id}`}
                          >
                            <option value="">Move to…</option>
                            {nexts.map((s) => <option key={s} value={s}>{s}</option>)}
                          </select>
                        )}
                        {o.status !== 'Cancelled' && o.status !== 'Delivered' && (
                          <button onClick={() => setConfirm({ kind: 'cancel', order: o })} className="inline-flex items-center gap-1 rounded-md border border-amber-300 px-2 py-1 text-xs font-medium text-amber-700 hover:bg-amber-50 dark:border-amber-800 dark:text-amber-300" aria-label={`Cancel order ${o.id}`}>
                            <XCircle className="h-3.5 w-3.5" aria-hidden /> Cancel
                          </button>
                        )}
                        <button onClick={() => setConfirm({ kind: 'delete', order: o })} className="inline-flex items-center gap-1 rounded-md border border-red-300 px-2 py-1 text-xs font-medium text-red-700 hover:bg-red-50 dark:border-red-800 dark:text-red-300" aria-label={`Delete order ${o.id}`}>
                          <Trash2 className="h-3.5 w-3.5" aria-hidden /> Delete
                        </button>
                      </div>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      )}

      {/* Detail modal */}
      {(detailLoading || detail) && (
        <Modal title={detail ? `Order #${detail.order.id}` : 'Loading order…'} onClose={() => setDetail(null)}>
          {detailLoading && <Spinner label="Loading order details…" />}
          {detail && (
            <div className="space-y-4">
              <dl className="grid grid-cols-2 gap-3 text-sm sm:grid-cols-3">
                <div><dt className="text-slate-500 dark:text-slate-400">Customer</dt><dd className="font-medium">{detail.order.customer_name}</dd></div>
                <div><dt className="text-slate-500 dark:text-slate-400">Product</dt><dd className="font-medium">{detail.order.product}</dd></div>
                <div><dt className="text-slate-500 dark:text-slate-400">Category</dt><dd className="font-medium">{detail.order.category}</dd></div>
                <div><dt className="text-slate-500 dark:text-slate-400">Price</dt><dd className="font-medium">{formatPkr(detail.order.price_pkr)} × {detail.order.quantity}</dd></div>
                <div><dt className="text-slate-500 dark:text-slate-400">Total</dt><dd className="font-medium">{formatPkr(detail.order.price_pkr * detail.order.quantity)}</dd></div>
                <div><dt className="text-slate-500 dark:text-slate-400">City</dt><dd className="font-medium">{detail.order.city}</dd></div>
                <div><dt className="text-slate-500 dark:text-slate-400">Date</dt><dd className="font-medium">{detail.order.order_date}</dd></div>
                <div><dt className="text-slate-500 dark:text-slate-400">Status</dt><dd><StatusBadge status={detail.order.status} /></dd></div>
              </dl>
              <div>
                <h3 className="mb-2 font-semibold text-slate-900 dark:text-white">History (from the Activity Log BST)</h3>
                {detail.history.length === 0 ? (
                  <p className="text-sm text-slate-500 dark:text-slate-400">No log entries for this order yet.</p>
                ) : (
                  <ul className="space-y-2">
                    {detail.history.map((h) => (
                      <li key={h.log_id} className="rounded-lg border border-slate-200 p-3 text-sm dark:border-slate-700">
                        <span className="font-semibold">{h.action_type}</span>
                        <span className="text-slate-500 dark:text-slate-400"> — {h.description}</span>
                        <span className="block text-xs text-slate-400">{h.timestamp.replace('T', ' ')}</span>
                      </li>
                    ))}
                  </ul>
                )}
              </div>
            </div>
          )}
        </Modal>
      )}

      {/* Add order modal */}
      {showAdd && (
        <Modal title="Add Order" onClose={() => setShowAdd(false)}>
          <form onSubmit={(e) => void submitAdd(e)} className="space-y-3">
            {formError && <ErrorBanner message={formError} />}
            <div className="grid gap-3 sm:grid-cols-2">
              <label className="flex flex-col gap-1 text-sm font-medium">Customer name
                <input required value={form.customer_name} onChange={(e) => setForm({ ...form, customer_name: e.target.value })} className={inputClass} />
              </label>
              <label className="flex flex-col gap-1 text-sm font-medium">Product
                <input required value={form.product} onChange={(e) => setForm({ ...form, product: e.target.value })} className={inputClass} />
              </label>
              <label className="flex flex-col gap-1 text-sm font-medium">Category
                <input required value={form.category} onChange={(e) => setForm({ ...form, category: e.target.value })} className={inputClass} />
              </label>
              <label className="flex flex-col gap-1 text-sm font-medium">City
                <select value={form.city} onChange={(e) => setForm({ ...form, city: e.target.value })} className={inputClass}>
                  {CITIES.map((c) => <option key={c} value={c}>{c}</option>)}
                </select>
              </label>
              <label className="flex flex-col gap-1 text-sm font-medium">Price (PKR)
                <input required type="number" min={1} value={form.price_pkr || ''} onChange={(e) => setForm({ ...form, price_pkr: Number(e.target.value) })} className={inputClass} />
              </label>
              <label className="flex flex-col gap-1 text-sm font-medium">Quantity
                <input required type="number" min={1} value={form.quantity} onChange={(e) => setForm({ ...form, quantity: Number(e.target.value) })} className={inputClass} />
              </label>
              <label className="flex flex-col gap-1 text-sm font-medium">Order date
                <input required type="date" value={form.order_date} onChange={(e) => setForm({ ...form, order_date: e.target.value })} className={inputClass} />
              </label>
            </div>
            <p className="text-xs text-slate-500 dark:text-slate-400">New orders start as Pending and join the processing queue. The backend assigns the next order ID.</p>
            <div className="flex justify-end gap-2">
              <button type="button" onClick={() => setShowAdd(false)} className={buttonSecondary}>Cancel</button>
              <button type="submit" disabled={formBusy} className={buttonPrimary}>{formBusy ? 'Adding…' : 'Add order'}</button>
            </div>
          </form>
        </Modal>
      )}

      {confirm && (
        <ConfirmDialog
          title={confirm.kind === 'cancel' ? `Cancel order #${confirm.order.id}?` : `Delete order #${confirm.order.id}?`}
          message={
            confirm.kind === 'cancel'
              ? `Order #${confirm.order.id} (${confirm.order.customer_name}, ${confirm.order.product}) will be marked Cancelled. The backend records it in the activity log and it can be restored with Undo via the API.`
              : `Order #${confirm.order.id} (${confirm.order.customer_name}, ${confirm.order.product}) will be permanently deleted from the list, BST and CSV. This cannot be undone.`
          }
          confirmLabel={confirm.kind === 'cancel' ? 'Cancel order' : 'Delete order'}
          onConfirm={() => void runConfirm()}
          onCancel={() => setConfirm(null)}
          busy={actionBusy}
        />
      )}
    </div>
  );
}
