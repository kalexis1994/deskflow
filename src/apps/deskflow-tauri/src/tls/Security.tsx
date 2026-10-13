import { useCallback, useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { fingerprintArtBody, fingerprintColumns } from "./fingerprint";
import type { CoreMode, FingerprintPrompt as Prompt } from "../types";
import "./Security.css";

interface Trusted {
  kind: string;
  hex: string;
}

export interface TlsInfo {
  enabled: boolean;
  checkPeers: boolean;
  localFingerprint: string | null;
  certificate: string;
  trustedServers: Trusted[];
  trustedClients: Trusted[];
}

/** A fingerprint as the picture (default) or as hex, with a toggle and a copy button. */
function Fingerprint({ hex, title, large }: { hex: string; title?: string; large?: boolean }) {
  const [asText, setAsText] = useState(false);
  const [copied, setCopied] = useState(false);
  const copy = async () => {
    try {
      await navigator.clipboard.writeText(fingerprintColumns(hex).replace(/\n/g, ""));
      setCopied(true);
      setTimeout(() => setCopied(false), 1500);
    } catch {
      // clipboard not available: the text is selectable anyway
    }
  };
  return (
    <figure className={`fp ${large ? "fp--large" : ""}`}>
      {title && <figcaption>{title}</figcaption>}
      <pre className={`fp__art ${asText ? "fp__art--text" : ""}`} aria-label={`Huella ${fingerprintColumns(hex).replace(/\n/g, "")}`}>
        {asText ? fingerprintColumns(hex) : fingerprintArtBody(hex)}
      </pre>
      <div className="fp__tools">
        <button className="le__link" onClick={() => setAsText(!asText)}>
          {asText ? "Ver dibujo" : "Ver como texto"}
        </button>
        <button className="le__link" onClick={copy}>
          {copied ? "Copiada" : "Copiar"}
        </button>
      </div>
    </figure>
  );
}

const PROMPT_TEXT: Record<"client" | "server", { title: string; body: string; accept: string; reject: string }> = {
  client: {
    title: "¿Es esta la computadora a la que te querés conectar?",
    body: "Es la primera vez que te conectás a este servidor. En esa computadora abrí Deskflow y mirá “Mi huella” en Seguridad: si el dibujo es igual a este, es ella.",
    accept: "Es esa, conectar",
    reject: "No, no conectar",
  },
  server: {
    title: "Una computadora nueva quiere conectarse",
    body: "En la computadora que se está conectando abrí Deskflow y mirá “Mi huella” en Seguridad: si el dibujo es igual a este, dejala entrar.",
    accept: "Es mía, dejarla entrar",
    reject: "No dejarla entrar",
  },
};

/** Shown when the core meets a peer whose fingerprint is not trusted yet. */
export function FingerprintPromptCard({ prompt, onDone }: { prompt: Prompt; onDone: () => void }) {
  const [info, setInfo] = useState<TlsInfo | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  useEffect(() => {
    invoke<TlsInfo>("tls_info").then(setInfo, () => setInfo(null));
  }, []);

  const mode = prompt.mode === "server" ? "server" : "client";
  const text = PROMPT_TEXT[mode];

  const run = async (cmd: "trust_fingerprint" | "reject_fingerprint") => {
    setBusy(true);
    setError(null);
    try {
      await invoke(cmd, { fingerprint: prompt.fingerprint });
      onDone();
    } catch (e) {
      setError(String(e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <section className="card fpp" role="alertdialog" aria-labelledby="fpp-title">
      <div className="fpp__head">
        <span className="fpp__icon" aria-hidden="true">
          <svg viewBox="0 0 24 24">
            <path d="M12 3 4 6v6c0 4.5 3.4 8.3 8 9 4.6-.7 8-4.5 8-9V6l-8-3Z" />
            <path d="M12 8v5M12 16h.01" />
          </svg>
        </span>
        <div>
          <h3 id="fpp-title">{text.title}</h3>
          <p className="muted small">{text.body}</p>
        </div>
      </div>
      <div className="fpp__prints">
        <Fingerprint hex={prompt.fingerprint} title={mode === "client" ? "Huella del servidor" : "Huella de la computadora nueva"} large />
        {info?.localFingerprint && <Fingerprint hex={info.localFingerprint} title="Huella de esta computadora (por si la otra te la pide)" />}
      </div>
      {error && <div className="alert alert--danger">{error}</div>}
      <div className="fpp__actions">
        <button className="btn btn--secondary" disabled={busy} onClick={() => run("reject_fingerprint")}>
          {text.reject}
        </button>
        <button className="btn btn--primary" disabled={busy} onClick={() => run("trust_fingerprint")}>
          {text.accept}
        </button>
      </div>
    </section>
  );
}

/** This computer's fingerprint and the computers it already trusts. */
export function SecurityCard({ mode, refreshKey }: { mode: CoreMode; refreshKey: unknown }) {
  const [info, setInfo] = useState<TlsInfo | null>(null);
  const [open, setOpen] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const load = useCallback(() => {
    invoke<TlsInfo>("tls_info").then(setInfo, (e) => setError(String(e)));
  }, []);
  useEffect(load, [load, refreshKey]);

  if (!info) return error ? <div className="alert alert--danger">{error}</div> : null;

  const forget = async (list: "servers" | "clients", t: Trusted) => {
    try {
      setInfo(await invoke<TlsInfo>("forget_fingerprint", { list, kind: t.kind, hex: t.hex }));
    } catch (e) {
      setError(String(e));
    }
  };

  const lists: { id: "servers" | "clients"; title: string; empty: string; items: Trusted[] }[] = [
    { id: "servers", title: "Servidores en los que confío", empty: "Ninguno todavía.", items: info.trustedServers },
    { id: "clients", title: "Computadoras que pueden conectarse a esta", empty: "Ninguna todavía.", items: info.trustedClients },
  ];
  // the list that matters in the current mode goes first
  if (mode === "server") lists.reverse();

  return (
    <section className="card sec">
      <button className="log__toggle" onClick={() => setOpen(!open)} aria-expanded={open}>
        <span className={`chevron ${open ? "chevron--open" : ""}`} aria-hidden="true" />
        Seguridad
        <span className={`badge ${info.enabled ? "badge--ok" : ""}`}>{info.enabled ? "Cifrado activado" : "Sin cifrado"}</span>
      </button>
      {open && (
        <div className="sec__body">
          <div>
            <h4>Mi huella</h4>
            <p className="muted small">Cuando otra computadora se conecte por primera vez, te va a mostrar este dibujo para que confirmes que sos vos.</p>
            {info.localFingerprint ? (
              <Fingerprint hex={info.localFingerprint} />
            ) : (
              <p className="le__warn">No encontré el certificado de esta computadora ({info.certificate}). Se crea la primera vez que Deskflow arranca con cifrado.</p>
            )}
          </div>
          <div className="sec__lists">
            {lists.map((l) => (
              <div key={l.id}>
                <h4>{l.title}</h4>
                {l.items.length === 0 ? (
                  <p className="muted small">{l.empty}</p>
                ) : (
                  <ul className="sec__trusted">
                    {l.items.map((t) => (
                      <li key={t.kind + t.hex}>
                        <pre className="sec__mini" aria-hidden="true">
                          {t.kind === "sha256" ? fingerprintArtBody(t.hex) : ""}
                        </pre>
                        <div className="sec__meta">
                          <code title={t.hex}>{fingerprintColumns(t.hex).split("\n")[0]}…</code>
                          {t.kind !== "sha256" && <span className="muted small">formato viejo (SHA-1)</span>}
                          <button className="le__link sec__forget" onClick={() => forget(l.id, t)}>
                            Olvidar
                          </button>
                        </div>
                      </li>
                    ))}
                  </ul>
                )}
              </div>
            ))}
          </div>
          {!info.checkPeers && mode === "server" && (
            <p className="le__warn">Este servidor no verifica las huellas de las computadoras que se conectan (opción de la app clásica).</p>
          )}
        </div>
      )}
    </section>
  );
}
