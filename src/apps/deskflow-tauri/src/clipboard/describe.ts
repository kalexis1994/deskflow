// Turns the core's clipboard reports (src-tauri/src/clipboard.rs) into sentences.

import { language, t, tn, type PluralKey, type Vars } from "../i18n";
import type { CoreMode } from "../types";

// Mirrors ClipboardActivity in src-tauri/src/clipboard.rs
export interface ClipboardActivity {
  event: "sent" | "received" | "shared" | "skipped" | "failed" | string;
  computer: string | null;
  reason: "tooLarge" | "nothingToShare" | "busy" | string | null;
  formats: string[];
  dropped: string[];
  size: number | null;
  chars: number | null;
  lines: number | null;
  image: { width: number; height: number } | null;
  text: string | null;
  /** A copy of files, when that is what it was. */
  files?: FilesSummary | null;
  /** Why it failed, in English, as the core logs it. */
  detail?: string | null;
  /** Unix ms when it reached the app. */
  at: number;
}

// Mirrors FilesSummary in src-tauri/src/clipboard.rs
export interface FilesSummary {
  count: number;
  folders: number;
  bytes: number;
  /** The first few top-level names. */
  names: string[];
  /** Top-level files and folders in all. */
  roots: number;
}

// Mirrors FileProgress in src-tauri/src/clipboard.rs
export interface FileProgress {
  direction: "send" | "receive" | string;
  computer: string | null;
  count: number;
  bytes: number;
  done: number;
}

// Mirrors ClipboardPrefs in src-tauri/src/clipboard.rs
export interface ClipboardPrefs {
  preview: boolean;
  sharing: boolean;
  limitMb: number;
  /** Copying files, decided on each computer. */
  files: boolean;
  filesLimitMb: number;
}

export type ClipboardTone = "ok" | "warn" | "danger";

export interface ClipboardView {
  tone: ClipboardTone;
  title: string;
  detail: string;
  /** Why part of it was left out, when something was. */
  note: string | null;
  /** "18 caracteres · 2 líneas", "1920 × 1080", "1,2 MB" */
  facts: string[];
  kind: "text" | "image" | "mixed" | "files" | "none";
}

/** The locale for numbers: Spanish keeps the Argentine format it always had. */
function locale(): string {
  const lang = language();
  return lang === "es" ? "es-AR" : lang;
}

/** A number in the current language's format: "1.500", "1,500". */
export const formatNumber = (n: number) => n.toLocaleString(locale());

/** "3 archivos", "1 línea": a count, with the number in the language's format. */
const count = (key: PluralKey, n: number, vars?: Vars) => tn(key, n, { ...vars, count: formatNumber(n) });

/** What was copied, as a noun phrase: "texto", "una imagen", "texto e imagen". */
export function contentKind(formats: string[]): { kind: ClipboardView["kind"]; phrase: string } {
  const text = formats.includes("text") || formats.includes("HTML");
  const image = formats.includes("bitmap");
  if (text && image) return { kind: "mixed", phrase: t("clipboard.kind.mixed") };
  if (image) return { kind: "image", phrase: t("clipboard.kind.image") };
  if (formats.includes("HTML")) return { kind: "text", phrase: t("clipboard.kind.formatted") };
  if (text) return { kind: "text", phrase: t("clipboard.kind.text") };
  return { kind: "none", phrase: t("clipboard.kind.none") };
}

export function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${Math.round(bytes / 1024)} KB`;
  return `${(bytes / (1024 * 1024)).toLocaleString(locale(), { maximumFractionDigits: 1 })} MB`;
}

/** "«informe.pdf»", "3 archivos", "una carpeta vacía": what a copy of files was. */
export function filesPhrase(files: FilesSummary | null | undefined): string {
  if (!files) return t("clipboard.files.generic");
  if (files.roots === 1 && files.names.length === 1) return t("clipboard.files.named", { name: files.names[0] });
  if (files.count === 0) {
    return files.folders === 1 ? t("clipboard.files.oneEmptyFolder") : count("clipboard.files.emptyFolders", files.folders);
  }
  return count("clipboard.files.count", files.count);
}

/** "5 archivos · 2 carpetas", "fotos, informe.pdf, c.txt y 1 más", "3,4 KB" */
export function filesFacts(files: FilesSummary): string[] {
  const facts: string[] = [];
  const counts = [count("clipboard.files.count", files.count)];
  if (files.folders > 0) counts.push(count("clipboard.files.folders", files.folders));
  facts.push(counts.join(" · "));
  if (files.roots > 1 && files.names.length > 0) {
    const more = files.roots - files.names.length;
    const names = files.names.join(", ");
    facts.push(more > 0 ? count("clipboard.files.more", more, { names }) : names);
  }
  facts.push(formatSize(files.bytes));
  return facts;
}

/** Why a copy of files failed, for people. */
function filesFailure(a: ClipboardActivity, filesLimitMb: number): { detail: string; tone: ClipboardTone } {
  switch (a.reason) {
    case "tooLarge":
      return { tone: "warn", detail: t("clipboard.files.fail.tooLarge", { limit: formatNumber(filesLimitMb) }) };
    case "noUser":
      return { tone: "warn", detail: t("clipboard.files.fail.noUser") };
    case "changed":
      return { tone: "warn", detail: t("clipboard.files.fail.changed") };
    case "damaged":
      return { tone: "danger", detail: t("clipboard.files.fail.damaged") };
    case "diskFull":
      return { tone: "danger", detail: t("clipboard.files.fail.diskFull") };
    case "refused":
      return { tone: "warn", detail: t("clipboard.files.fail.refused") };
    case "superseded":
      return { tone: "warn", detail: t("clipboard.files.fail.superseded") };
    default:
      return {
        tone: "danger",
        detail: a.detail ? t("clipboard.files.fail.detail", { detail: a.detail }) : t("clipboard.files.fail.retry"),
      };
  }
}

function describeFiles(a: ClipboardActivity, mode: CoreMode, other: string | null, filesLimitMb: number): ClipboardView {
  const what = filesPhrase(a.files);
  const facts = a.files ? filesFacts(a.files) : [];
  const view = (tone: ClipboardTone, title: string, detail: string): ClipboardView => ({ tone, title, detail, note: null, facts, kind: "files" });
  switch (a.event) {
    case "sent":
      return other
        ? view("ok", t("clipboard.files.sentTo.title", { files: what, computer: other }), t("clipboard.files.sentTo.detail", { computer: other }))
        : view("ok", t("clipboard.copiedHere", { what }), t("clipboard.files.sentToServer.detail"));
    case "received":
      return view(
        "ok",
        other ? t("clipboard.files.receivedFrom.title", { files: what, computer: other }) : t("clipboard.files.received.title", { files: what }),
        t("clipboard.files.received.detail"),
      );
    case "shared":
      return other
        ? view("ok", t("clipboard.copiedBy", { computer: other, what }), t("clipboard.files.sharedFrom.detail"))
        : view("ok", t("clipboard.copiedHere", { what }), t("clipboard.files.sharedHere.detail"));
    case "skipped":
      return view(
        "warn",
        t("clipboard.files.skipped.title"),
        a.reason === "tooLarge"
          ? t("clipboard.files.skipped.tooLarge", { limit: formatNumber(filesLimitMb) })
          : filesFailure(a, filesLimitMb).detail,
      );
    default: {
      const { tone, detail } = filesFailure(a, filesLimitMb);
      const title = other
        ? mode === "server" && a.files
          ? t("clipboard.files.failedWith.title", { files: what, computer: other })
          : t("clipboard.files.failedWithAll.title", { computer: other })
        : t("clipboard.files.failed.title");
      return view(tone, title, detail);
    }
  }
}

/** A copy of files on its way: "Enviando 3 archivos a laptop", 40 %, "1,2 MB de 3 MB". */
export function describeProgress(p: FileProgress): { title: string; detail: string; percent: number } {
  const files = count("clipboard.files.count", p.count);
  const title =
    p.direction === "send"
      ? p.computer
        ? t("clipboard.progress.sendTo", { files, computer: p.computer })
        : t("clipboard.progress.sendToServer", { files })
      : p.computer
        ? t("clipboard.progress.receiveFrom", { files, computer: p.computer })
        : t("clipboard.progress.receive", { files });
  const percent = p.bytes > 0 ? Math.min(100, Math.floor((p.done * 100) / p.bytes)) : 100;
  return { title, detail: t("clipboard.progress.amount", { done: formatSize(p.done), total: formatSize(p.bytes) }), percent };
}

function droppedPhrase(dropped: string[]): string {
  const names = dropped.map((d) =>
    d === "bitmap" ? t("clipboard.dropped.image") : d === "HTML" ? t("clipboard.dropped.format") : t("clipboard.dropped.text"),
  );
  return names.reduce((first, second) => t("clipboard.dropped.list", { first, second }));
}

/**
 * One activity as a title and a detail line.
 * `self` is this computer's name: on a server, `shared` from itself means it copied here.
 */
export function describeActivity(
  a: ClipboardActivity,
  mode: CoreMode,
  self: string,
  limitMb: number,
  filesLimitMb = 500,
): ClipboardView {
  if (a.formats.includes("files")) {
    return describeFiles(a, mode, a.computer && a.computer !== self ? a.computer : null, filesLimitMb);
  }
  const { kind, phrase: what } = contentKind(a.formats);
  const facts: string[] = [];
  if (a.chars != null && a.chars > 0) {
    const chars = count("clipboard.chars", a.chars);
    facts.push(a.lines && a.lines > 1 ? `${chars} · ${count("clipboard.lines", a.lines)}` : chars);
  }
  if (a.image) facts.push(`${a.image.width} × ${a.image.height}`);
  if (a.size != null) facts.push(formatSize(a.size));

  const note =
    a.dropped.length > 0 && (a.event === "sent" || a.event === "shared")
      ? t("clipboard.note.dropped", { what: droppedPhrase(a.dropped), limit: limitMb })
      : null;
  const other = a.computer && a.computer !== self ? a.computer : null;

  switch (a.event) {
    case "sent":
      return {
        tone: "ok",
        title: t("clipboard.copiedHere", { what }),
        detail: mode === "client" ? t("clipboard.sent.client") : t("clipboard.sent.server"),
        note,
        facts,
        kind,
      };
    case "received":
      return {
        tone: "ok",
        title: other ? t("clipboard.received.from", { what, computer: other }) : t("clipboard.received.unknown", { what }),
        detail: t("clipboard.received.detail"),
        note,
        facts,
        kind,
      };
    case "shared":
      return other
        ? { tone: "ok", title: t("clipboard.copiedBy", { computer: other, what }), detail: t("clipboard.shared.from"), note, facts, kind }
        : { tone: "ok", title: t("clipboard.copiedHere", { what }), detail: t("clipboard.shared.here"), note, facts, kind };
    case "skipped":
      if (a.reason === "tooLarge") {
        const detail = t("clipboard.skipped.tooLarge.detail", { limit: limitMb });
        return {
          tone: "warn",
          title: t("clipboard.skipped.tooLarge.title"),
          detail: mode === "server" ? `${detail} ${t("clipboard.skipped.tooLarge.server")}` : detail,
          note: null,
          facts,
          kind,
        };
      }
      return {
        tone: "warn",
        title: other ? t("clipboard.skipped.from", { computer: other }) : t("clipboard.skipped.here"),
        detail: t("clipboard.skipped.detail"),
        note: null,
        facts,
        kind,
      };
    case "failed":
      return {
        tone: "danger",
        title: t("clipboard.failed.title"),
        detail: t("clipboard.failed.detail"),
        note: null,
        facts,
        kind,
      };
    default:
      return { tone: "ok", title: t("clipboard.other.title"), detail: "", note, facts, kind };
  }
}

/** "hace un momento", "hace 3 min", "hace 2 h" */
export function relativeTime(at: number, now: number): string {
  const seconds = Math.max(0, Math.round((now - at) / 1000));
  if (seconds < 10) return t("clipboard.time.now");
  if (seconds < 60) return t("clipboard.time.seconds", { n: seconds });
  const minutes = Math.round(seconds / 60);
  if (minutes < 60) return t("clipboard.time.minutes", { n: minutes });
  return t("clipboard.time.hours", { n: Math.round(minutes / 60) });
}
