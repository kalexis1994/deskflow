// Turns the core's clipboard reports (src-tauri/src/clipboard.rs) into sentences.

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
  /** Unix ms when it reached the app. */
  at: number;
}

// Mirrors ClipboardPrefs in src-tauri/src/clipboard.rs
export interface ClipboardPrefs {
  preview: boolean;
  sharing: boolean;
  limitMb: number;
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
  kind: "text" | "image" | "mixed" | "none";
}

/** What was copied, as a noun phrase: "texto", "una imagen", "texto e imagen". */
export function contentKind(formats: string[]): { kind: ClipboardView["kind"]; phrase: string } {
  const text = formats.includes("text") || formats.includes("HTML");
  const image = formats.includes("bitmap");
  if (text && image) return { kind: "mixed", phrase: "texto e imagen" };
  if (image) return { kind: "image", phrase: "una imagen" };
  if (formats.includes("HTML")) return { kind: "text", phrase: "texto con formato" };
  if (text) return { kind: "text", phrase: "texto" };
  return { kind: "none", phrase: "algo" };
}

export function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${Math.round(bytes / 1024)} KB`;
  return `${(bytes / (1024 * 1024)).toLocaleString("es-AR", { maximumFractionDigits: 1 })} MB`;
}

const plural = (n: number, one: string, many: string) => `${n.toLocaleString("es-AR")} ${n === 1 ? one : many}`;

function droppedPhrase(dropped: string[]): string {
  const names = dropped.map((d) => (d === "bitmap" ? "la imagen" : d === "HTML" ? "el formato" : "el texto"));
  return names.join(" ni ");
}

/**
 * One activity as a title and a detail line.
 * `self` is this computer's name: on a server, `shared` from itself means it copied here.
 */
export function describeActivity(a: ClipboardActivity, mode: CoreMode, self: string, limitMb: number): ClipboardView {
  const { kind, phrase } = contentKind(a.formats);
  const facts: string[] = [];
  if (a.chars != null && a.chars > 0) {
    facts.push(a.lines && a.lines > 1 ? `${plural(a.chars, "carácter", "caracteres")} · ${plural(a.lines, "línea", "líneas")}` : plural(a.chars, "carácter", "caracteres"));
  }
  if (a.image) facts.push(`${a.image.width} × ${a.image.height}`);
  if (a.size != null) facts.push(formatSize(a.size));

  const note =
    a.dropped.length > 0 && (a.event === "sent" || a.event === "shared")
      ? `Se compartió sin ${droppedPhrase(a.dropped)}: no entraba en el límite de ${limitMb} MB.`
      : null;
  const other = a.computer && a.computer !== self ? a.computer : null;

  switch (a.event) {
    case "sent":
      return {
        tone: "ok",
        title: `Copiaste ${phrase} en esta computadora`,
        detail: mode === "client" ? "Se envió al servidor; ya lo podés pegar en las otras." : "Se envió a las otras computadoras.",
        note,
        facts,
        kind,
      };
    case "received":
      return {
        tone: "ok",
        title: other ? `Llegó ${phrase} de ${other}` : `Llegó ${phrase} de otra computadora`,
        detail: "Ya lo podés pegar acá.",
        note,
        facts,
        kind,
      };
    case "shared":
      return other
        ? { tone: "ok", title: `${other} copió ${phrase}`, detail: "Ya lo podés pegar acá y en las demás.", note, facts, kind }
        : { tone: "ok", title: `Copiaste ${phrase} en esta computadora`, detail: "Se compartió con las demás.", note, facts, kind };
    case "skipped":
      if (a.reason === "tooLarge") {
        return {
          tone: "warn",
          title: "No se compartió: es demasiado grande",
          detail: `Ni siquiera el texto entra en el límite de ${limitMb} MB.${mode === "server" ? " Podés subirlo en Opciones del servidor." : ""}`,
          note: null,
          facts,
          kind,
        };
      }
      return {
        tone: "warn",
        title: other ? `No se compartió lo que copió ${other}` : "No se compartió lo que copiaste",
        detail: "No tiene texto ni imagen. Deskflow todavía no comparte archivos.",
        note: null,
        facts,
        kind,
      };
    case "failed":
      return {
        tone: "danger",
        title: "Otro programa tenía el portapapeles ocupado",
        detail: "Deskflow lo intentó durante 3 segundos y no pudo. Volvé a copiar.",
        note: null,
        facts,
        kind,
      };
    default:
      return { tone: "ok", title: "Actividad del portapapeles", detail: "", note, facts, kind };
  }
}

/** "hace un momento", "hace 3 min", "hace 2 h" */
export function relativeTime(at: number, now: number): string {
  const seconds = Math.max(0, Math.round((now - at) / 1000));
  if (seconds < 10) return "hace un momento";
  if (seconds < 60) return `hace ${seconds} s`;
  const minutes = Math.round(seconds / 60);
  if (minutes < 60) return `hace ${minutes} min`;
  return `hace ${Math.round(minutes / 60)} h`;
}
