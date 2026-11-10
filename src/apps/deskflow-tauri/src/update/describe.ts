import type { CoreMode } from "../types";

export type UpdatePhase =
  | { kind: "idle" }
  | { kind: "checking" }
  | { kind: "upToDate" }
  | { kind: "available"; version: string; notes: string | null; error: string | null }
  | { kind: "downloading"; version: string; percent: number | null }
  | { kind: "installing"; version: string }
  | { kind: "failed"; message: string };

export type UpdateChannel = "stable" | "alpha";

export type UpdateState = UpdatePhase & { current: string; channel: UpdateChannel; dismissed: boolean };

export const CHANNELS: { value: UpdateChannel; label: string; title: string }[] = [
  { value: "stable", label: "Estable", title: "Las versiones ya probadas" },
  { value: "alpha", label: "Alpha", title: "Versiones de prueba, que pueden fallar. Igual pregunta antes de instalar." },
];

/**
 * What the question has to say before anyone answers it: the update cuts Deskflow off for a moment.
 * Under the service nothing else asks; otherwise Windows does.
 */
export function whatHappens(mode: CoreMode, service: boolean): string {
  const cut =
    mode === "server"
      ? "Las otras computadoras se quedan sin tu teclado y mouse hasta que termine."
      : mode === "client"
        ? "Esta computadora se queda sin el teclado y el mouse compartidos hasta que termine."
        : "Deskflow se detiene hasta que termine.";
  const ask = service ? "No hay que aceptar nada más." : "Windows va a pedir permiso de administrador.";
  return `${ask} La instalación tarda unos segundos. ${cut} Después vuelve solo.`;
}

/** The question stays until it is answered; a search that found nothing has nothing to ask. */
export function isAsking(state: UpdateState): boolean {
  return state.kind === "available" && !state.dismissed;
}

/** Changing channel has to wait for what is already running. */
export function canChangeChannel(state: UpdateState): boolean {
  return state.kind !== "checking" && !isWorking(state);
}

export function isWorking(state: UpdateState): boolean {
  return state.kind === "downloading" || state.kind === "installing";
}

export function progressLine(state: UpdateState): string {
  if (state.kind === "installing") return `Instalando la versión ${state.version}…`;
  if (state.kind !== "downloading") return "";
  const percent = state.percent === null ? "" : ` (${state.percent}%)`;
  return `Bajando la versión ${state.version}${percent}…`;
}

/** The short answer next to the version, after looking for one by hand. */
export function searchLine(state: UpdateState): string | null {
  switch (state.kind) {
    case "checking":
      return "Buscando…";
    case "upToDate":
      return "Está al día";
    case "available":
      return state.dismissed ? `Hay una versión nueva: ${state.version}` : null;
    default:
      return null;
  }
}
