import { useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import type { CoreMode } from "../types";
import {
  describeActivity,
  describeProgress,
  relativeTime,
  type ClipboardActivity,
  type ClipboardPrefs,
  type ClipboardView,
  type FileProgress,
} from "./describe";
import "./ClipboardCard.css";

interface Props {
  activity: ClipboardActivity[];
  /** A copy of files on its way. */
  progress: FileProgress | null;
  mode: CoreMode;
  /** This computer's name, to tell its own copies from other computers'. */
  self: string;
  running: boolean;
}

/** Recent shared-clipboard activity: what went where, and why something didn't. */
export function ClipboardCard({ activity, progress, mode, self, running }: Props) {
  const [prefs, setPrefs] = useState<ClipboardPrefs | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [now, setNow] = useState(Date.now());

  useEffect(() => {
    invoke<ClipboardPrefs>("clipboard_prefs").then(setPrefs, (e) => setError(String(e)));
  }, []);

  // keep "hace 5 s" current
  useEffect(() => {
    if (activity.length === 0) return;
    const id = setInterval(() => setNow(Date.now()), 5000);
    return () => clearInterval(id);
  }, [activity.length]);

  const togglePreview = async () => {
    if (!prefs) return;
    try {
      setPrefs(await invoke<ClipboardPrefs>("set_clipboard_preview", { enabled: !prefs.preview }));
    } catch (e) {
      setError(String(e));
    }
  };

  const setFiles = async (enabled: boolean, limitMb: number) => {
    try {
      setPrefs(await invoke<ClipboardPrefs>("set_file_sharing", { enabled, limitMb }));
      setError(null);
    } catch (e) {
      setError(String(e));
    }
  };

  const limitMb = prefs?.limitMb ?? 3;
  const filesLimitMb = prefs?.filesLimitMb ?? 500;
  const [latest, ...older] = activity;

  return (
    <section className="card clip" aria-labelledby="clip-title">
      <div className="clip__head">
        <div>
          <h3 id="clip-title">Portapapeles compartido</h3>
          <p className="muted small">
            {mode === "server"
              ? prefs && !prefs.sharing
                ? "Está desactivado en las opciones del servidor."
                : `Texto e imágenes de hasta ${limitMb} MB · se cambia en Opciones del servidor.${
                    prefs?.files ? ` Archivos de hasta ${filesLimitMb.toLocaleString("es-AR")} MB.` : ""
                  }`
              : `El servidor decide si se comparte y hasta qué tamaño.${
                  prefs?.files ? ` Archivos de hasta ${filesLimitMb.toLocaleString("es-AR")} MB.` : ""
                }`}
          </p>
        </div>
        {prefs && (
          <button
            className={`clip__toggle ${prefs.preview ? "clip__toggle--on" : ""}`}
            role="switch"
            aria-checked={prefs.preview}
            onClick={togglePreview}
            title="Muestra el comienzo del texto copiado. Solo se ve en esta ventana y no se guarda."
          >
            <span className="clip__switch" aria-hidden="true" />
            <span>Mostrar el texto</span>
          </button>
        )}
      </div>

      {error && <div className="alert alert--danger">{error}</div>}

      {progress && <Progress p={progress} />}

      {latest ? (
        <Activity
          a={latest}
          mode={mode}
          self={self}
          limitMb={limitMb}
          filesLimitMb={filesLimitMb}
          now={now}
          preview={prefs?.preview ?? true}
        />
      ) : (
        <p className="clip__empty muted">
          {running
            ? "Todavía no pasó nada por el portapapeles. Copiá texto, una imagen o archivos en cualquiera de las computadoras."
            : "Cuando Deskflow esté funcionando, acá vas a ver lo que se copia entre las computadoras."}
        </p>
      )}

      {older.length > 0 && (
        <details className="clip__older">
          <summary>Anteriores ({older.length})</summary>
          <ul>
            {older.map((a) => {
              const v = describeActivity(a, mode, self, limitMb, filesLimitMb);
              return (
                <li key={a.at} className={`clip__item clip__item--${v.tone}`}>
                  <KindIcon kind={v.kind} />
                  <span className="clip__item-title">{v.title}</span>
                  <span className="muted small">{relativeTime(a.at, now)}</span>
                </li>
              );
            })}
          </ul>
        </details>
      )}

      {prefs && <FilesSettings prefs={prefs} onChange={setFiles} />}
    </section>
  );
}

function Progress({ p }: { p: FileProgress }) {
  const v = describeProgress(p);
  return (
    <div className="clip__progress" aria-live="polite">
      <div className="clip__summary">
        <KindIcon kind="files" />
        <div className="clip__body">
          <div className="clip__title">
            <strong>{v.title}</strong>
            <span className="muted small">{v.percent} %</span>
          </div>
          <div
            className="clip__bar"
            role="progressbar"
            aria-label={v.title}
            aria-valuemin={0}
            aria-valuemax={100}
            aria-valuenow={v.percent}
          >
            <span style={{ width: `${v.percent}%` }} />
          </div>
          <p className="muted small">{v.detail} · podés seguir usando el teclado y el mouse.</p>
        </div>
      </div>
    </div>
  );
}

/** Copying files: on or off and up to what size, decided on each computer. */
function FilesSettings({ prefs, onChange }: { prefs: ClipboardPrefs; onChange: (enabled: boolean, limitMb: number) => void }) {
  const [limit, setLimit] = useState(String(prefs.filesLimitMb));
  useEffect(() => setLimit(String(prefs.filesLimitMb)), [prefs.filesLimitMb]);

  const commit = () => {
    const mb = Number.parseInt(limit, 10);
    if (Number.isFinite(mb) && mb !== prefs.filesLimitMb) onChange(prefs.files, mb);
    else setLimit(String(prefs.filesLimitMb));
  };

  return (
    <div className="clip__files">
      <button
        className={`clip__toggle ${prefs.files ? "clip__toggle--on" : ""}`}
        role="switch"
        aria-checked={prefs.files}
        onClick={() => onChange(!prefs.files, prefs.filesLimitMb)}
      >
        <span className="clip__switch" aria-hidden="true" />
        <span>Copiar archivos</span>
      </button>
      <label className={`clip__limit small ${prefs.files ? "" : "muted"}`}>
        hasta
        <input
          type="number"
          min={1}
          max={100000}
          value={limit}
          disabled={!prefs.files}
          onChange={(e) => setLimit(e.target.value)}
          onBlur={commit}
          onKeyDown={(e) => e.key === "Enter" && commit()}
          aria-label="Tamaño máximo de una copia de archivos, en MB"
        />
        MB por copia
      </label>
      <p className="muted small clip__files-note">
        Lo decide cada computadora, para lo que manda y lo que recibe. Se aplica la próxima vez que arranque Deskflow.
      </p>
    </div>
  );
}

function Activity({
  a,
  mode,
  self,
  limitMb,
  filesLimitMb,
  now,
  preview,
}: {
  a: ClipboardActivity;
  mode: CoreMode;
  self: string;
  limitMb: number;
  filesLimitMb: number;
  now: number;
  preview: boolean;
}) {
  const v = describeActivity(a, mode, self, limitMb, filesLimitMb);
  const showsContent = a.event === "sent" || a.event === "received" || a.event === "shared";
  return (
    <div className={`clip__latest clip__latest--${v.tone}`} aria-live="polite">
      <div className="clip__summary">
        <KindIcon kind={v.kind} />
        <div className="clip__body">
          <div className="clip__title">
            <strong>{v.title}</strong>
            <span className="muted small">{relativeTime(a.at, now)}</span>
          </div>
          {v.detail && <p className="muted small">{v.detail}</p>}
        </div>
      </div>

      {showsContent && a.text != null && preview && (
        <div className="clip__preview">
          <pre>
            {a.text}
            {a.chars != null && a.chars > [...a.text].length ? "…" : ""}
          </pre>
        </div>
      )}
      {showsContent && (a.text == null || !preview) && a.chars != null && a.chars > 0 && (
        <p className="clip__hidden muted small">Texto oculto: la vista previa está desactivada.</p>
      )}

      {v.facts.length > 0 && (
        <div className="clip__facts">
          {v.facts.map((f) => (
            <span key={f} className="tag">
              {f}
            </span>
          ))}
        </div>
      )}
      {v.note && <p className="clip__note small">{v.note}</p>}
    </div>
  );
}

function KindIcon({ kind }: { kind: ClipboardView["kind"] }) {
  return (
    <span className={`clip__icon clip__icon--${kind}`} aria-hidden="true">
      {kind === "files" ? (
        <svg viewBox="0 0 24 24" width="18" height="18">
          <path
            d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"
            fill="none"
            stroke="currentColor"
            strokeWidth="1.8"
            strokeLinejoin="round"
          />
        </svg>
      ) : kind === "image" ? (
        <svg viewBox="0 0 24 24" width="18" height="18">
          <rect x="3" y="5" width="18" height="14" rx="2" fill="none" stroke="currentColor" strokeWidth="1.8" />
          <circle cx="9" cy="10" r="1.6" fill="currentColor" />
          <path d="M4 17l5-5 4 4 3-3 4 4" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinejoin="round" />
        </svg>
      ) : kind === "none" ? (
        <svg viewBox="0 0 24 24" width="18" height="18">
          <circle cx="12" cy="12" r="8" fill="none" stroke="currentColor" strokeWidth="1.8" />
          <path d="M8 16L16 8" stroke="currentColor" strokeWidth="1.8" />
        </svg>
      ) : (
        <svg viewBox="0 0 24 24" width="18" height="18">
          <rect x="6" y="3" width="12" height="18" rx="2" fill="none" stroke="currentColor" strokeWidth="1.8" />
          <path d="M9 8h6M9 12h6M9 16h4" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" />
        </svg>
      )}
    </span>
  );
}
