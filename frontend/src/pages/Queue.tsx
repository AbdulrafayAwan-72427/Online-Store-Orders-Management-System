import { useCallback, useEffect, useState } from 'react';
import { ListOrdered, RotateCcw } from 'lucide-react';
import { formatPkr, getOrders, getQueue, getStats, processNext, undo, type Order, type Stats } from '../lib/api';
import { EmptyState, ErrorBanner, Spinner, StatusBadge, buttonPrimary, buttonSecondary, errorMessage } from '../components/ui';

export default function Queue() {
  const [queue, setQueue] = useState<Order[]>([]);
  const [stats, setStats] = useState<Stats | null>(null);
  const [orderCount, setOrderCount] = useState<number | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const load = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const [q, s, o] = await Promise.all([getQueue(), getStats(), getOrders()]);
      setQueue(q);
      setStats(s);
      setOrderCount(o.length);
    } catch (e) {
      setError(errorMessage(e, 'Could not load the queue.'));
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  // After every successful action, re-fetch queue + orders + stats.
  const refreshAfterAction = async () => {
    try {
      const [q, s, o] = await Promise.all([getQueue(), getStats(), getOrders()]);
      setQueue(q);
      setStats(s);
      setOrderCount(o.length);
    } catch (e) {
      setError(errorMessage(e, 'Action succeeded, but re-loading the queue failed.'));
    }
  };

  const runProcessNext = async () => {
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      const processed = await processNext();
      setNotice(`Order #${processed.id} processed — it is now ${processed.status}.`);
      await refreshAfterAction();
    } catch (e) {
      // Backend 409 message shown verbatim (e.g. "Queue is empty.").
      setError(errorMessage(e, 'Could not process the next order.'));
    } finally {
      setBusy(false);
    }
  };

  const runUndo = async () => {
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      await undo();
      setNotice('Undone — the last action was reversed (status and queue position restored).');
      await refreshAfterAction();
    } catch (e) {
      // Backend 409 message shown verbatim: "Nothing to undo."
      setError(errorMessage(e, 'Could not undo.'));
    } finally {
      setBusy(false);
    }
  };

  const front = queue[0];

  return (
    <div className="space-y-4">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <h2 className="flex items-center gap-2 text-2xl font-bold text-slate-900 dark:text-white">
            <ListOrdered className="h-6 w-6 text-emerald-600" aria-hidden /> Processing Queue
          </h2>
          <p className="text-sm text-slate-500 dark:text-slate-400">
            Queue size: <strong>{queue.length}</strong> Pending order{queue.length === 1 ? '' : 's'} (front → rear, from GET /api/queue)
            {stats !== null && orderCount !== null && (
              <> · {orderCount} orders total · {stats.pending} Pending · {stats.processing} Processing (from /api/stats)</>
            )}
          </p>
        </div>
        <div className="flex gap-2">
          <button onClick={() => void runProcessNext()} disabled={busy || queue.length === 0} className={buttonPrimary}>
            {busy ? 'Working…' : 'Process next order'}
          </button>
          <button onClick={() => void runUndo()} disabled={busy} className={`${buttonSecondary} inline-flex items-center gap-1`}>
            <RotateCcw className="h-4 w-4" aria-hidden /> Undo last action
          </button>
        </div>
      </div>

      {notice && (
        <div className="rounded-lg border border-emerald-200 bg-emerald-50 px-4 py-3 text-sm text-emerald-800 dark:border-emerald-900 dark:bg-emerald-950/60 dark:text-emerald-200" role="status">
          {notice}
          <button onClick={() => setNotice(null)} className="float-right font-semibold" aria-label="Dismiss">×</button>
        </div>
      )}
      {error && <ErrorBanner message={error} onRetry={() => void load()} />}

      {/* How the queue works — explanation, not data */}
      <div className="rounded-lg border border-slate-200 bg-white px-4 py-3 text-sm text-slate-600 dark:border-slate-700 dark:bg-slate-900 dark:text-slate-300">
        <strong>How this queue works (explanation, not live data):</strong> new Pending orders join at the rear;
        processing always takes from the front (FIFO). Undo restores the exact original position (Phase 3b rule) —
        an undone cancel puts the order back where it was, not just at the front or rear.
      </div>

      {loading ? (
        <Spinner label="Loading queue…" />
      ) : queue.length === 0 ? (
        <div className="rounded-xl border border-slate-200 bg-white dark:border-slate-700 dark:bg-slate-900">
          <EmptyState title="The queue is empty" hint="No Pending orders right now — add an order and it will join the rear of the queue." />
        </div>
      ) : (
        <div className="overflow-x-auto rounded-xl border border-slate-200 bg-white shadow-sm dark:border-slate-700 dark:bg-slate-900">
          <table className="w-full min-w-[640px] text-sm">
            <thead>
              <tr className="border-b border-slate-200 text-left text-slate-500 dark:border-slate-700 dark:text-slate-400">
                <th className="px-4 py-3">Position</th>
                <th className="px-4 py-3">Order ID</th>
                <th className="px-4 py-3">Customer</th>
                <th className="px-4 py-3">City</th>
                <th className="px-4 py-3">Amount</th>
                <th className="px-4 py-3">Status</th>
              </tr>
            </thead>
            <tbody>
              {queue.map((o, i) => {
                const isFront = i === 0;
                return (
                  <tr
                    key={o.id}
                    className={`border-b border-slate-100 last:border-0 dark:border-slate-800 ${
                      isFront ? 'bg-emerald-50 dark:bg-emerald-950/40' : ''
                    }`}
                  >
                    <td className="px-4 py-3 font-medium">
                      {i + 1}
                      {isFront && (
                        <span className="ml-2 inline-flex rounded-full bg-emerald-600 px-2 py-0.5 text-xs font-bold text-white">
                          NEXT
                        </span>
                      )}
                    </td>
                    <td className="px-4 py-3 font-medium">#{o.id}</td>
                    <td className="px-4 py-3">{o.customer_name}</td>
                    <td className="px-4 py-3">{o.city}</td>
                    <td className="px-4 py-3">{formatPkr(o.price_pkr * o.quantity)}</td>
                    <td className="px-4 py-3"><StatusBadge status={o.status} /></td>
                  </tr>
                );
              })}
            </tbody>
          </table>
          {front && (
            <p className="border-t border-slate-200 px-4 py-3 text-sm text-slate-600 dark:border-slate-700 dark:text-slate-300">
              <strong>Next to process:</strong> order #{front.id} — {front.customer_name} ({front.city}).
              Pressing “Process next order” moves it to Processing.
            </p>
          )}
        </div>
      )}
    </div>
  );
}
