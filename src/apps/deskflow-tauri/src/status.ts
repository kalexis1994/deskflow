import { t, tn } from "./i18n";
import type { ConnectionRefusal, CoreState, Overview } from "./types";

export type Tone = "off" | "busy" | "wait" | "ok" | "warn";

export interface StatusView {
  tone: Tone;
  title: string;
  detail: string;
}

/** Turns the raw core state into one sentence a person can act on. */
export function describeStatus(
  core: CoreState,
  ov: Overview,
  retryIn: number | null,
  address: string | null,
): StatusView {
  const host = ov.remoteHost;

  if (core.process === "starting") return { tone: "busy", title: t("app.status.starting.title"), detail: t("app.status.starting.detail") };
  if (core.process === "stopping") return { tone: "busy", title: t("app.status.stopping.title"), detail: t("app.status.stopping.detail") };
  if (core.process === "stopped" || core.ipc === "disconnected") {
    return { tone: "off", title: t("app.status.off.title"), detail: t("app.status.off.detail") };
  }

  const isServer = ov.coreMode === "server";
  switch (core.connection) {
    case "Connected":
      if (isServer) {
        const n = core.clients.length;
        return {
          tone: "ok",
          title:
            n === 1
              ? t("app.status.sharing.titleOne", { name: core.clients[0] })
              : n > 1
                ? tn("app.status.sharing.titleMany", n)
                : t("app.status.sharing.title"),
          detail: t("app.status.sharing.detail"),
        };
      }
      return {
        tone: "ok",
        title: host ? t("app.status.connected.title", { host }) : t("app.status.connected.titleUnknown"),
        detail: t("app.status.connected.detail"),
      };
    case "Listening":
      return {
        tone: "wait",
        title: t("app.status.listening.title"),
        detail: address ? t("app.status.listening.detail", { address }) : t("app.status.listening.detailNoAddress"),
      };
    case "Connecting":
      if (isServer) return { tone: "busy", title: t("app.status.preparing.title"), detail: t("app.status.preparing.detail") };
      return {
        tone: core.refusal ? "warn" : "busy",
        title: host ? t("app.status.connecting.title", { host }) : t("app.status.connecting.titleUnknown"),
        detail:
          retryIn !== null && retryIn > 0
            ? t("app.status.connecting.retry", { seconds: retryIn })
            : t("app.status.connecting.hint"),
      };
    default:
      return { tone: "warn", title: t("app.status.disconnected.title"), detail: t("app.status.disconnected.detail") };
  }
}

export function describeRefusal(reason: ConnectionRefusal, computerName: string): string {
  switch (reason) {
    case "IncompatibleVersion":
      return t("app.refusal.incompatibleVersion");
    case "AlreadyConnected":
      return t("app.refusal.alreadyConnected", { name: computerName });
    case "UnknownClient":
      return t("app.refusal.unknownClient", { name: computerName });
    case "ProtocolError":
      return t("app.refusal.protocolError");
  }
}

export type LogLevel = "error" | "warn" | "info" | "debug";

const LEVEL_RE = /\]\s+(FATAL|ERROR|WARNING|WARN|NOTE|INFO|DEBUG\d?)\s*:/;

export function parseLogLine(line: string): { time: string | null; level: LogLevel; text: string } {
  const time = line.match(/^\[(?:\d{4}-\d\d-\d\dT)?(\d\d:\d\d:\d\d)/)?.[1] ?? null;
  const m = line.match(LEVEL_RE);
  const raw = m?.[1] ?? "INFO";
  const level: LogLevel = raw === "FATAL" || raw === "ERROR" ? "error" : raw.startsWith("WARN") ? "warn" : raw.startsWith("DEBUG") ? "debug" : "info";
  const text = m ? line.slice((m.index ?? 0) + m[0].length).trim() : line.replace(/^\[[^\]]*\]\s*/, "");
  return { time, level, text };
}
