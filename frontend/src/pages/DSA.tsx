import { useCallback, useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { Bar, BarChart, CartesianGrid, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import { GraduationCap } from 'lucide-react';
import {
  getBenchmark, getLogTree, getOrderTree, getPlan, getQueue, postMultiStop,
  type Benchmark, type LogTree, type MultiStopResult, type Order, type OrderTree,
  type OrderTreeNode, type PlanResult,
} from '../lib/api';
import { ErrorBanner, Spinner } from '../components/ui';
import { TreeFlow, type FlowTreeNode } from '../components/TreeFlow';

function orderNodeToFlow(n: OrderTreeNode | null): FlowTreeNode | null {
  if (!n) return null;
  return {
    key: `order-${n.order_id}`,
    label: `#${n.order_id} · ${n.status}`,
    detail: `${n.customer_name} · ${n.city}`,
    left: orderNodeToFlow(n.left),
    right: orderNodeToFlow(n.right),
  };
}

function Section({
  n,
  title,
  source,
  children,
}: {
  n: string;
  title: string;
  source: string;
  children: React.ReactNode;
}) {
  return (
    <section className="rounded-xl border border-slate-200 bg-white p-4 shadow-sm dark:border-slate-700 dark:bg-slate-900" aria-label={title}>
      <h3 className="text-base font-semibold text-slate-900 dark:text-white">
        {n}. {title}
      </h3>
      <p className="mt-0.5 text-xs font-medium text-slate-500 dark:text-slate-400">Source: {source}</p>
      <div className="mt-3 space-y-2 text-sm text-slate-700 dark:text-slate-200">{children}</div>
    </section>
  );
}

type Settled<T> = { ok: true; value: T } | { ok: false; error: string };

function toSettled<T>(p: Promise<T>): Promise<Settled<T>> {
  return p
    .then((value) => ({ ok: true as const, value }))
    .catch((e: unknown) => ({ ok: false as const, error: e instanceof Error ? e.message : 'Request failed.' }));
}

export default function DSA() {
  const [benchmark, setBenchmark] = useState<Settled<Benchmark> | null>(null);
  const [queue, setQueue] = useState<Settled<Order[]> | null>(null);
  const [planProd, setPlanProd] = useState<Settled<PlanResult> | null>(null);
  const [planTest, setPlanTest] = useState<Settled<PlanResult> | null>(null);
  const [logTree, setLogTree] = useState<Settled<LogTree> | null>(null);
  const [orderTree, setOrderTree] = useState<Settled<OrderTree> | null>(null);
  const [multi, setMulti] = useState<Settled<MultiStopResult> | null>(null);
  const [loading, setLoading] = useState(true);

  const load = useCallback(async () => {
    setLoading(true);
    const [b, q, pp, pt, lt, ot, m] = await Promise.all([
      toSettled(getBenchmark()),
      toSettled(getQueue()),
      toSettled(getPlan('Lahore', 'Karachi', 'production', false)),
      toSettled(getPlan('Lahore', 'Islamabad', 'test', false)),
      toSettled(getLogTree()),
      toSettled(getOrderTree()),
      toSettled(postMultiStop('Lahore', 'Karachi', ['Faisalabad', 'Multan'], 'production', false)),
    ]);
    setBenchmark(b);
    setQueue(q);
    setPlanProd(pp);
    setPlanTest(pt);
    setLogTree(lt);
    setOrderTree(ot);
    setMulti(m);
    setLoading(false);
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  if (loading) return <Spinner label="Loading live DSA data…" />;

  const benchData =
    benchmark?.ok
      ? [
          { name: 'Linked List', steps: benchmark.value.linked_list_steps },
          { name: 'BST', steps: benchmark.value.bst_steps },
        ]
      : [];

  return (
    <div className="space-y-4">
      <div>
        <h2 className="flex items-center gap-2 text-2xl font-bold text-slate-900 dark:text-white">
          <GraduationCap className="h-6 w-6 text-emerald-600" aria-hidden /> DSA Performance
        </h2>
        <p className="text-sm text-slate-500 dark:text-slate-400">
          What each data structure in the C++ backend actually does, with live numbers from the API.
          Every number below names its source; nothing here is invented or measured by hand.
        </p>
      </div>

      {/* 1. Linked List vs BST */}
      <Section n="1" title="Linked List vs BST — searching for orders" source="GET /api/benchmark (live)">
        <p>
          <strong>What it stores:</strong> the Linked List (<code>LinkedList.h</code>) holds every order in
          insertion order; the Order BST (<code>OrderBST.h</code>) holds the same orders keyed by order ID.
        </p>
        <p>
          <strong>What the backend actually does:</strong> it searches for the same 5 order IDs, 200 times each
          (1,000 searches total), counting every node visited, and reports the totals.
        </p>
        {benchmark?.ok ? (
          <>
            <p>
              <strong>1,000 searches:</strong> Linked List <strong>{benchmark.value.linked_list_steps.toLocaleString()} steps</strong> vs
              BST <strong>{benchmark.value.bst_steps.toLocaleString()} steps</strong>. Searching for order 1024 alone
              takes <strong>{benchmark.value.bst_search_1024_steps} steps</strong> in the BST.
            </p>
            <div className="h-56">
              <ResponsiveContainer width="100%" height="100%">
                <BarChart data={benchData}>
                  <CartesianGrid strokeDasharray="3 3" />
                  <XAxis dataKey="name" tick={{ fontSize: 12 }} />
                  <YAxis tick={{ fontSize: 12 }} />
                  <Tooltip formatter={(v: number | string) => `${Number(v).toLocaleString()} steps`} />
                  <Bar dataKey="steps" fill="#059669" name="Steps (1,000 searches)" />
                </BarChart>
              </ResponsiveContainer>
            </div>
            {benchmark.value.note && <p className="text-xs text-slate-500 dark:text-slate-400">Backend note: {benchmark.value.note}</p>}
          </>
        ) : (
          <ErrorBanner message={benchmark?.error ?? 'Benchmark unavailable.'} onRetry={() => void load()} />
        )}
        <p>
          <strong>Observed on this dataset (30 orders):</strong> the BST visited far fewer nodes than the list for
          the same 1,000 searches (see the live numbers above).
        </p>
        <p>
          <strong>Theory:</strong> List O(n), BST O(log n) average, O(n) worst case — this BST is NOT self-balancing;
          sorted IDs stay searchable because loading inserts middle-first (insertBalancedBST), not because the tree
          rebalances itself.
        </p>
      </Section>

      {/* 2. Queue */}
      <Section n="2" title="Queue — Pending orders waiting to be processed" source="GET /api/queue (live)">
        <p>
          <strong>What it stores:</strong> the Pending orders, front → rear (<code>OrderQueue.h</code>). Only Pending
          orders are in the queue — an order leaves it the moment it is processed, cancelled or deleted.
        </p>
        <p>
          <strong>What the backend actually does:</strong> the queue is rebuilt from <code>orders.csv</code> on every
          load, so it cannot go stale after a restart. Processing takes from the front (FIFO); new Pending orders
          join at the rear.
        </p>
        {queue?.ok ? (
          <p>
            Live right now: <strong>{queue.value.length}</strong> order{queue.value.length === 1 ? '' : 's'} in the
            queue
            {queue.value.length > 0 && (
              <> — front order is <strong>#{queue.value[0].id}</strong> ({queue.value[0].customer_name},{' '}
              {queue.value[0].city})</>
            )}
            . <Link to="/queue" className="font-medium text-emerald-600 hover:underline">Open the Processing Queue page →</Link>
          </p>
        ) : (
          <ErrorBanner message={queue?.error ?? 'Queue unavailable.'} onRetry={() => void load()} />
        )}
      </Section>

      {/* 3. Stack */}
      <Section n="3" title="Stack — Undo" source="theory + Store behaviour (no count endpoint exists)">
        <p>
          <strong>What it stores:</strong> the Action Stack (<code>ActionStack.h</code>) holds one record per
          undoable action — the order, its old status, and its exact queue position before the action.
        </p>
        <p>
          <strong>What the backend actually does:</strong> Undo pops the top action and restores the order&apos;s
          status <em>and</em> its exact original queue position (Phase 3b rule — not just front or rear). Undo
          appends a new UNDO entry to the activity log and never erases history.
        </p>
        <p>
          <strong>Undo depth is not exposed by the API.</strong> <code>GET /api/stats</code> has no undo count, so
          no number is shown here — inventing one would be fake data. Use the Queue page&apos;s “Undo last action”
          button to undo; the backend answers 409 (“Nothing to undo.”) when the stack is empty.
        </p>
      </Section>

      {/* 4. Graph + Dijkstra */}
      <Section n="4" title="Graph + Dijkstra — shortest delivery routes" source="GET /api/delivery/plan (live, two calls)">
        <p>
          <strong>What it stores:</strong> the delivery network (<code>DeliveryGraph.h</code>) — 6 cities, 12 roads,
          each road with a time in minutes and a distance in kilometres.
        </p>
        <p>
          <strong>What the backend actually does:</strong> Dijkstra&apos;s algorithm as implemented in{' '}
          <code>DeliveryGraph.h</code> — O(V²) with the adjacency-matrix implementation used here — finds the
          cheapest route by time. A route can pass through intermediate cities and serve their eligible orders.
        </p>
        {planProd?.ok ? (
          <p>
            Live, production routes: <strong>Lahore → Karachi</strong> — {planProd.value.path.join(' → ')} —{' '}
            <strong>{planProd.value.total_time_min} min</strong> · <strong>{planProd.value.total_distance_km} km</strong> (direct, no intermediate city).
          </p>
        ) : (
          <ErrorBanner message={planProd?.error ?? 'Production plan unavailable.'} onRetry={() => void load()} />
        )}
        {planTest?.ok ? (
          <p>
            Live, test dataset (production routes minus the Lahore–Islamabad direct edge):{' '}
            <strong>Lahore → Islamabad</strong> — {planTest.value.path.join(' → ')} —{' '}
            <strong>{planTest.value.total_time_min} min</strong> · <strong>{planTest.value.total_distance_km} km</strong>,
            via Rawalpindi.
          </p>
        ) : (
          <ErrorBanner message={planTest?.error ?? 'Test plan unavailable.'} onRetry={() => void load()} />
        )}
        <p>
          <Link to="/planner" className="font-medium text-emerald-600 hover:underline">Try it yourself on the Delivery Planner →</Link>
        </p>
      </Section>

      {/* 5. Activity Log BST */}
      <Section n="5" title="Activity Log BST — the audit trail as a tree" source="GET /api/logs/tree (live)">
        <p>
          <strong>What it stores:</strong> every action on an order (<code>ActivityLog.h</code>) — a separate custom
          BST keyed by (timestamp, logId), persisted append-only in <code>activity_logs.csv</code>.
        </p>
        <p>
          <strong>What the backend actually does:</strong> each action appends one entry; Undo appends a UNDO entry
          instead of erasing history. Inorder traversal of the tree IS the timeline.
        </p>
        {logTree?.ok ? (
          <>
            <p>
              Live: <strong>{logTree.value.size}</strong> log entries, tree height{' '}
              <strong>{logTree.value.height}</strong>
              {logTree.value.size > 0 && logTree.value.height === logTree.value.size && (
                <> — height equals size: the tree is right-leaning, not balanced (timestamps arrive in increasing
                order and this BST is not self-balancing).</>
              )}
            </p>
            <p>
              Ordered by (timestamp, logId) — inorder traversal IS the timeline. Searching by log ID alone is O(n):
              the tree is ordered by time, not by ID, so there is no shortcut to a log ID.
            </p>
            <p>
              <Link to="/logs" className="font-medium text-emerald-600 hover:underline">See the full log tree on the Activity Logs page →</Link>
            </p>
          </>
        ) : (
          <ErrorBanner message={logTree?.error ?? 'Log tree unavailable.'} onRetry={() => void load()} />
        )}
      </Section>

      {/* 6. Order BST visualisation */}
      <Section n="6" title="Order BST — the live tree" source="GET /api/orders/tree (live)">
        <p>
          <strong>What it stores:</strong> all orders keyed by order ID (<code>OrderBST.h</code>), loaded
          middle-first (insertBalancedBST) so sorted IDs 1001–1030 do not degenerate into a list.
        </p>
        {orderTree?.ok ? (
          <>
            <p>
              Live: root order <strong>#{orderTree.value.root?.order_id ?? '—'}</strong>,{' '}
              <strong>{orderTree.value.size}</strong> orders, height{' '}
              <strong>{orderTree.value.height}</strong> (real recursive walk; 0 = empty, 1 = single node).
            </p>
            <TreeFlow
              root={orderNodeToFlow(orderTree.value.root)}
              totalSize={orderTree.value.size}
              emptyLabel="No orders yet"
              emptyHint="The tree appears once orders exist."
            />
          </>
        ) : (
          <ErrorBanner message={orderTree?.error ?? 'Order tree unavailable.'} onRetry={() => void load()} />
        )}
      </Section>

      {/* 7. Multi-stop */}
      <Section n="7" title="Multi-stop — exact best route through several cities" source="POST /api/delivery/multi-stop (live)">
        <p>
          <strong>What it stores / does:</strong> <code>RoutePlanner</code> (<code>Delivery.h</code>) tries every
          ordering of the stops and runs Dijkstra for each leg — exact permutation search, O(k! × Dijkstra), max 6
          stops — then keeps the fastest route (ties broken by distance).
        </p>
        <p>
          Live call just made by this page: Lahore → Karachi, stops Faisalabad + Multan, production routes.
        </p>
        {multi?.ok ? (
          <p>
            Best stop order: <strong>{multi.value.stop_order.length > 0 ? multi.value.stop_order.join(' → ') : '(no stops)'}</strong>
            {' '}· path: {multi.value.path.join(' → ')} · total{' '}
            <strong>{multi.value.total_time_min} min</strong> ·{' '}
            <strong>{multi.value.total_distance_km} km</strong>.
            {planProd?.ok && (
              <> That is {multi.value.total_time_min - planProd.value.total_time_min} min more than the direct
              Lahore → Karachi route ({planProd.value.total_time_min} min) — the price of serving two extra cities.</>
            )}
          </p>
        ) : (
          <ErrorBanner message={multi?.error ?? 'Multi-stop unavailable.'} onRetry={() => void load()} />
        )}
      </Section>

      <p className="text-xs text-slate-400">
        Note: this page calls the planning endpoints with <code>audit=false</code> — read-only
        demonstrations that do not append ROUTE_GENERATED entries to the activity log. Viewing this
        page changes no order status and grows no log. The Delivery Planner, by contrast, uses the
        audited default, so its route calculations are recorded.
      </p>
    </div>
  );
}
