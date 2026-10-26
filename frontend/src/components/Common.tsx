import type { ReactNode } from "react";
import { CircleDashed, ShieldCheck, TriangleAlert } from "lucide-react";
import { json } from "../api/client";
export const words = (text: string) => text.replaceAll("_", " ");
export function Badge({ value }: { value: string }) {
  return (
    <span className={`badge ${value}`}>
      <span className="status-dot" />
      {words(value)}
    </span>
  );
}
export function Empty({
  title = "No session selected",
  children,
}: {
  title?: string;
  children?: ReactNode;
}) {
  return (
    <div className="empty">
      <CircleDashed size={32} />
      <h3>{title}</h3>
      <p>
        {children ||
          "Start an approved application below or select a retained session to inspect its real observations."}
      </p>
    </div>
  );
}
export function Panel({
  title,
  subtitle,
  children,
  action,
  className = "",
}: {
  title: string;
  subtitle?: string;
  children: ReactNode;
  action?: ReactNode;
  className?: string;
}) {
  return (
    <section className={`panel ${className}`}>
      <div className="panel-heading">
        <div>
          <h2>{title}</h2>
          {subtitle && <p>{subtitle}</p>}
        </div>
        {action}
      </div>
      {children}
    </section>
  );
}
export function Notice({
  children,
  danger = false,
}: {
  children: ReactNode;
  danger?: boolean;
}) {
  return (
    <div className={`notice ${danger ? "danger" : ""}`} role="status">
      <TriangleAlert size={17} />
      <div>{children}</div>
    </div>
  );
}
export function Details({ value }: { value: unknown }) {
  return <pre className="json-details">{json(value)}</pre>;
}
export function Completeness({ value }: { value: boolean | null }) {
  return (
    <span className={`completeness ${value === true ? "good" : "warn"}`}>
      {value === true ? <ShieldCheck size={15} /> : <TriangleAlert size={15} />}{" "}
      {value === null
        ? "Unknown"
        : value
          ? "Complete within monitor scope"
          : "Incomplete observation"}
    </span>
  );
}
