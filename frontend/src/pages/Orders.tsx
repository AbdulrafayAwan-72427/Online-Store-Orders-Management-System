import { useCallback, useEffect, useRef, useState } from 'react';
import { ArrowRight, ChevronDown, ChevronUp, Eye, FileText, Plus, RefreshCw, RotateCcw, Search, Trash2, XCircle } from 'lucide-react';
import {
  ALL_STATUSES, CITIES, NEXT_STATUSES, cancelOrder, createOrder, deleteOrder, formatPkr,
  getCsvPreview, getOrder, getOrders, getOrderTree, getQueue, updateOrderStatus,
  type CsvPreview, type NewOrder, type Order, type OrderDetail, type OrderStatus, type OrderTree,
} from '../lib/api';
import {
  PanelRequestGuard, SelectionRequestGuard, changedRowId, lastChangeMessage, listPosition, loadPanelData, loadSelectionData, parseCsvRowId, previewRowFor, queueNote, treeContains,
  type LastChange, type PreviewState,
} from '../lib/csvView';
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

// The real backend flow, in two stages (Store::loadOrders on
// startup, Store::saveAtomic on every successful change). The
// three data structures are parallel branches from the parsed
// orders — not a chain: loadOrders appends every order to the
// linked list, enqueues only the Pending IDs, and builds the
// Order BST from all the loaded orders. Kept in plain English for
// teaching; the tree is the Order BST, not an AST.
const LOAD_STEPS: { title: string; text: string }[] = [
  { title: 'orders.csv row', text: 'One line of text in the runtime file on disk.' },
  { title: 'parseOrder()', text: 'Splits the line on commas; converts order_id, price_pkr and quantity to numbers.' },
  { title: 'Order', text: 'The in-memory object with the 9 parsed fields.' },
];
const LOAD_BRANCHES: { title: string; text: string }[] = [
  { title: 'Linked List', text: 'Every parsed order is appended in file order — the source for listing orders and for saving.' },
  { title: 'Processing Queue', text: 'Only if the status is Pending: the order ID joins the FIFO queue.' },
  { title: 'Order BST', text: 'Built from all the loaded orders, middle-first, so search by ID stays fast.' },
];
const SAVE_STEPS: { title: string; text: string }[] = [
  { title: 'A normal Store action', text: 'e.g. change status: validated, list + BST updated together, queue fixed.' },
  { title: 'Saved back to orders.csv', text: 'The whole file is rewritten via a temp file, then atomically replaced; one activity-log line is appended.' },
];

function FlowBox({ title, text, className = '' }: { title: string; text: string; className?: string }) {
  return (
    <span className={`rounded-lg border border-slate-200 bg-slate-50 p-3 dark:border-slate-700 dark:bg-slate-800 ${className}`}>
      <span className="block text-sm font-semibold text-slate-900 dark:text-white">{title}</span>
      <span className="mt-1 block text-xs text-slate-500 dark:text-slate-400">{text}</span>
    </span>
  );
}

function FlowChain({ steps }: { steps: { title: string; text: string }[] }) {
  return (
    <div className="flex flex-wrap items-stretch gap-2">
      {steps.map((step, i) => (
        <span key={step.title} className="contents">
          <FlowBox title={step.title} text={step.text} className="w-40" />
          {i < steps.length - 1 && (
            <ArrowRight className="h-4 w-4 shrink-0 self-center text-slate-400" aria-hidden />
          )}
        </span>
      ))}
    </div>
  );
}

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

  // ---- "How CSV becomes orders" educational panel (read-only) ----
  // Everything shown here comes from existing read endpoints plus the
  // new bounded csv-preview endpoint. The panel never mutates
  // anything: status changes still go through the table controls.
  const [csvOpen, setCsvOpen] = useState(false);
  const panelOpened = useRef(false);
  const [preview, setPreview] = useState<CsvPreview | null>(null);
  const [previewLoading, setPreviewLoading] = useState(false);
  const [previewError, setPreviewError] = useState<string | null>(null);
  const [panelOrders, setPanelOrders] = useState<Order[]>([]);
  const [panelQueue, setPanelQueue] = useState<Order[]>([]);
  const [panelTree, setPanelTree] = useState<OrderTree | null>(null);
  const [selectedId, setSelectedId] = useState<number | null>(null);
  const [selDetail, setSelDetail] = useState<OrderDetail | null>(null);
  const [selLoading, setSelLoading] = useState(false);
  const [selError, setSelError] = useState<string | null>(null);
  const [lastChange, setLastChange] = useState<LastChange | null>(null);
  // True while the displayed preview may predate the last mutation:
  // the panel must not claim a refresh (or highlight a row) until
  // the file has actually been re-read successfully.
  const [previewStale, setPreviewStale] = useState(true);
  // Generation guard for panel loads: every load start and every
  // mutation bumps the generation, so only the newest request may
  // apply its result. A response that arrives after a newer load
  // or mutation started is discarded entirely (see loadPanelData
  // in csvView.ts) — it must not overwrite newer rows, clear the
  // stale flag, or produce a "refreshed" message.
  const panelGuard = useRef(new PanelRequestGuard());
  // Generation guard for inspector detail loads: a response is
  // applied only while no newer selection request has started AND
  // its order is still selected (see loadSelectionData in
  // csvView.ts). Selection changes are reported to the guard
  // synchronously (changeSelection below), so a late response for
  // a previously selected — or deleted — order is discarded
  // instead of repopulating the inspector.
  const selectionGuard = useRef(new SelectionRequestGuard());

  // Record a successful mutation for the panel: it replaces any
  // earlier "last change" report, invalidates any in-flight panel
  // load, and marks the preview stale until the file has been
  // successfully re-read.
  const noteMutation = useCallback((change: LastChange) => {
    panelGuard.current.invalidate();
    setPreviewStale(true);
    setLastChange(change);
  }, []);

  const loadPanel = useCallback(async () => {
    const token = panelGuard.current.begin();
    setPreviewLoading(true);
    setPreviewError(null);
    try {
      await loadPanelData(
        { getCsvPreview, getOrders, getQueue, getOrderTree },
        panelGuard.current,
        token,
        ({ preview: pv, orders: all, queue, tree }) => {
          setPreview(pv);
          setPanelOrders(all);
          setPanelQueue(queue);
          setPanelTree(tree);
          setPreviewStale(false);
        },
      );
      // If the load was discarded as stale, nothing above ran: the
      // newer request (or the mutation's own refresh) owns the
      // panel state, the stale flag, and the loading indicator.
    } catch (e) {
      if (!panelGuard.current.isCurrent(token)) return; // stale: discard silently
      setPreviewError(errorMessage(e, 'Could not load the CSV preview.'));
    } finally {
      if (panelGuard.current.isCurrent(token)) setPreviewLoading(false);
    }
  }, []);

  // Change (or clear) the inspected order. The guard is told
  // synchronously, so a detail response already in flight for the
  // previous selection is stale from this moment and cannot
  // repopulate the inspector when it arrives late.
  const changeSelection = (id: number | null) => {
    selectionGuard.current.selectionChanged(id);
    setSelectedId(id);
    if (id === null) {
      setSelDetail(null);
      setSelError(null);
      setSelLoading(false);
    }
  };

  const loadSelection = useCallback(async (id: number) => {
    const token = selectionGuard.current.begin(id);
    setSelLoading(true);
    setSelError(null);
    try {
      await loadSelectionData(getOrder, selectionGuard.current, token, id, (detail) => {
        setSelDetail(detail);
      });
      // If the response was discarded as stale, nothing above ran:
      // the newer selection (or its own load) owns the inspector,
      // the error banner, and the loading indicator.
    } catch (e) {
      if (!selectionGuard.current.isCurrent(token, id)) return; // stale: discard silently
      setSelDetail(null);
      setSelError(errorMessage(e, 'Could not load that order.'));
    } finally {
      if (selectionGuard.current.isCurrent(token, id)) setSelLoading(false);
    }
  }, []);

  // After any successful mutation the backend has rewritten
  // orders.csv, so re-read the panel data — but only once the panel
  // has been opened, to avoid four extra GETs on every action.
  const refreshPanelAfterMutation = useCallback((changedId?: number) => {
    if (!panelOpened.current) return;
    void loadPanel();
    if (changedId !== undefined && changedId === selectedId) void loadSelection(changedId);
  }, [loadPanel, loadSelection, selectedId]);

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

  useEffect(() => {
    if (selectedId !== null) void loadSelection(selectedId);
  }, [selectedId, loadSelection]);

  const toggleCsvPanel = () => {
    const next = !csvOpen;
    setCsvOpen(next);
    if (next) {
      panelOpened.current = true;
      void loadPanel();
    }
  };

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
      noteMutation({ kind: 'created', id: created.id });
      setShowAdd(false);
      setForm(EMPTY_FORM);
      refresh();
      refreshPanelAfterMutation();
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
    const before = order.status;
    try {
      await updateOrderStatus(order.id, next);
      setNotice(`Order #${order.id} is now ${next}.`);
      // The backend has rewritten orders.csv: record before/after for
      // the educational panel and refresh its live file preview.
      noteMutation({ kind: 'status', id: order.id, from: before, to: next });
      refresh();
      refreshPanelAfterMutation(order.id);
    } catch (e) {
      setError(errorMessage(e, 'Status change failed.'));
    } finally {
      setStatusBusyId(null);
    }
  };

  const runConfirm = async () => {
    if (!confirm) return;
    const { kind, order } = confirm;
    setActionBusy(true);
    try {
      if (kind === 'cancel') {
        await cancelOrder(order.id);
        setNotice(`Order #${order.id} cancelled.`);
        // Replace any earlier "last change" report: the row now
        // ends with ,Cancelled, not the previous transition.
        noteMutation({ kind: 'cancelled', id: order.id });
      } else {
        await deleteOrder(order.id);
        setNotice(`Order #${order.id} deleted.`);
        noteMutation({
          kind: 'deleted',
          id: order.id,
          // Was this order's row actually shown in the preview the
          // user is looking at? Only then can the refreshed
          // preview verify its absence; a row beyond the bounded
          // preview was never visible (see lastChangeMessage).
          wasInPreview:
            preview !== null && preview.available && previewRowFor(preview.rows, order.id) !== null,
        });
        // The inspected order is gone: clear the selection instead
        // of leaving its stale details on screen. changeSelection
        // also invalidates any detail request still in flight for
        // it, so a late response cannot repopulate the inspector.
        if (selectedId === order.id) {
          changeSelection(null);
        }
      }
      setConfirm(null);
      refresh();
      refreshPanelAfterMutation(kind === 'cancel' ? order.id : undefined);
    } catch (e) {
      setError(errorMessage(e, 'That action failed — nothing was changed.'));
      setConfirm(null);
    } finally {
      setActionBusy(false);
    }
  };

  // What the panel may honestly claim about the file preview: it
  // counts as refreshed only after a successful re-read that
  // postdates the last mutation (see loadPanel / noteMutation).
  const previewState: PreviewState = {
    refreshed: !previewStale,
    available: preview?.available ?? false,
    rows: preview?.available ? preview.rows : [],
    previewRows: preview?.preview_rows ?? 10,
  };
  const changedId = changedRowId(lastChange, previewState);

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

      {/* "How CSV becomes orders" — educational, read-only panel.
          The diagram describes the real backend flow (Store::loadOrders
          + Store::saveAtomic). The file preview is the actual runtime
          orders.csv, fetched from GET /api/orders/csv-preview; all CSV
          text below is rendered as plain text, never as HTML. */}
      <div className="rounded-xl border border-slate-200 bg-white shadow-sm dark:border-slate-700 dark:bg-slate-900">
        <button
          onClick={toggleCsvPanel}
          aria-expanded={csvOpen}
          className="flex w-full items-center justify-between gap-3 px-4 py-3 text-left"
        >
          <span className="flex items-center gap-3">
            <FileText className="h-5 w-5 shrink-0 text-emerald-600 dark:text-emerald-400" aria-hidden />
            <span>
              <span className="block font-semibold text-slate-900 dark:text-white">How CSV becomes orders</span>
              <span className="block text-xs text-slate-500 dark:text-slate-400">
                See the real orders.csv file and how each row becomes an Order in the linked list, queue and BST.
              </span>
            </span>
          </span>
          {csvOpen ? <ChevronUp className="h-5 w-5 shrink-0 text-slate-400" aria-hidden /> : <ChevronDown className="h-5 w-5 shrink-0 text-slate-400" aria-hidden />}
        </button>

        {csvOpen && (
          <div className="space-y-5 border-t border-slate-200 px-4 py-4 dark:border-slate-700">
            {/* Flow diagram (matches Store::loadOrders / saveAtomic).
                Two labelled stages; the three structures are parallel
                branches from the parsed orders, not a chain. The
                stage containers keep the grouping clear when the
                boxes wrap on a narrow screen. */}
            <div className="space-y-3">
              <div className="rounded-lg border border-slate-200 p-3 dark:border-slate-700">
                <p className="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500 dark:text-slate-400">
                  Stage 1 · Load — when the server starts (Store::loadOrders)
                </p>
                <FlowChain steps={LOAD_STEPS} />
                <p className="mb-2 mt-3 text-xs text-slate-500 dark:text-slate-400">
                  Each parsed Order is then used in three places at once:
                </p>
                <div className="grid gap-2 sm:grid-cols-3">
                  {LOAD_BRANCHES.map((branch) => (
                    <FlowBox key={branch.title} title={branch.title} text={branch.text} className="w-full" />
                  ))}
                </div>
              </div>
              <div className="rounded-lg border border-slate-200 p-3 dark:border-slate-700">
                <p className="mb-2 text-xs font-semibold uppercase tracking-wide text-slate-500 dark:text-slate-400">
                  Stage 2 · Save — on every successful change
                </p>
                <FlowChain steps={SAVE_STEPS} />
              </div>
              <p className="text-xs text-slate-500 dark:text-slate-400">
                The tree in this flow is the order ID binary search tree (the Order BST). It is not an abstract syntax tree.
              </p>
            </div>

            {/* Live runtime file preview */}
            <div>
              <div className="mb-2 flex flex-wrap items-center justify-between gap-2">
                <h3 className="font-semibold text-slate-900 dark:text-white">The actual runtime file</h3>
                <button onClick={() => void loadPanel()} disabled={previewLoading} className={`inline-flex items-center gap-1 ${buttonSecondary}`}>
                  <RefreshCw className="h-4 w-4" aria-hidden /> Refresh preview
                </button>
              </div>
              {previewError && <ErrorBanner message={previewError} onRetry={() => void loadPanel()} />}
              {previewLoading && !preview ? (
                <Spinner label="Reading orders.csv from the server…" />
              ) : preview && !preview.available ? (
                <div className="rounded-lg border border-amber-200 bg-amber-50 px-4 py-3 text-sm text-amber-800 dark:border-amber-900 dark:bg-amber-950/60 dark:text-amber-200" role="status">
                  <p className="font-medium">File not available: <code className="font-mono">{preview.path}</code></p>
                  <p>{preview.message ?? 'The runtime orders.csv could not be read.'}</p>
                </div>
              ) : preview ? (
                <>
                  <p className="mb-2 text-sm text-slate-500 dark:text-slate-400">
                    Live file on disk: <code className="font-mono text-slate-700 dark:text-slate-200">{preview.path}</code> — read fresh from the
                    server on every refresh. Showing the first {preview.preview_rows} of {preview.total_rows} data rows
                    {preview.truncated ? ' (bounded preview — the rest of the file is not sent)' : ''}. Click a row to inspect that order.
                  </p>
                  <div className="overflow-x-auto rounded-lg border border-slate-200 bg-slate-50 p-2 dark:border-slate-700 dark:bg-slate-800">
                    <div className="min-w-[640px]">
                      <div className="px-2 py-1 font-mono text-xs font-semibold text-slate-700 dark:text-slate-200">{preview.header}</div>
                      {preview.rows.map((row) => {
                        const rowId = parseCsvRowId(row);
                        const isSelected = rowId !== null && rowId === selectedId;
                        const isChanged = changedId !== null && rowId === changedId;
                        return (
                          <button
                            key={row}
                            onClick={() => { if (rowId !== null) changeSelection(rowId); }}
                            aria-label={rowId !== null ? `Inspect order ${rowId}` : 'Inspect this row'}
                            className={`block w-full rounded px-2 py-1 text-left font-mono text-xs hover:bg-slate-200 dark:hover:bg-slate-700 ${
                              isSelected ? 'bg-emerald-100 dark:bg-emerald-900/40' : isChanged ? 'bg-amber-100 dark:bg-amber-900/40' : ''
                            }`}
                          >
                            {row}
                            {isChanged && <span className="ml-2 font-sans font-semibold text-amber-700 dark:text-amber-300">(just changed)</span>}
                          </button>
                        );
                      })}
                    </div>
                  </div>
                  {lastChange && (
                    <p className="mt-2 text-sm text-slate-600 dark:text-slate-300" role="status">
                      {lastChangeMessage(lastChange, previewState)}
                    </p>
                  )}
                </>
              ) : null}
            </div>

            {/* One-order inspector */}
            <div>
              <h3 className="mb-2 font-semibold text-slate-900 dark:text-white">Inspect one order</h3>
              <label className="flex max-w-md flex-col gap-1 text-xs font-medium text-slate-600 dark:text-slate-300">
                Pick an order (or click a row in the file preview above)
                <select
                  value={selectedId ?? ''}
                  onChange={(e) => changeSelection(e.target.value ? Number(e.target.value) : null)}
                  className={inputClass}
                  aria-label="Pick an order to inspect"
                >
                  <option value="">Select an order…</option>
                  {panelOrders.map((o) => (
                    <option key={o.id} value={o.id}>#{o.id} — {o.customer_name} ({o.status})</option>
                  ))}
                </select>
              </label>

              {selLoading && <Spinner label="Loading order details…" />}
              {selError && <ErrorBanner message={selError} />}
              {selDetail && !selLoading && (
                <div className="mt-4 space-y-4">
                  <div>
                    <h4 className="mb-1 text-sm font-semibold text-slate-900 dark:text-white">Raw row in the file</h4>
                    {preview && preview.available && previewRowFor(preview.rows, selDetail.order.id) ? (
                      <p className="overflow-x-auto rounded-lg border border-slate-200 bg-slate-50 px-3 py-2 font-mono text-xs dark:border-slate-700 dark:bg-slate-800">
                        {previewRowFor(preview.rows, selDetail.order.id)}
                      </p>
                    ) : (
                      <p className="text-sm text-slate-500 dark:text-slate-400">
                        This order&apos;s row is beyond the first {preview?.preview_rows ?? 10} rows shown in the preview.
                        The parsed fields below come from GET /api/orders/{selDetail.order.id} instead.
                      </p>
                    )}
                    <p className="mt-1 text-xs text-slate-500 dark:text-slate-400">
                      On load, parseOrder() splits this line on commas and converts order_id, price_pkr and quantity to numbers;
                      the other fields stay as text.
                    </p>
                  </div>

                  <div>
                    <h4 className="mb-2 text-sm font-semibold text-slate-900 dark:text-white">
                      Parsed fields (live state, from GET /api/orders/{selDetail.order.id} — a BST search on the backend)
                    </h4>
                    <dl className="grid grid-cols-2 gap-3 text-sm sm:grid-cols-3">
                      <div><dt className="text-slate-500 dark:text-slate-400">Order ID</dt><dd className="font-medium">{selDetail.order.id}</dd></div>
                      <div><dt className="text-slate-500 dark:text-slate-400">Customer</dt><dd className="font-medium">{selDetail.order.customer_name}</dd></div>
                      <div><dt className="text-slate-500 dark:text-slate-400">Product</dt><dd className="font-medium">{selDetail.order.product}</dd></div>
                      <div><dt className="text-slate-500 dark:text-slate-400">Category</dt><dd className="font-medium">{selDetail.order.category}</dd></div>
                      <div><dt className="text-slate-500 dark:text-slate-400">Price × qty</dt><dd className="font-medium">{formatPkr(selDetail.order.price_pkr)} × {selDetail.order.quantity}</dd></div>
                      <div><dt className="text-slate-500 dark:text-slate-400">City</dt><dd className="font-medium">{selDetail.order.city}</dd></div>
                      <div><dt className="text-slate-500 dark:text-slate-400">Date</dt><dd className="font-medium">{selDetail.order.order_date}</dd></div>
                      <div><dt className="text-slate-500 dark:text-slate-400">Status</dt><dd><StatusBadge status={selDetail.order.status} /></dd></div>
                    </dl>
                  </div>

                  <div className="grid gap-3 sm:grid-cols-3">
                    <div className="rounded-lg border border-slate-200 p-3 dark:border-slate-700">
                      <h4 className="text-sm font-semibold text-slate-900 dark:text-white">Linked List</h4>
                      <p className="mt-1 text-xs text-slate-500 dark:text-slate-400">
                        {listPosition(panelOrders, selDetail.order.id) > 0
                          ? <>Position {listPosition(panelOrders, selDetail.order.id)} of {panelOrders.length}. The list keeps orders in file order — GET /api/orders returns them in this order, and every save writes them back in this order.</>
                          : <>Not in the list the API just returned — it may have been deleted. Refresh the preview to re-check.</>}
                      </p>
                    </div>
                    <div className="rounded-lg border border-slate-200 p-3 dark:border-slate-700">
                      <h4 className="text-sm font-semibold text-slate-900 dark:text-white">Processing Queue</h4>
                      <p className="mt-1 text-xs text-slate-500 dark:text-slate-400">{queueNote(selDetail.order, panelQueue)}</p>
                    </div>
                    <div className="rounded-lg border border-slate-200 p-3 dark:border-slate-700">
                      <h4 className="text-sm font-semibold text-slate-900 dark:text-white">Order BST</h4>
                      <p className="mt-1 text-xs text-slate-500 dark:text-slate-400">
                        {panelTree
                          ? <>{treeContains(panelTree.root, selDetail.order.id)
                              ? <>Found in the Order BST (size {panelTree.size}, height {panelTree.height}) — the ID index behind Search by ID. </>
                              : <>Not found in the Order BST the API just returned (size {panelTree.size}, height {panelTree.height}). </>}
                            Conceptual illustration: a search starts at the root and goes left or right by comparing IDs, so an order is
                            usually found in a few steps — the DSA Performance page measures the real step counts.</>
                          : <>Tree data not loaded yet.</>}
                      </p>
                    </div>
                  </div>

                  <p className="text-xs text-slate-500 dark:text-slate-400">
                    To see the file change: use the &ldquo;Move to…&rdquo; control in the table below to change this order&apos;s status.
                    The backend validates the change, updates the list and BST together, fixes the queue, rewrites orders.csv
                    (temp file, then atomic replace) and appends one activity-log line. Then refresh the preview above.
                  </p>
                </div>
              )}
            </div>
          </div>
        )}
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
