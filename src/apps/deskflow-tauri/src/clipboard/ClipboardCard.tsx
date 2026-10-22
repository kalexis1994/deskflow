import { useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import type { CoreMode } from "../types";
import { describeActivity, relativeTime, type ClipboardActivity, type ClipboardPrefs } from "./describe";
import "./ClipboardCard.css";

interface Props {
  activity: ClipboardActivity[];
  mode: CoreMode;
  /** This computer's name, to tell its own copies from other computers'. */
  self: string;
  running: boolean;
}

/** Recent shared-clipboard activity: what went where, and why something didn't. */
export function ClipboardCard({ activity, mode, self, running }: Props) {
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

  const limitMb = prefs?.limitMb ?? 3;
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
                : `Texto e imágenes de hasta ${limitMb} MB · se cambia en Opciones del servidor.`
              : "El servidor decide si se comparte y hasta qué tamaño."}
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

      {latest ? (
        <Activity a={latest} mode={mode} self={self} limitMb={limitMb} now={now} preview={prefs?.preview ?? true} />
      ) : (
        <p className="clip__empty muted">
          {running
            ? "Todavía no pasó nada por el portapapeles. Copiá algo en cualquiera de las computadoras."
            : "Cuando Deskflow esté funcionando, acá vas a ver lo que se copia entre las computadoras."}
        </p>
      )}

      {older.length > 0 && (
        <details className="clip__older">
          <summary>Anteriores ({older.length})</summary>
          <ul>
            {older.map((a) => {
              const v = describeActivity(a, mode, self, limitMb);
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
    </section>
  );
}

function Activity({
  a,
  mode,
  self,
  limitMb,
  now,
  preview,
}: {
  a: ClipboardActivity;
  mode: CoreMode;
  self: string;
  limitMb: number;
  now: number;
  preview: boolean;
}) {
  const v = describeActivity(a, mode, self, limitMb);
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

function KindIcon({ kind }: { kind: "text" | "image" | "mixed" | "none" }) {
  return (
    <span className={`clip__icon clip__icon--${kind}`} aria-hidden="true">
      {kind === "image" ? (
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
