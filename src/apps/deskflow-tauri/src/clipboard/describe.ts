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

/** "«informe.pdf»", "3 archivos", "una carpeta vacía": what a copy of files was. */
export function filesPhrase(files: FilesSummary | null | undefined): string {
  if (!files) return "archivos";
  if (files.roots === 1 && files.names.length === 1) return `«${files.names[0]}»`;
  if (files.count === 0) return files.folders === 1 ? "una carpeta vacía" : plural(files.folders, "carpeta", "carpetas") + " vacías";
  return plural(files.count, "archivo", "archivos");
}

/** "5 archivos · 2 carpetas", "fotos, informe.pdf, c.txt y 1 más", "3,4 KB" */
export function filesFacts(files: FilesSummary): string[] {
  const facts: string[] = [];
  const counts = [plural(files.count, "archivo", "archivos")];
  if (files.folders > 0) counts.push(plural(files.folders, "carpeta", "carpetas"));
  facts.push(counts.join(" · "));
  if (files.roots > 1 && files.names.length > 0) {
    const more = files.roots - files.names.length;
    facts.push(files.names.join(", ") + (more > 0 ? ` y ${more} más` : ""));
  }
  facts.push(formatSize(files.bytes));
  return facts;
}

/** Why a copy of files failed, for people. */
function filesFailure(a: ClipboardActivity, filesLimitMb: number): { detail: string; tone: ClipboardTone } {
  switch (a.reason) {
    case "tooLarge":
      return { tone: "warn", detail: `Pasan el límite de tamaño (acá es de ${filesLimitMb.toLocaleString("es-AR")} MB; la otra computadora tiene el suyo).` };
    case "noUser":
      return { tone: "warn", detail: "En la otra computadora no hay nadie con la sesión iniciada." };
    case "changed":
      return { tone: "warn", detail: "Un archivo cambió o se borró mientras se enviaba. Volvé a copiarlo." };
    case "damaged":
      return { tone: "danger", detail: "Un archivo llegó dañado y se descartó la copia entera. Volvé a copiar." };
    case "diskFull":
      return { tone: "danger", detail: "No hay lugar en el disco para recibirlos." };
    case "refused":
      return { tone: "warn", detail: "La otra computadora no acepta archivos: tiene el portapapeles compartido o la copia de archivos desactivados." };
    case "superseded":
      return { tone: "warn", detail: "Se copió otra cosa mientras tanto." };
    default:
      return { tone: "danger", detail: a.detail ? `Detalle: ${a.detail}` : "Volvé a copiar." };
  }
}

function describeFiles(a: ClipboardActivity, mode: CoreMode, other: string | null, filesLimitMb: number): ClipboardView {
  const phrase = filesPhrase(a.files);
  const facts = a.files ? filesFacts(a.files) : [];
  const view = (tone: ClipboardTone, title: string, detail: string): ClipboardView => ({ tone, title, detail, note: null, facts, kind: "files" });
  switch (a.event) {
    case "sent":
      return other
        ? view("ok", `Se enviaron ${phrase} a ${other}`, `Ya los podés pegar en ${other}.`)
        : view("ok", `Copiaste ${phrase} en esta computadora`, "Se enviaron al servidor; ya los podés pegar en la computadora a la que pases.");
    case "received":
      return view("ok", other ? `Llegaron ${phrase} de ${other}` : `Llegaron ${phrase}`, "Ya los podés pegar acá: Ctrl+V en una carpeta.");
    case "shared":
      return other
        ? view("ok", `${other} copió ${phrase}`, "Llegaron a esta computadora; van a cada una cuando el cursor entra en ella.")
        : view("ok", `Copiaste ${phrase} en esta computadora`, "Van a cada computadora cuando el cursor entra en ella.");
    case "skipped":
      return view(
        "warn",
        "No se compartieron los archivos copiados",
        a.reason === "tooLarge" ? `Pasan el límite de ${filesLimitMb.toLocaleString("es-AR")} MB; se puede cambiar acá abajo.` : filesFailure(a, filesLimitMb).detail,
      );
    default: {
      const { tone, detail } = filesFailure(a, filesLimitMb);
      const title = other
        ? mode === "server" && a.files
          ? `No se pudieron copiar ${phrase} con ${other}`
          : `No se pudieron copiar los archivos con ${other}`
        : "No se pudieron copiar los archivos";
      return view(tone, title, detail);
    }
  }
}

/** A copy of files on its way: "Enviando 3 archivos a laptop", 40 %, "1,2 MB de 3 MB". */
export function describeProgress(p: FileProgress): { title: string; detail: string; percent: number } {
  const phrase = plural(p.count, "archivo", "archivos");
  const title =
    p.direction === "send"
      ? p.computer
        ? `Enviando ${phrase} a ${p.computer}`
        : `Enviando ${phrase} al servidor`
      : p.computer
        ? `Recibiendo ${phrase} de ${p.computer}`
        : `Recibiendo ${phrase}`;
  const percent = p.bytes > 0 ? Math.min(100, Math.floor((p.done * 100) / p.bytes)) : 100;
  return { title, detail: `${formatSize(p.done)} de ${formatSize(p.bytes)}`, percent };
}

function droppedPhrase(dropped: string[]): string {
  const names = dropped.map((d) => (d === "bitmap" ? "la imagen" : d === "HTML" ? "el formato" : "el texto"));
  return names.join(" ni ");
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
        detail: "No tiene texto, imagen ni archivos que Deskflow pueda compartir.",
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
