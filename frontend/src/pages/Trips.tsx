import { useCallback, useEffect, useMemo, useState } from 'react';
import { Truck } from 'lucide-react';
import { cancelTrip, completeTrip, getOrders, getTrips, type Order, type Trip, type TripStatus } from '../lib/api';
import { ConfirmDialog, EmptyState, ErrorBanner, Spinner, buttonSecondary, errorMessage, inputClass } from '../components/ui';

const TRIP_STYLES: Record<TripStatus, string> = {
  Planned: 'bg-blue-100 text-blue-800 dark:bg-blue-900/50 dark:text-blue-200',
  Completed: 'bg-emerald-100 text-emerald-800 dark:bg-emerald-900/50 dark:text-emerald-200',
  Cancelled: 'bg-red-100 text-red-800 dark:bg-red-900/50 dark:text-red-200',
};

function TripStatusBadge({ status }: { status: TripStatus }) {
  return <span className={`inline-flex rounded-full px-2.5 py-0.5 text-xs font-semibold ${TRIP_STYLES[status]}`}>{status}</span>;
}

type Filter = 'All' | TripStatus;

export default function Trips() {
  const [trips, setTrips] = useState<Trip[]>([]);
  const [orders, setOrders] = useState<Order[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [filter, setFilter] = useState<Filter>('All');
  const [confirm, setConfirm] = useState<{ kind: 'complete' | 'cancel'; trip: Trip } | null>(null);
  const [actionBusy, setActionBusy] = useState(false);

  const load = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const [t, o] = await Promise.all([getTrips(), getOrders()]);
      setTrips(t);
      setOrders(o);
    } catch (e) {
      setError(errorMessage(e, 'Could not load trips.'));
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => { void load(); }, [load]);

  const orderById = useMemo(() => {
    const m = new Map<number, Order>();
    for (const o of orders) m.set(o.id, o);
    return m;
  }, [orders]);

  const filtered = filter === 'All' ? trips : trips.filter((t) => t.status === filter);
  const counts = useMemo(() => {
    const c: Record<Filter, number> = { All: trips.length, Planned: 0, Completed: 0, Cancelled: 0 };
    for (const t of trips) c[t.status] += 1;
    return c;
  }, [trips]);

  const runConfirm = async () => {
    if (!confirm) return;
    setActionBusy(true);
    setError(null);
    try {
      const updated = confirm.kind === 'complete'
        ? await completeTrip(confirm.trip.trip_id)
        : await cancelTrip(confirm.trip.trip_id);
      setNotice(
        confirm.kind === 'complete'
          ? `Trip #${updated.trip_id} completed — its orders are now Delivered.`
          : `Trip #${updated.trip_id} cancelled — its orders are back to Processing.`,
      );
      setConfirm(null);
      // Re-fetch trips + orders after every mutation. No optimistic update.
      // (No getStats() here: the Dashboard is what displays stats, and this
      // page discarded the result anyway.)
      await load();
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
          <h2 className="flex items-center gap-2 text-2xl font-bold text-slate-900 dark:text-white">
            <Truck className="h-6 w-6 text-emerald-600" aria-hidden /> Delivery Trips
          </h2>
          <p className="text-sm text-slate-500 dark:text-slate-400">
            {trips.length} trip{trips.length === 1 ? '' : 's'} — {counts.Planned} planned, {counts.Completed} completed, {counts.Cancelled} cancelled.
          </p>
        </div>
        <label className="flex items-center gap-2 text-xs font-medium text-slate-600 dark:text-slate-300">
          Status
          <select value={filter} onChange={(e) => setFilter(e.target.value as Filter)} className={inputClass} aria-label="Filter trips by status">
            {(['All', 'Planned', 'Completed', 'Cancelled'] as Filter[]).map((f) => (
              <option key={f} value={f}>{f} ({counts[f]})</option>
            ))}
          </select>
        </label>
      </div>

      <div className="rounded-lg border border-slate-200 bg-white px-4 py-3 text-sm text-slate-600 dark:border-slate-700 dark:bg-slate-900 dark:text-slate-300">
        <strong>Rules:</strong> only Processing orders can be assigned to a trip (a Pending order must be processed first).
        Completion is explicit — a trip is only marked Completed when you confirm it here.
        Cancelling a Planned trip releases its orders back to Processing, so they can be assigned again.
      </div>

      {notice && (
        <div className="rounded-lg border border-emerald-200 bg-emerald-50 px-4 py-3 text-sm text-emerald-800 dark:border-emerald-900 dark:bg-emerald-950/60 dark:text-emerald-200" role="status">
          {notice}
          <button onClick={() => setNotice(null)} className="float-right font-semibold" aria-label="Dismiss">×</button>
        </div>
      )}
      {error && <ErrorBanner message={error} onRetry={() => void load()} />}

      {loading ? (
        <Spinner label="Loading trips…" />
      ) : filtered.length === 0 ? (
        <div className="rounded-xl border border-slate-200 bg-white dark:border-slate-700 dark:bg-slate-900">
          <EmptyState
            title={trips.length === 0 ? 'No trips yet' : `No ${filter.toLowerCase()} trips`}
            hint={trips.length === 0 ? 'Create one from the Delivery Planner by selecting eligible Processing orders.' : 'Try a different status filter.'}
          />
        </div>
      ) : (
        <div className="grid gap-4 md:grid-cols-2 xl:grid-cols-3">
          {filtered.map((t) => (
            <article key={t.trip_id} className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900">
              <div className="flex items-center justify-between gap-2">
                <h3 className="font-bold text-slate-900 dark:text-white">Trip #{t.trip_id}</h3>
                <TripStatusBadge status={t.status} />
              </div>
              <p className="mt-2 text-sm">
                <strong>{t.source} → {t.destination}</strong>
              </p>
              <p className="mt-1 break-words text-sm text-slate-600 dark:text-slate-300">
                Route: {t.route.join(' → ')}
              </p>
              <p className="mt-1 text-sm text-slate-600 dark:text-slate-300">
                {t.time_min} min · {t.distance_km} km
              </p>
              <div className="mt-3">
                <p className="text-xs font-semibold text-slate-500 dark:text-slate-400">Assigned orders ({t.assigned_order_ids.length})</p>
                {t.assigned_order_ids.length === 0 ? (
                  <p className="text-sm text-slate-500 dark:text-slate-400">None.</p>
                ) : (
                  <ul className="mt-1 space-y-1 text-sm">
                    {t.assigned_order_ids.map((id) => {
                      const o = orderById.get(id);
                      return (
                        <li key={id} className="rounded-md bg-slate-100 px-2 py-1 dark:bg-slate-800">
                          #{id}
                          {o ? (
                            <> — {o.customer_name} · {o.city} · <span className="font-medium">{o.status}</span></>
                          ) : (
                            <> — order no longer in the orders list</>
                          )}
                        </li>
                      );
                    })}
                  </ul>
                )}
              </div>
              {t.status === 'Planned' && (
                <div className="mt-4 flex gap-2">
                  <button
                    onClick={() => setConfirm({ kind: 'complete', trip: t })}
                    className="rounded-md bg-emerald-600 px-3 py-1.5 text-sm font-medium text-white hover:bg-emerald-700"
                  >
                    Complete
                  </button>
                  <button onClick={() => setConfirm({ kind: 'cancel', trip: t })} className={buttonSecondary}>
                    Cancel
                  </button>
                </div>
              )}
            </article>
          ))}
        </div>
      )}

      {confirm && (
        <ConfirmDialog
          title={confirm.kind === 'complete' ? `Complete trip #${confirm.trip.trip_id}?` : `Cancel trip #${confirm.trip.trip_id}?`}
          message={
            confirm.kind === 'complete'
              ? `Trip #${confirm.trip.trip_id} (${confirm.trip.source} → ${confirm.trip.destination}) will be marked Completed and its ${confirm.trip.assigned_order_ids.length} order(s) will become Delivered.`
              : `Trip #${confirm.trip.trip_id} (${confirm.trip.source} → ${confirm.trip.destination}) will be cancelled and its ${confirm.trip.assigned_order_ids.length} order(s) will go back to Processing.`
          }
          confirmLabel={confirm.kind === 'complete' ? 'Complete trip' : 'Cancel trip'}
          onConfirm={() => void runConfirm()}
          onCancel={() => setConfirm(null)}
          busy={actionBusy}
        />
      )}
    </div>
  );
}
