import { useEffect, useMemo, useState } from "react";
import {
  ReactFlow,
  Background,
  Controls,
  MiniMap,
  MarkerType,
  Position,
  BaseEdge,
  Handle,
  useNodesState,
  type EdgeProps,
} from "@xyflow/react";
import type { ReactNode } from "react";
import "@xyflow/react/dist/style.css";
import type { Graph } from "../api/types";
import { reportedCycleEdges } from "../lib/replay";
import { Details, Empty, Notice, Panel } from "./Common";
function CurvedEdge({
  sourceX,
  sourceY,
  targetX,
  targetY,
  markerEnd,
  style,
  id,
  source,
  target,
}: EdgeProps) {
  const dx = targetX - sourceX,
    dy = targetY - sourceY,
    len = Math.max(1, Math.hypot(dx, dy));
  const mx = (sourceX + targetX) / 2 - (dy / len) * 85,
    my = (sourceY + targetY) / 2 + (dx / len) * 85;
  const path =
    source === target
      ? `M ${sourceX} ${sourceY} C ${sourceX + 110} ${sourceY - 160}, ${targetX - 110} ${targetY - 160}, ${targetX} ${targetY}`
      : `M ${sourceX} ${sourceY} Q ${mx} ${my} ${targetX} ${targetY}`;
  return <BaseEdge id={id} path={path} markerEnd={markerEnd} style={style} />;
}
const edgeTypes = { dependency: CurvedEdge };
function LockIdentity({ data }: { data: { label: ReactNode } }) {
  return (
    <>
      <Handle type="source" position={Position.Right} id="right-out" />
      <Handle type="target" position={Position.Right} id="right-in" />
      {data.label}
      <Handle type="source" position={Position.Left} id="left-out" />
      <Handle type="target" position={Position.Left} id="left-in" />
    </>
  );
}
const nodeTypes = { lock: LockIdentity };
export function graphElements(graph: Graph) {
  const cycles = reportedCycleEdges(graph.reported_cycles);
  const cycleLocks = new Set(
    graph.reported_cycles.flatMap((e) => e.cycle || []),
  );
  const nodes = graph.nodes.map((node, i) => ({
    id: node.lock_id,
    data: {
      label: (
        <>
          <span className="lock-caption">
            MUTEX · {node.lock_kind || "unknown kind"}
          </span>
          <strong>{node.lock_id}</strong>
          <span className="lock-caption">
            {node.destroy_observed ? "destroy observed" : "observed identity"}
          </span>
        </>
      ),
    },
    position: { x: 70 + (i % 4) * 290, y: 120 + Math.floor(i / 4) * 190 },
    sourcePosition: Position.Right,
    targetPosition: Position.Left,
    className: cycleLocks.has(node.lock_id) ? "cycle-node" : "",
  }));
  const edges = graph.edges
    .filter(
      (e) =>
        graph.nodes.some((n) => n.lock_id === e.from_lock) &&
        graph.nodes.some((n) => n.lock_id === e.to_lock),
    )
    .map((e) => ({
      id: e.from_lock + ">" + e.to_lock,
      source: e.from_lock,
      target: e.to_lock,
      type: "dependency",
      data: { ...e, reportedCycle: cycles.has(e.from_lock + ">" + e.to_lock) },
      style: {
        stroke: cycles.has(e.from_lock + ">" + e.to_lock)
          ? "#fb7185"
          : "#7b91ef",
        strokeWidth: cycles.has(e.from_lock + ">" + e.to_lock) ? 2.8 : 2,
      },
      markerEnd: {
        type: MarkerType.ArrowClosed,
        color: cycles.has(e.from_lock + ">" + e.to_lock)
          ? "#fb7185"
          : "#7b91ef",
      },
    }));
  return {
    nodes: nodes.map((node) => ({ ...node, type: "lock" })),
    edges: edges.map((edge) => {
      const forward =
        nodes.findIndex((n) => n.id === edge.source) <=
        nodes.findIndex((n) => n.id === edge.target);
      return {
        ...edge,
        sourceHandle: forward ? "right-out" : "left-out",
        targetHandle: forward ? "left-in" : "right-in",
      };
    }),
  };
}
export function LockGraph({ graph }: { graph: Graph }) {
  const elements = useMemo(() => graphElements(graph), [graph]);
  const [nodes, setNodes, onNodesChange] = useNodesState(elements.nodes);
  const [detail, setDetail] = useState<unknown>(null);
  useEffect(() => {
    setNodes((old) =>
      elements.nodes.map((n) => ({
        ...n,
        position: old.find((o) => o.id === n.id)?.position || n.position,
      })),
    );
  }, [elements, setNodes]);
  return (
    <Panel
      title="Lock dependency graph"
      subtitle="Historical request relationships · C analyzer evidence"
      action={
        <span className="subtle">
          {graph.nodes.length} locks / {graph.edges.length} edges
        </span>
      }
    >
      {!graph.projection_complete && (
        <Notice>
          Projection capacity or retained-history limit reached. This graph is
          incomplete.
        </Notice>
      )}
      {graph.monitoring_complete === false && (
        <Notice>
          Monitoring is incomplete. Edges show retained supported observations
          only.
        </Notice>
      )}
      {nodes.length ? (
        <div className="graph-canvas" data-testid="lock-graph">
          <ReactFlow
            nodes={nodes}
            edges={elements.edges}
            nodeTypes={nodeTypes}
            edgeTypes={edgeTypes}
            onNodesChange={onNodesChange}
            fitView
            fitViewOptions={{ padding: 0.5 }}
            minZoom={0.15}
            maxZoom={2}
            colorMode="dark"
            nodesConnectable={false}
            onNodeClick={(_, node) =>
              setDetail(graph.nodes.find((n) => n.lock_id === node.id))
            }
            onEdgeClick={(_, edge) => setDetail(edge.data)}
          >
            <Background color="#26334a" gap={22} size={1} />
            <Controls showInteractive={false} />
            <MiniMap
              pannable
              zoomable
              nodeColor="#667cf0"
              maskColor="#090f1caa"
            />
          </ReactFlow>
        </div>
      ) : (
        <Empty title="No locks observed yet">
          The graph will appear when the monitor reports actual lock identities.
        </Empty>
      )}
      <div className="graph-legend">
        <span>
          <i className="legend-line" />
          Observed dependency
        </span>
        <span>
          <i className="legend-line cycle" />
          C-reported cycle edge
        </span>
        <span>Drag to pan · Scroll to zoom · Select for evidence</span>
      </div>
      {detail !== null && (
        <div className="selection">
          <div className="row-between">
            <h3>Selected graph evidence</h3>
            <button onClick={() => setDetail(null)}>Close details</button>
          </div>
          <Details value={detail} />
        </div>
      )}
      <p className="footnote">
        A potential cycle does not prove the application is currently
        deadlocked.
      </p>
    </Panel>
  );
}
