import { useCallback, useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { fingerprintArtBody, fingerprintColumns } from "./fingerprint";
import { t, type Key } from "../i18n";
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
  /** Names people gave the trusted computers, by fingerprint. */
  names: Record<string, string>;
}

/** Longest name the Rust side keeps (`tls::MAX_NAME`). */
const MAX_NAME = 64;

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
      <pre className={`fp__art ${asText ? "fp__art--text" : ""}`} aria-label={t("security.fingerprint.label", { hex: fingerprintColumns(hex).replace(/\n/g, "") })}>
        {asText ? fingerprintColumns(hex) : fingerprintArtBody(hex)}
      </pre>
      <div className="fp__tools">
        <button className="le__link" onClick={() => setAsText(!asText)}>
          {asText ? t("security.fingerprint.showArt") : t("security.fingerprint.showText")}
        </button>
        <button className="le__link" onClick={copy}>
          {copied ? t("security.fingerprint.copied") : t("security.fingerprint.copy")}
        </button>
      </div>
    </figure>
  );
}

const PROMPT_TEXT: Record<"client" | "server", { title: Key; body: Key; accept: Key; reject: Key; print: Key }> = {
  client: {
    title: "security.prompt.client.title",
    body: "security.prompt.client.body",
    accept: "security.prompt.client.accept",
    reject: "security.prompt.client.reject",
    print: "security.prompt.client.print",
  },
  server: {
    title: "security.prompt.server.title",
    body: "security.prompt.server.body",
    accept: "security.prompt.server.accept",
    reject: "security.prompt.server.reject",
    print: "security.prompt.server.print",
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
          <h3 id="fpp-title">{t(text.title)}</h3>
          <p className="muted small">{t(text.body, { mine: t("security.mine.title"), section: t("common.tab.security") })}</p>
        </div>
      </div>
      <div className="fpp__prints">
        <Fingerprint hex={prompt.fingerprint} title={t(text.print)} large />
        {info?.localFingerprint && <Fingerprint hex={info.localFingerprint} title={t("security.prompt.localPrint")} />}
      </div>
      {error && <div className="alert alert--danger">{error}</div>}
      <div className="fpp__actions">
        <button className="btn btn--secondary" disabled={busy} onClick={() => run("reject_fingerprint")}>
          {t(text.reject)}
        </button>
        <button className="btn btn--primary" disabled={busy} onClick={() => run("trust_fingerprint")}>
          {t(text.accept)}
        </button>
      </div>
    </section>
  );
}

/** This computer's fingerprint and the computers it already trusts. */
export function SecurityCard({ mode, refreshKey }: { mode: CoreMode; refreshKey: unknown }) {
  const [info, setInfo] = useState<TlsInfo | null>(null);
  const [error, setError] = useState<string | null>(null);

  const load = useCallback(() => {
    invoke<TlsInfo>("tls_info").then(setInfo, (e) => setError(String(e)));
  }, []);
  useEffect(load, [load, refreshKey]);
  // a computer that connects gets a name by itself
  useEffect(() => {
    const unlisten = listen("tls-changed", load);
    return () => {
      unlisten.then((f) => f());
    };
  }, [load]);

  if (!info) return error ? <div className="alert alert--danger">{error}</div> : null;

  const forget = async (list: "servers" | "clients", item: Trusted) => {
    try {
      setInfo(await invoke<TlsInfo>("forget_fingerprint", { list, kind: item.kind, hex: item.hex }));
    } catch (e) {
      setError(String(e));
    }
  };

  const rename = async (item: Trusted, name: string) => {
    try {
      setInfo(await invoke<TlsInfo>("rename_trusted", { hex: item.hex, name }));
    } catch (e) {
      setError(String(e));
    }
  };

  const lists: { id: "servers" | "clients"; title: string; empty: string; items: Trusted[] }[] = [
    { id: "servers", title: t("security.trustedServers.title"), empty: t("security.trustedServers.empty"), items: info.trustedServers },
    { id: "clients", title: t("security.trustedClients.title"), empty: t("security.trustedClients.empty"), items: info.trustedClients },
  ];
  // the list that matters in the current mode goes first
  if (mode === "server") lists.reverse();

  return (
    <section className="card sec">
      <h3 className="sec__title">
        {t("security.title")}
        <span className={`badge ${info.enabled ? "badge--ok" : ""}`}>{info.enabled ? t("security.encryption.on") : t("security.encryption.off")}</span>
      </h3>
      <div className="sec__body">
        <div>
          <h4>{t("security.mine.title")}</h4>
          <p className="muted small">{t("security.mine.hint")}</p>
          {info.localFingerprint ? (
            <Fingerprint hex={info.localFingerprint} />
          ) : (
            <p className="le__warn">{t("security.mine.missing", { path: info.certificate })}</p>
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
                  {l.items.map((item) => (
                    <li key={item.kind + item.hex}>
                      <pre className="sec__mini" aria-hidden="true">
                        {item.kind === "sha256" ? fingerprintArtBody(item.hex) : ""}
                      </pre>
                      <TrustedMeta
                        item={item}
                        name={info.names?.[item.hex] ?? ""}
                        onRename={(name) => rename(item, name)}
                        onForget={() => forget(l.id, item)}
                      />
                    </li>
                  ))}
                </ul>
              )}
            </div>
          ))}
        </div>
        {!info.checkPeers && mode === "server" && (
          <p className="le__warn">{t("security.noPeerCheck")}</p>
        )}
      </div>
    </section>
  );
}

/** A trusted computer's name, fingerprint and what can be done with it. */
function TrustedMeta({ item, name, onRename, onForget }: { item: Trusted; name: string; onRename: (name: string) => void; onForget: () => void }) {
  const [editing, setEditing] = useState(false);
  const [draft, setDraft] = useState(name);

  if (editing) {
    return (
      <form
        className="sec__meta sec__rename"
        onSubmit={(e) => {
          e.preventDefault();
          onRename(draft);
          setEditing(false);
        }}
      >
        <input
          autoFocus
          value={draft}
          maxLength={MAX_NAME}
          aria-label={t("security.name.label")}
          placeholder={t("security.name.placeholder")}
          onChange={(e) => setDraft(e.target.value)}
          onKeyDown={(e) => e.key === "Escape" && setEditing(false)}
        />
        <span className="muted small">{t("security.name.hint")}</span>
        <span className="sec__actions">
          <button className="btn btn--primary" type="submit">
            {t("common.save")}
          </button>
          <button className="le__link" type="button" onClick={() => setEditing(false)}>
            {t("common.cancel")}
          </button>
        </span>
      </form>
    );
  }

  return (
    <div className="sec__meta">
      {name ? <strong className="sec__name">{name}</strong> : <span className="muted small">{t("security.unnamed")}</span>}
      <code title={item.hex}>{fingerprintColumns(item.hex).split("\n")[0]}…</code>
      {item.kind !== "sha256" && <span className="muted small">{t("security.oldFormat")}</span>}
      <span className="sec__actions">
        <button
          className="le__link"
          onClick={() => {
            setDraft(name);
            setEditing(true);
          }}
        >
          {t("security.rename")}
        </button>
        <button className="le__link sec__forget" onClick={onForget}>
          {t("security.forget")}
        </button>
      </span>
    </div>
  );
}
