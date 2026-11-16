import { useCallback, useEffect, useMemo, useState } from 'react';
import { Link } from 'react-router-dom';
import { Background, Controls, ReactFlow, type Edge, type Node } from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import { Map as MapIcon, Route as RouteIcon, Split, Truck } from 'lucide-react';
import {
  createTrip, getDetour, getGraph, getPlan, postMultiStop,
  type Dataset, type DetourResult, type GraphData, type MultiStopResult, type Order, type PlanResult, type Trip,
} from '../lib/api';
import { EmptyState, ErrorBanner, Spinner, StatusBadge, buttonPrimary, buttonSecondary, errorMessage, inputClass } from '../components/ui';

// Static presentation layout only — cities, edges and weights all come from
// GET /api/delivery/graph. A city missing from this map gets a default
// position instead of crashing the page.
const NODE_POSITIONS: Record<string, { x: number; y: number }> = {
  Islamabad: { x: 480, y: 30 },
  Rawalpindi: { x: 390, y: 110 },
  Lahore: { x: 540, y: 250 },
  Faisalabad: { x: 330, y: 270 },
  Multan: { x: 300, y: 430 },
  Karachi: { x: 110, y: 500 },
};
const DEFAULT_POSITION = { x: 350, y: 250 };

const SOURCE = 'Lahore'; // Fixed warehouse (Phase 3a decision).

function OrderCheckRow({ order, checked, onToggle }: { order: Order; checked: boolean; onToggle: (id: number, on: boolean) => void }) {
  return (
    <label className="flex cursor-pointer items-center gap-2 rounded-lg border border-slate-200 px-3 py-2 text-sm hover:bg-slate-50 dark:border-slate-700 dark:hover:bg-slate-800">
      <input
        type="checkbox"
        checked={checked}
        onChange={(e) => onToggle(order.id, e.target.checked)}
        aria-label={`Select order ${order.id}`}
        className="h-4 w-4 accent-emerald-600"
      />
      <span className="font-medium">#{order.id}</span>
      <span className="min-w-0 flex-1 truncate">{order.customer_name} — {order.city}</span>
      <StatusBadge status={order.status} />
    </label>
  );
}

export default function DeliveryPlanner() {
  const [dataset, setDataset] = useState<Dataset>('production');
  const [graph, setGraph] = useState<GraphData | null>(null);
  const [graphLoading, setGraphLoading] = useState(true);
  const [graphError, setGraphError] = useState<string | null>(null);

  const [destination, setDestination] = useState('Karachi');
  const [plan, setPlan] = useState<PlanResult | null>(null);
  const [planLoading, setPlanLoading] = useState(false);
  const [planError, setPlanError] = useState<string | null>(null);

  const [detourCity, setDetourCity] = useState('');
  const [detour, setDetour] = useState<DetourResult | null>(null);
  const [detourLoading, setDetourLoading] = useState(false);
  const [detourError, setDetourError] = useState<string | null>(null);

  const [multiStops, setMultiStops] = useState<string[]>([]);
  const [multi, setMulti] = useState<MultiStopResult | null>(null);
  const [multiLoading, setMultiLoading] = useState(false);
  const [multiError, setMultiError] = useState<string | null>(null);

  const [selected, setSelected] = useState<Set<number>>(new Set());
  const [ordersById, setOrdersById] = useState<Map<number, Order>>(new Map());
  const [tripError, setTripError] = useState<string | null>(null);
  const [tripBusy, setTripBusy] = useState(false);
  const [createdTrip, setCreatedTrip] = useState<Trip | null>(null);

  const loadGraph = useCallback(async (ds: Dataset) => {
    setGraphLoading(true);
    setGraphError(null);
    try {
      setGraph(await getGraph(ds));
    } catch (e) {
      setGraph(null);
      setGraphError(errorMessage(e, 'Could not load the delivery graph.'));
    } finally {
      setGraphLoading(false);
    }
  }, []);

  useEffect(() => { void loadGraph(dataset); }, [dataset, loadGraph]);

  const switchDataset = (ds: Dataset) => {
    if (ds === dataset) return;
    setDataset(ds);
    // A different network invalidates every calculation made on the old one.
    setPlan(null);
    setPlanError(null);
    setDetour(null);
    setDetourError(null);
    setDetourCity('');
    setMulti(null);
    setMultiError(null);
    setMultiStops([]);
    setSelected(new Set());
    setCreatedTrip(null);
    setTripError(null);
  };

  const cities = graph?.cities ?? [];
  const destinationOptions = cities.filter((c) => c !== SOURCE);

  // The path to highlight: multi-stop path wins, then detour via-route is
  // shown in the detour panel (its path is not returned by the API, so the
  // shortest path stays highlighted), else the shortest plan path.
  const highlightPath = multi?.path ?? plan?.path ?? [];
  const highlightEdges = useMemo(() => {
    const s = new Set<string>();
    for (let i = 0; i + 1 < highlightPath.length; i++) {
      s.add(`${highlightPath[i]}|${highlightPath[i + 1]}`);
      s.add(`${highlightPath[i + 1]}|${highlightPath[i]}`);
    }
    return s;
  }, [highlightPath]);
  const highlightNodes = useMemo(() => new Set(highlightPath), [highlightPath]);

  const nodes: Node[] = useMemo(
    () =>
      cities.map((city) => {
        const onPath = highlightNodes.has(city);
        const isSource = city === SOURCE;
        const isDest = city === destination;
        return {
          id: city,
          position: NODE_POSITIONS[city] ?? DEFAULT_POSITION,
          data: {
            label: `${city}${isSource ? ' (warehouse)' : ''}`,
          },
          style: {
            background: onPath ? '#059669' : isDest ? '#dbeafe' : '#ffffff',
            color: onPath ? '#ffffff' : '#0f172a',
            border: isDest ? '2px solid #2563eb' : '1px solid #94a3b8',
            borderRadius: 10,
            padding: 10,
            fontWeight: 600,
            fontSize: 13,
          },
        };
      }),
    [cities, highlightNodes, destination],
  );

  const edges: Edge[] = useMemo(
    () =>
      (graph?.edges ?? []).map((e, i) => {
        const onPath = highlightEdges.has(`${e.from}|${e.to}`);
        return {
          id: `e${i}`,
          source: e.from,
          target: e.to,
          label: `${e.time_min} min · ${e.distance_km} km`,
          animated: onPath,
          style: { stroke: onPath ? '#059669' : '#94a3b8', strokeWidth: onPath ? 3 : 1.5 },
          labelStyle: { fontSize: 11, fill: '#475569' },
          labelBgStyle: { fill: '#ffffff', fillOpacity: 0.85 },
        };
      }),
    [graph, highlightEdges],
  );

  const runPlan = async () => {
    if (!destination) return;
    setPlanLoading(true);
    setPlanError(null);
    setDetour(null);
    setDetourError(null);
    setMulti(null);
    setMultiError(null);
    setSelected(new Set());
    setCreatedTrip(null);
    setTripError(null);
    try {
      const p = await getPlan(SOURCE, destination, dataset);
      setPlan(p);
      const byId = new Map<number, Order>();
      for (const m of p.intermediate) for (const o of m.orders) byId.set(o.id, o);
      for (const o of p.destination_orders) byId.set(o.id, o);
      setOrdersById(byId);
    } catch (e) {
      setPlan(null);
      setPlanError(errorMessage(e, 'Could not calculate the route.'));
    } finally {
      setPlanLoading(false);
    }
  };

  const runDetour = async () => {
    if (!detourCity || !destination) return;
    setDetourLoading(true);
    setDetourError(null);
    try {
      const d = await getDetour(SOURCE, destination, detourCity, dataset);
      setDetour(d);
      setOrdersById((prev) => {
        const next = new Map(prev);
        for (const o of d.eligible_orders) next.set(o.id, o);
        return next;
      });
    } catch (e) {
      setDetour(null);
      setDetourError(errorMessage(e, 'Could not calculate the detour.'));
    } finally {
      setDetourLoading(false);
    }
  };

  const runMultiStop = async () => {
    if (!destination) return;
    setMultiLoading(true);
    setMultiError(null);
    try {
      setMulti(await postMultiStop(SOURCE, destination, multiStops, dataset));
    } catch (e) {
      setMulti(null);
      setMultiError(errorMessage(e, 'Could not calculate the multi-stop route.'));
    } finally {
      setMultiLoading(false);
    }
  };

  const toggleOrder = (id: number, on: boolean) => {
    setSelected((prev) => {
      const next = new Set(prev);
      if (on) next.add(id);
      else next.delete(id);
      return next;
    });
    setTripError(null);
    setCreatedTrip(null);
  };

  const createTripFromSelection = async () => {
    setTripError(null);
    setCreatedTrip(null);
    if (selected.size === 0) {
      setTripError('Select at least one eligible order first.');
      return;
    }
    // Frontend pre-check; the backend enforces the same rule and its 400
    // message is shown verbatim if it fires. Wording matches the backend's
    // Store::createTrip message exactly (Phase 4B-5 removed the
    // console-menu "(option 7)" wording from the backend message).
    for (const id of selected) {
      const o = ordersById.get(id);
      if (o && o.status !== 'Processing') {
        setTripError(`Order ${id} is ${o.status}, not Processing. Process it first, then assign it to a trip.`);
        return;
      }
    }
    if (dataset !== 'production') {
      setTripError('Trips can only be created on the production routes.');
      return;
    }
    setTripBusy(true);
    try {
      // Stops must cover every city the selected orders come from, or the
      // backend rejects the trip ("not on the route"). Plan intermediates
      // are always on the route; the detour city only counts if it is
      // passed as a stop — include it when a detour-city order is selected.
      const stops = [...(plan?.intermediate.map((m) => m.city) ?? [])];
      const detourIds = new Set((detour?.eligible_orders ?? []).map((o) => o.id));
      if (detour && [...selected].some((id) => detourIds.has(id)) && !stops.includes(detour.detour_city)) {
        stops.push(detour.detour_city);
      }
      const trip = await createTrip(SOURCE, destination, stops, [...selected]);
      setCreatedTrip(trip);
      setSelected(new Set());
      // Statuses changed (Processing → Shipped): re-run the plan so the
      // eligible-order lists reflect the live data. No optimistic update.
      await runPlan();
    } catch (e) {
      setTripError(errorMessage(e, 'Could not create the trip.'));
    } finally {
      setTripBusy(false);
    }
  };

  const toggleMultiStop = (city: string, on: boolean) => {
    setMultiStops((prev) => (on ? [...prev, city] : prev.filter((c) => c !== city)));
    setMulti(null);
    setMultiError(null);
  };

  const detourOptions = cities.filter((c) => c !== SOURCE && c !== destination);
  const multiOptions = cities.filter((c) => c !== SOURCE && c !== destination);
  const selectedOrders = [...selected].map((id) => ordersById.get(id)).filter((o): o is Order => o !== undefined);
  const allSelectedProcessing = selectedOrders.length > 0 && selectedOrders.every((o) => o.status === 'Processing');
  const planHasEligible =
    plan !== null &&
    (plan.intermediate.some((m) => m.orders.length > 0) || plan.destination_orders.length > 0 || (detour?.eligible_orders.length ?? 0) > 0);

  return (
    <div className="space-y-4">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <h2 className="flex items-center gap-2 text-2xl font-bold text-slate-900 dark:text-white">
            <RouteIcon className="h-6 w-6 text-emerald-600" aria-hidden /> Delivery Planner
          </h2>
          <p className="text-sm text-slate-500 dark:text-slate-400">
            Shortest routes, detours and multi-stop trips from the live delivery graph. Source is fixed to Lahore (warehouse).
          </p>
        </div>
        {/* Dataset toggle */}
        <div className="flex rounded-lg border border-slate-300 bg-white p-1 dark:border-slate-600 dark:bg-slate-900" role="group" aria-label="Dataset">
          <button
            onClick={() => switchDataset('production')}
            className={`rounded-md px-3 py-1.5 text-sm font-medium ${dataset === 'production' ? 'bg-emerald-600 text-white' : 'text-slate-600 hover:bg-slate-100 dark:text-slate-300 dark:hover:bg-slate-800'}`}
          >
            Production routes
          </button>
          <button
            onClick={() => switchDataset('test')}
            className={`rounded-md px-3 py-1.5 text-sm font-medium ${dataset === 'test' ? 'bg-emerald-600 text-white' : 'text-slate-600 hover:bg-slate-100 dark:text-slate-300 dark:hover:bg-slate-800'}`}
          >
            Test dataset demo
          </button>
        </div>
      </div>

      {dataset === 'test' && (
        <div className="rounded-lg border border-amber-200 bg-amber-50 px-4 py-3 text-sm text-amber-800 dark:border-amber-900 dark:bg-amber-950/60 dark:text-amber-200" role="note">
          Demo: production routes minus the Lahore–Islamabad direct edge — Lahore→Islamabad now goes via Rawalpindi.
          Trips cannot be created in this mode; switch back to Production routes to create a trip.
        </div>
      )}

      {graphError && <ErrorBanner message={graphError} onRetry={() => void loadGraph(dataset)} />}

      {/* Controls + graph */}
      <div className="grid gap-4 lg:grid-cols-[320px_1fr]">
        <div className="space-y-4">
          <div className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900">
            <label className="flex flex-col gap-1 text-xs font-medium text-slate-600 dark:text-slate-300">
              Destination (click a city on the map, or pick here)
              <select value={destination} onChange={(e) => { setDestination(e.target.value); setPlan(null); }} className={inputClass} aria-label="Destination city">
                {destinationOptions.map((c) => <option key={c} value={c}>{c}</option>)}
              </select>
            </label>
            <p className="mt-2 text-xs text-slate-500 dark:text-slate-400">Source: <strong>Lahore</strong> (fixed warehouse). Clicking a city node sets it as the destination.</p>
            <button onClick={() => void runPlan()} disabled={planLoading || !destination} className={`mt-3 w-full ${buttonPrimary}`}>
              {planLoading ? 'Calculating…' : 'Calculate route'}
            </button>
            {planError && <div className="mt-3"><ErrorBanner message={planError} /></div>}
            {plan && (
              <div className="mt-4 space-y-2 text-sm">
                <p className="font-semibold text-slate-900 dark:text-white">Shortest path</p>
                <p className="break-words">{plan.path.join(' → ')}</p>
                <p><strong>{plan.total_time_min} min</strong> · <strong>{plan.total_distance_km} km</strong></p>
                {plan.log_warning && (
                  <p className="rounded-md bg-amber-50 px-2 py-1 text-xs text-amber-800 dark:bg-amber-950/60 dark:text-amber-200">
                    Route calculated, but the activity log could not be written: {plan.log_warning}
                  </p>
                )}
              </div>
            )}
            {!plan && !planLoading && !planError && (
              <p className="mt-4 text-sm text-slate-500 dark:text-slate-400">No route calculated yet — pick a destination and press “Calculate route”.</p>
            )}
          </div>

          {/* Detour */}
          <div className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900">
            <h3 className="flex items-center gap-2 font-semibold text-slate-900 dark:text-white">
              <Split className="h-4 w-4 text-emerald-600" aria-hidden /> Detour
            </h3>
            <p className="mt-1 text-xs text-slate-500 dark:text-slate-400">
              Route via one extra city to pick up its eligible orders. A detour always costs extra time here — it is never an improvement, only a trade-off.
            </p>
            <div className="mt-3 flex gap-2">
              <select value={detourCity} onChange={(e) => setDetourCity(e.target.value)} className={inputClass} aria-label="Detour city">
                <option value="">Via…</option>
                {detourOptions.map((c) => <option key={c} value={c}>{c}</option>)}
              </select>
              <button onClick={() => void runDetour()} disabled={detourLoading || !detourCity || !plan} className={buttonPrimary} title={!plan ? 'Calculate a route first' : undefined}>
                {detourLoading ? 'Checking…' : 'Check detour'}
              </button>
            </div>
            {!plan && <p className="mt-2 text-xs text-slate-500 dark:text-slate-400">Calculate a route first — the detour is compared against it.</p>}
            {detourError && <div className="mt-3"><ErrorBanner message={detourError} /></div>}
            {detour && (
              <div className="mt-3 space-y-2 text-sm">
                <div className="grid grid-cols-2 gap-2">
                  <div className="rounded-lg border border-slate-200 p-2 dark:border-slate-700">
                    <p className="text-xs font-semibold text-slate-500 dark:text-slate-400">Original (direct)</p>
                    <p><strong>{detour.original.time_min} min</strong> · {detour.original.distance_km} km</p>
                  </div>
                  <div className="rounded-lg border border-emerald-200 bg-emerald-50 p-2 dark:border-emerald-900 dark:bg-emerald-950/40">
                    <p className="text-xs font-semibold text-slate-500 dark:text-slate-400">Via {detour.detour_city}</p>
                    <p><strong>{detour.via.time_min} min</strong> · {detour.via.distance_km} km</p>
                  </div>
                </div>
                <p>
                  Extra cost: <strong>+{detour.extra_time_min} min</strong> · +{detour.extra_distance_km} km
                  {detour.extra_time_min < 0 ? ' (this detour is actually faster)' : ''}
                </p>
                {detour.log_warning && <p className="text-xs text-amber-700 dark:text-amber-300">Log warning: {detour.log_warning}</p>}
              </div>
            )}
          </div>

          {/* Multi-stop */}
          <div className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900">
            <h3 className="flex items-center gap-2 font-semibold text-slate-900 dark:text-white">
              <MapIcon className="h-4 w-4 text-emerald-600" aria-hidden /> Multi-stop
            </h3>
            <p className="mt-1 text-xs text-slate-500 dark:text-slate-400">
              Exact method: tries every ordering of the stops (factorial complexity, O(k! × Dijkstra)) — fine for 6 cities, impractical for large networks.
            </p>
            <div className="mt-3 flex flex-wrap gap-2">
              {multiOptions.map((c) => (
                <label key={c} className="flex cursor-pointer items-center gap-1 rounded-full border border-slate-300 px-3 py-1 text-xs font-medium dark:border-slate-600">
                  <input type="checkbox" checked={multiStops.includes(c)} onChange={(e) => toggleMultiStop(c, e.target.checked)} className="accent-emerald-600" />
                  {c}
                </label>
              ))}
            </div>
            <button onClick={() => void runMultiStop()} disabled={multiLoading || multiStops.length === 0 || !plan} className={`mt-3 w-full ${buttonPrimary}`} title={!plan ? 'Calculate a route first' : undefined}>
              {multiLoading ? 'Searching orderings…' : 'Find best stop order'}
            </button>
            {!plan && <p className="mt-2 text-xs text-slate-500 dark:text-slate-400">Calculate a route first — the multi-stop result is compared against the direct route.</p>}
            {multiError && <div className="mt-3"><ErrorBanner message={multiError} /></div>}
            {multi && (
              <div className="mt-3 space-y-2 text-sm">
                <p><span className="font-semibold">Best stop order:</span> {multi.stop_order.length > 0 ? multi.stop_order.join(' → ') : '(no stops)'}</p>
                <p className="break-words"><span className="font-semibold">Path:</span> {multi.path.join(' → ')}</p>
                <p><span className="font-semibold">Total:</span> {multi.total_time_min} min · {multi.total_distance_km} km</p>
                {plan && (
                  <p className="rounded-lg bg-slate-100 px-2 py-1 text-xs dark:bg-slate-800">
                    vs direct route ({plan.total_time_min} min · {plan.total_distance_km} km):{' '}
                    {multi.total_time_min - plan.total_time_min >= 0 ? '+' : ''}
                    {multi.total_time_min - plan.total_time_min} min ·{' '}
                    {multi.total_distance_km - plan.total_distance_km >= 0 ? '+' : ''}
                    {multi.total_distance_km - plan.total_distance_km} km extra
                  </p>
                )}
                {multi.log_warning && <p className="text-xs text-amber-700 dark:text-amber-300">Log warning: {multi.log_warning}</p>}
              </div>
            )}
          </div>
        </div>

        {/* React Flow graph */}
        <div className="h-[520px] overflow-hidden rounded-xl border border-slate-200 bg-white shadow-sm dark:border-slate-700 dark:bg-slate-900">
          {graphLoading ? (
            <Spinner label="Loading delivery graph…" />
          ) : graph ? (
            <ReactFlow
              nodes={nodes}
              edges={edges}
              onNodeClick={(_, node) => { if (node.id !== SOURCE) { setDestination(node.id); setPlan(null); } }}
              fitView
              proOptions={{ hideAttribution: true }}
            >
              <Background />
              <Controls />
            </ReactFlow>
          ) : (
            <EmptyState title="Delivery graph unavailable" hint="Check the backend and retry." />
          )}
        </div>
      </div>

      {/* Eligible orders + trip creation */}
      {plan && (
        <div className="space-y-4">
          <div className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900">
            <h3 className="font-semibold text-slate-900 dark:text-white">Eligible orders — tick orders to include in a trip</h3>
            <p className="mt-1 text-xs text-slate-500 dark:text-slate-400">
              Only Pending and Processing orders on the route are eligible. Only Processing orders can actually be assigned to a trip — a Pending order must be processed first (Orders page).
            </p>
            {!planHasEligible && (
              <div className="mt-3"><EmptyState title="No eligible orders on this route" hint="No Pending or Processing orders are in the cities on this route right now." /></div>
            )}
            <div className="mt-3 grid gap-4 md:grid-cols-3">
              {/* Group 1: on the shortest path */}
              <div>
                <h4 className="mb-2 text-sm font-semibold text-emerald-700 dark:text-emerald-300">On the shortest path</h4>
                {plan.intermediate.length === 0 ? (
                  <p className="text-sm text-slate-500 dark:text-slate-400">No intermediate cities on this route.</p>
                ) : (
                  <div className="space-y-3">
                    {plan.intermediate.map((m) => (
                      <div key={m.city}>
                        <p className="mb-1 text-xs font-medium text-slate-600 dark:text-slate-300">
                          {m.city} — {m.time_to_city_min} min · {m.distance_to_city_km} km from Lahore
                        </p>
                        {m.orders.length === 0 ? (
                          <p className="text-xs text-slate-400">No eligible orders here.</p>
                        ) : (
                          <div className="space-y-1">
                            {m.orders.map((o) => (
                              <OrderCheckRow key={o.id} order={o} checked={selected.has(o.id)} onToggle={toggleOrder} />
                            ))}
                          </div>
                        )}
                      </div>
                    ))}
                  </div>
                )}
              </div>
              {/* Group 2: at the destination */}
              <div>
                <h4 className="mb-2 text-sm font-semibold text-blue-700 dark:text-blue-300">At the destination</h4>
                <p className="mb-1 text-xs font-medium text-slate-600 dark:text-slate-300">{destination}</p>
                {plan.destination_orders.length === 0 ? (
                  <p className="text-sm text-slate-500 dark:text-slate-400">No eligible orders in {destination}.</p>
                ) : (
                  <div className="space-y-1">
                    {plan.destination_orders.map((o) => (
                      <OrderCheckRow key={o.id} order={o} checked={selected.has(o.id)} onToggle={toggleOrder} />
                    ))}
                  </div>
                )}
              </div>
              {/* Group 3: needs a detour (only after a detour is calculated) */}
              <div>
                <h4 className="mb-2 text-sm font-semibold text-violet-700 dark:text-violet-300">Needs a detour</h4>
                {!detour ? (
                  <p className="text-sm text-slate-500 dark:text-slate-400">Calculate a detour (left panel) to see orders in a city off the shortest path.</p>
                ) : detour.eligible_orders.length === 0 ? (
                  <p className="text-sm text-slate-500 dark:text-slate-400">No eligible orders in {detour.detour_city}.</p>
                ) : (
                  <div className="space-y-1">
                    <p className="mb-1 text-xs font-medium text-slate-600 dark:text-slate-300">
                      {detour.detour_city} — via route costs +{detour.extra_time_min} min · +{detour.extra_distance_km} km
                    </p>
                    {detour.eligible_orders.map((o) => (
                      <OrderCheckRow key={o.id} order={o} checked={selected.has(o.id)} onToggle={toggleOrder} />
                    ))}
                  </div>
                )}
              </div>
            </div>
          </div>

          {/* Trip creation */}
          <div className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900">
            <h3 className="flex items-center gap-2 font-semibold text-slate-900 dark:text-white">
              <Truck className="h-5 w-5 text-emerald-600" aria-hidden /> Create trip from selected orders
            </h3>
            <p className="mt-1 text-sm text-slate-500 dark:text-slate-400">
              {selected.size === 0
                ? 'No orders selected yet.'
                : `${selected.size} order${selected.size === 1 ? '' : 's'} selected: ${[...selected].sort((a, b) => a - b).join(', ')}`}
              {selected.size > 0 && !allSelectedProcessing && ' — every selected order must be Processing before a trip can be created.'}
            </p>
            {dataset !== 'production' && (
              <p className="mt-2 text-sm text-amber-700 dark:text-amber-300">
                Trips are production-only: the test dataset is a demo network, so trips cannot be created while it is selected.
              </p>
            )}
            {tripError && <div className="mt-3"><ErrorBanner message={tripError} /></div>}
            {createdTrip && (
              <div className="mt-3 rounded-lg border border-emerald-200 bg-emerald-50 px-4 py-3 text-sm text-emerald-800 dark:border-emerald-900 dark:bg-emerald-950/60 dark:text-emerald-200" role="status">
                Trip #{createdTrip.trip_id} created ({createdTrip.source} → {createdTrip.destination}, {createdTrip.time_min} min · {createdTrip.distance_km} km).
                Its orders are now Shipped.{' '}
                <Link to="/trips" className="font-semibold underline">View on the Trips page →</Link>
              </div>
            )}
            <div className="mt-3 flex flex-wrap gap-2">
              <button
                onClick={() => void createTripFromSelection()}
                disabled={tripBusy || selected.size === 0 || dataset !== 'production'}
                className={buttonPrimary}
                title={dataset !== 'production' ? 'Trips can only be created on the production routes' : undefined}
              >
                {tripBusy ? 'Creating trip…' : 'Create trip from selected orders'}
              </button>
              <button onClick={() => { setSelected(new Set()); setTripError(null); }} className={buttonSecondary}>Clear selection</button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
