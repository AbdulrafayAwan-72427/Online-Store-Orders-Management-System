import { useCallback, useEffect, useState } from 'react';
import { ScrollText, Search } from 'lucide-react';
import {
  getLog, getLogs, getLogTree, getStats,
  type LogEntry, type LogTree, type LogTreeNode, type Stats,
} from '../lib/api';
import { EmptyState, ErrorBanner, Spinner, buttonPrimary, buttonSecondary, errorMessage, inputClass } from '../components/ui';
import { TreeFlow, type FlowTreeNode } from '../components/TreeFlow';

const ACTION_TYPES = [
  'ORDER_CREATED', 'ORDER_CANCELLED', 'STATUS_UPDATED', 'ORDER_PROCESSED',
  'ORDER_DELETED', 'UNDO', 'ROUTE_GENERATED', 'DELIVERY_SELECTION',
  'DELIVERY_ASSIGNMENT', 'DELIVERY_COMPLETED', 'TRIP_CANCELLED',
] as const;

function logNodeToFlow(n: LogTreeNode | null): FlowTreeNode | null {
  if (!n) return null;
  return {
    key: `log-${n.log_id}`,
    label: `#${n.log_id} · ${n.action_type}`,
    detail: `${n.timestamp.replace('T', ' ')} · order ${n.order_id}`,
    left: logNodeToFlow(n.left),
    right: logNodeToFlow(n.right),
  };
}

function LogRow({ log }: { log: LogEntry }) {
  return (
    <li className="flex items-start gap-3 border-b border-slate-100 pb-3 last:border-0 last:pb-0 dark:border-slate-800">
      <span className="mt-0.5 shrink-0 rounded-md bg-slate-100 px-2 py-0.5 text-xs font-semibold text-slate-700 dark:bg-slate-800 dark:text-slate-200">
        {log.action_type}
      </span>
      <div className="min-w-0 flex-1">
        <p className="text-sm text-slate-800 dark:text-slate-100">{log.description}</p>
        <p className="mt-0.5 text-xs text-slate-500 dark:text-slate-400">
          {log.timestamp.replace('T', ' ')} · actor: {log.actor} · order ID: {log.order_id} ·{' '}
          {log.old_status || log.new_status ? `${log.old_status || '—'} → ${log.new_status || '—'}` : 'no status change'}
        </p>
      </div>
      <span className="shrink-0 text-xs text-slate-400">#{log.log_id}</span>
    </li>
  );
}

export default function ActivityLogs() {
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [stats, setStats] = useState<Stats | null>(null);
  const [tree, setTree] = useState<LogTree | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const [actionType, setActionType] = useState('');
  const [orderId, setOrderId] = useState('');
  const [from, setFrom] = useState('');
  const [to, setTo] = useState('');
  const [filtersApplied, setFiltersApplied] = useState(false);
  const [newestFirst, setNewestFirst] = useState(false);

  const [searchId, setSearchId] = useState('');
  const [searchedLog, setSearchedLog] = useState<LogEntry | null>(null);
  const [searchError, setSearchError] = useState<string | null>(null);
  const [searchBusy, setSearchBusy] = useState(false);

  // Filters are applied via the API query params (server-side), not by
  // filtering a big list on the client.
  const load = useCallback(async (applied: { action_type?: string; order_id?: number; from?: string; to?: string }) => {
    setLoading(true);
    setError(null);
    try {
      const [l, s, t] = await Promise.all([getLogs(applied), getStats(), getLogTree()]);
      setLogs(l);
      setStats(s);
      setTree(t);
    } catch (e) {
      setError(errorMessage(e, 'Could not load activity logs.'));
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    void load({});
  }, [load]);

  const applyFilters = () => {
    const orderIdNum = orderId.trim() === '' ? undefined : Number(orderId.trim());
    if (orderId.trim() !== '' && (!Number.isInteger(orderIdNum) || (orderIdNum as number) <= 0)) {
      setError('Order ID must be a positive number (or leave it blank).');
      return;
    }
    setFiltersApplied(true);
    setSearchedLog(null);
    setSearchError(null);
    void load({
      action_type: actionType || undefined,
      order_id: orderIdNum,
      from: from || undefined,
      to: to || undefined,
    });
  };

  const clearFilters = () => {
    setActionType('');
    setOrderId('');
    setFrom('');
    setTo('');
    setFiltersApplied(false);
    setSearchedLog(null);
    setSearchError(null);
    void load({});
  };

  const runSearchById = async () => {
    const id = Number(searchId.trim());
    if (!searchId.trim() || !Number.isInteger(id) || id <= 0) {
      setSearchError('Enter a valid log ID (a positive number).');
      return;
    }
    setSearchError(null);
    setSearchedLog(null);
    setSearchBusy(true);
    try {
      setSearchedLog(await getLog(id));
    } catch (e) {
      // API 404 shown as the error (e.g. "Log entry 999 not found.").
      setSearchError(errorMessage(e, 'Log entry not found.'));
    } finally {
      setSearchBusy(false);
    }
  };

  const clearSearch = () => {
    setSearchedLog(null);
    setSearchError(null);
    setSearchId('');
  };

  // Newest-first is presentation-only reordering of the list the API
  // returned (inorder = chronological, oldest first).
  const shownLogs = newestFirst ? [...logs].reverse() : logs;

  return (
    <div className="space-y-4">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <h2 className="flex items-center gap-2 text-2xl font-bold text-slate-900 dark:text-white">
            <ScrollText className="h-6 w-6 text-emerald-600" aria-hidden /> Activity Logs
          </h2>
          <p className="text-sm text-slate-500 dark:text-slate-400">
            entries in this list: {logs.length}
            {stats !== null && <> · log entries (from /api/stats): {stats.log_entries}</>}
          </p>
        </div>
        <button
          onClick={() => setNewestFirst((v) => !v)}
          className={buttonSecondary}
          aria-pressed={newestFirst}
        >
          {newestFirst ? 'Newest first ✓' : 'Oldest first ✓'} — switch order
        </button>
      </div>
      <p className="text-xs text-slate-500 dark:text-slate-400">
        The timeline is oldest-first by default (the API returns inorder = chronological). The toggle above only
        reorders the display — it does not re-query the backend.
      </p>

      {error && <ErrorBanner message={error} onRetry={() => void load({})} />}

      {/* Search by log ID */}
      <section className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900" aria-label="Search by log ID">
        <h3 className="font-semibold text-slate-900 dark:text-white">Search by log ID</h3>
        <div className="mt-3 flex flex-wrap items-end gap-2">
          <label className="flex flex-col gap-1 text-xs font-medium text-slate-600 dark:text-slate-300">
            Log ID
            <span className="flex gap-2">
              <input
                value={searchId}
                onChange={(e) => setSearchId(e.target.value)}
                onKeyDown={(e) => { if (e.key === 'Enter') void runSearchById(); }}
                placeholder="e.g. 12"
                inputMode="numeric"
                className={inputClass}
                aria-label="Search log by ID"
              />
              <button onClick={() => void runSearchById()} disabled={searchBusy} className={buttonPrimary} aria-label="Search log">
                <Search className="h-4 w-4" aria-hidden />
              </button>
            </span>
          </label>
          {searchedLog && (
            <button onClick={clearSearch} className={buttonSecondary}>Clear search</button>
          )}
        </div>
        {searchError && <div className="mt-3"><ErrorBanner message={searchError} /></div>}
        {searchedLog && (
          <div className="mt-3 rounded-lg border border-emerald-200 bg-emerald-50 p-3 dark:border-emerald-900 dark:bg-emerald-950/40">
            <p className="text-sm font-semibold text-slate-900 dark:text-white">Log entry #{searchedLog.log_id} (from GET /api/logs/:id)</p>
            <ul className="mt-2"><LogRow log={searchedLog} /></ul>
          </div>
        )}
      </section>

      {/* Filters (server-side via GET /api/logs query params) */}
      <section className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900" aria-label="Filter logs">
        <h3 className="font-semibold text-slate-900 dark:text-white">Filters (applied by the API, not in the browser)</h3>
        <div className="mt-3 flex flex-wrap items-end gap-3">
          <label className="flex flex-col gap-1 text-xs font-medium text-slate-600 dark:text-slate-300">
            Action type
            <select value={actionType} onChange={(e) => setActionType(e.target.value)} className={inputClass} aria-label="Filter by action type">
              <option value="">All action types</option>
              {ACTION_TYPES.map((a) => <option key={a} value={a}>{a}</option>)}
            </select>
          </label>
          <label className="flex flex-col gap-1 text-xs font-medium text-slate-600 dark:text-slate-300">
            Order ID
            <input value={orderId} onChange={(e) => setOrderId(e.target.value)} placeholder="e.g. 1006" inputMode="numeric" className={inputClass} aria-label="Filter by order ID" />
          </label>
          <label className="flex flex-col gap-1 text-xs font-medium text-slate-600 dark:text-slate-300">
            From
            <input type="date" value={from} onChange={(e) => setFrom(e.target.value)} className={inputClass} aria-label="Filter from date" />
          </label>
          <label className="flex flex-col gap-1 text-xs font-medium text-slate-600 dark:text-slate-300">
            To
            <input type="date" value={to} onChange={(e) => setTo(e.target.value)} className={inputClass} aria-label="Filter to date" />
          </label>
          <button onClick={applyFilters} className={buttonPrimary}>Apply filters</button>
          {filtersApplied && <button onClick={clearFilters} className={buttonSecondary}>Clear filters</button>}
        </div>
        <p className="mt-2 text-xs text-slate-500 dark:text-slate-400">
          Compared lexicographically against timestamps, as the API does — a date alone matches that day&apos;s prefix.
        </p>
      </section>

      {/* Timeline */}
      <section className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900" aria-label="Log timeline">
        <h3 className="font-semibold text-slate-900 dark:text-white">
          Timeline ({newestFirst ? 'newest first — display order only' : 'oldest first'})
        </h3>
        {loading ? (
          <Spinner label="Loading logs…" />
        ) : stats !== null && stats.log_entries === 0 ? (
          <EmptyState title="No log entries yet" hint="Actions on orders are recorded here by the backend. Process, cancel or change an order and the log will appear." />
        ) : shownLogs.length === 0 ? (
          <EmptyState title="No log entries match these filters" hint="Try clearing a filter." />
        ) : (
          <ul className="mt-3 space-y-3">
            {shownLogs.map((l) => <LogRow key={l.log_id} log={l} />)}
          </ul>
        )}
      </section>

      {/* BST visualisation */}
      <section className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900" aria-label="Activity log BST">
        <h3 className="font-semibold text-slate-900 dark:text-white">Activity Log BST (live, from GET /api/logs/tree)</h3>
        <p className="mt-1 text-sm text-slate-500 dark:text-slate-400">
          {tree ? <>Size: <strong>{tree.size}</strong> entries · height: <strong>{tree.height}</strong> (real recursive walk; 0 = empty, 1 = single node).</> : 'Loading tree…'}
        </p>
        {tree && tree.size > 0 && tree.height === tree.size && (
          <p className="mt-1 text-sm text-amber-700 dark:text-amber-300">
            Height {tree.height} equals size {tree.size}: the tree is right-leaning, not balanced — timestamps arrive in
            increasing order and this BST is not self-balancing, so each new entry lands to the right of the last.
          </p>
        )}
        <div className="mt-3">
          {tree ? (
            <TreeFlow root={logNodeToFlow(tree.root)} totalSize={tree.size} emptyLabel="No log entries yet" emptyHint="The tree appears once the backend has logged an action." />
          ) : (
            <Spinner label="Loading log tree…" />
          )}
        </div>
        <p className="mt-3 text-sm text-slate-600 dark:text-slate-300">
          Ordered by (timestamp, logId) — inorder traversal IS the timeline. Searching by log ID alone is O(n): the tree
          is ordered by time, not by ID, so there is no shortcut to a log ID.
        </p>
      </section>
    </div>
  );
}
