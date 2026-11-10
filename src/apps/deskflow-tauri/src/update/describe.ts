import { t } from "../i18n";
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

export const CHANNELS: UpdateChannel[] = ["stable", "alpha"];

export function channelLabel(channel: UpdateChannel): string {
  return channel === "alpha" ? t("update.channel.alpha") : t("update.channel.stable");
}

export function channelTitle(channel: UpdateChannel): string {
  return channel === "alpha" ? t("update.channel.alpha.title") : t("update.channel.stable.title");
}

/**
 * What the question has to say before anyone answers it: the update cuts Deskflow off for a moment.
 * Under the service nothing else asks; otherwise Windows does.
 */
export function whatHappens(mode: CoreMode, service: boolean): string {
  const cut = mode === "server" ? t("update.cut.server") : mode === "client" ? t("update.cut.client") : t("update.cut.none");
  const ask = service ? t("update.ask.service") : t("update.ask.windows");
  return t("update.whatHappens", { ask, cut });
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
  if (state.kind === "installing") return t("update.installing", { version: state.version });
  if (state.kind !== "downloading") return "";
  if (state.percent === null) return t("update.downloading", { version: state.version });
  return t("update.downloadingPercent", { version: state.version, percent: state.percent });
}

/** The short answer next to the version, after looking for one by hand. */
export function searchLine(state: UpdateState): string | null {
  switch (state.kind) {
    case "checking":
      return t("update.searching");
    case "upToDate":
      return t("update.upToDate");
    case "available":
      return state.dismissed ? t("update.newVersion", { version: state.version }) : null;
    default:
      return null;
  }
}
