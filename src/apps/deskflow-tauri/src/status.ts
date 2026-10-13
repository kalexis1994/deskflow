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
  const host = ov.remoteHost || "la otra computadora";

  if (core.process === "starting") return { tone: "busy", title: "Iniciando…", detail: "Arrancando el servicio de Deskflow." };
  if (core.process === "stopping") return { tone: "busy", title: "Deteniendo…", detail: "Cerrando la conexión." };
  if (core.process === "stopped" || core.ipc === "disconnected") {
    return { tone: "off", title: "Apagado", detail: "No se está compartiendo el teclado ni el mouse." };
  }

  const isServer = ov.coreMode === "server";
  switch (core.connection) {
    case "Connected":
      if (isServer) {
        const n = core.clients.length;
        return {
          tone: "ok",
          title: n === 1 ? `Compartiendo con ${core.clients[0]}` : n > 1 ? `Compartiendo con ${n} computadoras` : "Compartiendo",
          detail: "Llevá el mouse al borde de la pantalla para pasar a la otra computadora.",
        };
      }
      return {
        tone: "ok",
        title: `Conectado a ${host}`,
        detail: "El teclado y el mouse de esa computadora ya pueden controlar esta.",
      };
    case "Listening":
      return {
        tone: "wait",
        title: "Esperando computadoras",
        detail: address
          ? `En la otra computadora elegí “Usar el teclado de otra” y escribí ${address}.`
          : "En la otra computadora elegí “Usar el teclado de otra” y escribí la IP de esta.",
      };
    case "Connecting":
      if (isServer) return { tone: "busy", title: "Preparando…", detail: "Abriendo el puerto para que se conecten." };
      return {
        tone: core.refusal ? "warn" : "busy",
        title: `Buscando a ${host}…`,
        detail:
          retryIn !== null && retryIn > 0
            ? `No respondió. Reintento en ${retryIn} s.`
            : "Asegurate de que Deskflow esté abierto y compartiendo en esa computadora.",
      };
    default:
      return { tone: "warn", title: "Desconectado", detail: "El servicio está activo pero no hay conexión." };
  }
}

export function describeRefusal(reason: ConnectionRefusal, computerName: string): string {
  switch (reason) {
    case "IncompatibleVersion":
      return "La otra computadora tiene una versión de Deskflow incompatible. Actualizá las dos a la misma versión.";
    case "AlreadyConnected":
      return `Ya hay otra computadora conectada con el nombre “${computerName}”. Cambiá el nombre de una de las dos.`;
    case "UnknownClient":
      return `La otra computadora no reconoce a “${computerName}”. Agregala a su diseño de pantallas.`;
    case "ProtocolError":
      return "Hubo un error de comunicación con la otra computadora.";
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
