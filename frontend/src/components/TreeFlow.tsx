import { useMemo } from 'react';
import { Background, Controls, ReactFlow, type Edge, type Node } from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import { EmptyState } from './ui';

/**
 * One node of a backend tree, adapted to the shape React Flow needs.
 * `left` / `right` are the REAL children from the API response — an edge is
 * drawn only where the backend says a child exists.
 */
export interface FlowTreeNode {
  key: string;
  label: string;
  detail?: string;
  left: FlowTreeNode | null;
  right: FlowTreeNode | null;
}

const X_SPACING = 180;
const Y_SPACING = 95;
const MAX_SHOWN = 40;

/**
 * Renders a real backend BST with @xyflow/react.
 * Layout is computed from the actual parent/child links: x = inorder index
 * × spacing, y = depth × spacing. If the tree has more than 40 nodes, only
 * the first 40 by inorder are rendered (still real nodes, not invented).
 */
export function TreeFlow({
  root,
  totalSize,
  emptyLabel,
  emptyHint,
}: {
  root: FlowTreeNode | null;
  totalSize: number;
  emptyLabel: string;
  emptyHint?: string;
}) {
  const { nodes, edges, shown } = useMemo(() => {
    if (!root) return { nodes: [] as Node[], edges: [] as Edge[], shown: 0 };
    // Recursive walk: inorder traversal, tracking each node's depth.
    const inorder: { node: FlowTreeNode; depth: number }[] = [];
    const walk = (n: FlowTreeNode | null, depth: number): void => {
      if (!n) return;
      walk(n.left, depth + 1);
      inorder.push({ node: n, depth });
      walk(n.right, depth + 1);
    };
    walk(root, 0);
    const shownList = inorder.slice(0, MAX_SHOWN);
    const shownKeys = new Set(shownList.map((e) => e.node.key));
    const nodes: Node[] = shownList.map((e, i) => ({
      id: e.node.key,
      position: { x: i * X_SPACING, y: e.depth * Y_SPACING },
      data: {
        label: (
          <div>
            <div className="font-semibold">{e.node.label}</div>
            {e.node.detail ? <div className="text-xs font-normal text-slate-500">{e.node.detail}</div> : null}
          </div>
        ),
      },
      style: {
        background: e.depth === 0 ? '#d1fae5' : '#ffffff',
        border: e.depth === 0 ? '2px solid #059669' : '1px solid #94a3b8',
        borderRadius: 10,
        padding: 8,
        fontSize: 12,
        minWidth: 150,
        textAlign: 'left' as const,
      },
    }));
    // Edges only where the backend says a child exists AND both ends are shown.
    const edges: Edge[] = [];
    for (const { node } of shownList) {
      for (const child of [node.left, node.right]) {
        if (child && shownKeys.has(child.key)) {
          edges.push({ id: `${node.key}->${child.key}`, source: node.key, target: child.key });
        }
      }
    }
    return { nodes, edges, shown: shownList.length };
  }, [root]);

  if (!root || totalSize === 0) {
    return (
      <div className="rounded-xl border border-slate-200 bg-white dark:border-slate-700 dark:bg-slate-900">
        <EmptyState title={emptyLabel} hint={emptyHint} />
      </div>
    );
  }

  return (
    <div>
      {shown < totalSize && (
        <p className="mb-2 text-sm font-medium text-amber-700 dark:text-amber-300">
          Showing first {shown} of {totalSize} nodes (by inorder). The rest are real nodes too — the view is capped, not the data.
        </p>
      )}
      <div className="h-[420px] overflow-hidden rounded-xl border border-slate-200 bg-white shadow-sm dark:border-slate-700 dark:bg-slate-900">
        <ReactFlow nodes={nodes} edges={edges} fitView proOptions={{ hideAttribution: true }}>
          <Background />
          <Controls />
        </ReactFlow>
      </div>
      <p className="mt-2 text-xs text-slate-500 dark:text-slate-400">
        Layout: x = inorder position, y = depth in the tree. The green node is the root. Edges are drawn only where the backend reports a child.
      </p>
    </div>
  );
}
