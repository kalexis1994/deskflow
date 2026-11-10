import { useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import "./App.css";
import { useCountdown, useDeskflow } from "./useDeskflow";
import { describeRefusal, describeStatus, parseLogLine } from "./status";
import { LayoutEditor } from "./layout/LayoutEditor";
import { ServerOptionsCard } from "./ServerOptionsCard";
import { HotkeysCard } from "./hotkeys/HotkeysCard";
import { FingerprintPromptCard, SecurityCard } from "./tls/Security";
import { ClipboardCard } from "./clipboard/ClipboardCard";
import { UpdateCard, UpdateFooter } from "./update/UpdateCard";
import { useUpdate } from "./update/useUpdate";
import { recentProblems, saveTab, savedTab, shownTab, tabsFor, type Tab, type TabId } from "./tabs";
import type { Autostart, CoreMode, CoreState, Overview } from "./types";

export default function App() {
  const df = useDeskflow();
  const update = useUpdate();
  const retryIn = useCountdown(df.core?.retryAt ?? null);
  const [layoutVersion, setLayoutVersion] = useState(0);
  const [chosenTab, setChosenTab] = useState(savedTab);
  const log = useMemo(() => df.log.map(parseLogLine), [df.log]);

  if (!df.info || !df.core) {
    return (
      <main className="app app--loading">
        {df.error ? <div className="alert alert--danger">{df.error}</div> : <div className="spinner" aria-label="Cargando" />}
      </main>
    );
  }

  const { overview: ov } = df.info;
  const core = df.core;
  const running = core.process !== "stopped";
  const tabs = tabsFor(ov.coreMode);
  const tab = shownTab(chosenTab, tabs);
  const problems = recentProblems(log);

  return (
    <main className="app">
      <Header ov={ov} core={core} />

      {!df.info.coreFound && (
        <div className="alert alert--danger">
          No encontré <code>deskflow-core</code>. Instalá Deskflow o indicá la carpeta con la variable <code>DESKFLOW_DIR</code>.
        </div>
      )}

      {update.state && <UpdateCard state={update.state} mode={ov.coreMode} service={ov.processMode === "service"} onInstall={update.install} onDismiss={update.dismiss} />}

      {core.fingerprintPrompt && <FingerprintPromptCard key={core.fingerprintPrompt.fingerprint} prompt={core.fingerprintPrompt} onDone={() => {}} />}

      <Alerts core={core} ov={ov} error={df.error} onDismissError={df.dismissError} />

      <Tabs
        tabs={tabs}
        current={tab}
        badges={{ activity: problems }}
        onChange={(id) => {
          setChosenTab(id);
          saveTab(id);
        }}
      />

      {/* every panel stays mounted, so what is being edited in one survives a look at another */}
      <Panel id="home" current={tab}>
        <ModePicker mode={ov.coreMode} disabled={df.busy} onChange={(coreMode) => df.save({ coreMode })} />
        <StatusCard
          core={core}
          ov={ov}
          retryIn={retryIn}
          address={df.address}
          busy={df.busy}
          onStart={df.start}
          onStop={df.stop}
          autostart={df.autostart}
        />
        {ov.coreMode === "client" && (
          <ClientCard host={ov.remoteHost} port={ov.port} running={running} busy={df.busy} onSave={(remoteHost) => df.save({ remoteHost })} />
        )}
      </Panel>

      {ov.coreMode === "server" && (
        <>
          <Panel id="layout" current={tab}>
            <LayoutEditor
              core={core}
              address={df.address}
              port={ov.port}
              onSaved={() => {
                df.refresh();
                setLayoutVersion((v) => v + 1);
              }}
            />
          </Panel>
          <Panel id="hotkeys" current={tab}>
            <HotkeysCard running={core.process === "running"} layoutVersion={layoutVersion} />
          </Panel>
        </>
      )}

      {ov.coreMode !== "none" && (
        <Panel id="clipboard" current={tab}>
          <ClipboardCard activity={core.clipboard ?? []} progress={core.fileProgress ?? null} mode={ov.coreMode} self={ov.computerName} running={core.process === "running"} />
        </Panel>
      )}

      {ov.coreMode === "server" && (
        <Panel id="options" current={tab}>
          <ServerOptionsCard running={core.process === "running"} />
        </Panel>
      )}

      <Panel id="security" current={tab}>
        <SecurityCard mode={ov.coreMode} refreshKey={core.fingerprintPrompt?.fingerprint ?? ""} />
      </Panel>

      <Panel id="activity" current={tab}>
        <ActivityLog lines={log} visible={tab === "activity"} />
      </Panel>

      <footer className="footer">
        <span>{ov.processMode === "service" ? "Corre como servicio del sistema" : "Corre dentro de esta app"}</span>
        {update.state && <UpdateFooter state={update.state} onCheck={update.check} onChannel={update.setChannel} />}
        <span className="footer__path" title={ov.settingsFile}>
          {ov.settingsFile}
        </span>
      </footer>
    </main>
  );
}

function Header({ ov, core }: { ov: Overview; core: CoreState }) {
  const ipcLabel = { connected: "Core conectado", versionMismatch: "Core de otra versión", disconnected: "Core apagado" }[core.ipc];
  return (
    <header className="header">
      <Logo />
      <div className="header__titles">
        <h1>Deskflow</h1>
        <p>
          Esta computadora: <strong>{ov.computerName}</strong>
        </p>
      </div>
      <div className="header__meta">
        <span className={`pill pill--${core.ipc}`} title="Conexión de esta app con el proceso de Deskflow">
          <span className="dot" /> {ipcLabel}
        </span>
        {core.coreVersion && <span className="muted small">v{core.coreVersion}</span>}
      </div>
    </header>
  );
}

function Logo() {
  return (
    <svg className="logo" viewBox="0 0 40 40" aria-hidden="true">
      <rect x="2" y="9" width="17" height="13" rx="2.5" />
      <rect x="21" y="9" width="17" height="13" rx="2.5" />
      <path d="M8 27h24M14 31h12" />
      <path className="logo__arrow" d="M15 15.5h10m-3-3 3 3-3 3" />
    </svg>
  );
}

const MODES: { value: CoreMode; title: string; text: string; icon: string }[] = [
  {
    value: "server",
    title: "Compartir mi teclado y mouse",
    text: "Esta computadora controla a las demás.",
    icon: "M4 6h16v9H4zM9 19h6M12 15v4",
  },
  {
    value: "client",
    title: "Usar el teclado de otra",
    text: "Esta computadora se controla desde otra.",
    icon: "M5 4h14v10H5zM2 20h20M12 14v3",
  },
];

function ModePicker({ mode, disabled, onChange }: { mode: CoreMode; disabled: boolean; onChange: (m: CoreMode) => void }) {
  return (
    <section className="modes" role="radiogroup" aria-label="Qué hace esta computadora">
      {MODES.map((m) => (
        <button
          key={m.value}
          role="radio"
          aria-checked={mode === m.value}
          className={`mode ${mode === m.value ? "mode--active" : ""}`}
          disabled={disabled}
          onClick={() => mode !== m.value && onChange(m.value)}
        >
          <svg viewBox="0 0 24 24" className="mode__icon" aria-hidden="true">
            <path d={m.icon} />
          </svg>
          <span className="mode__title">{m.title}</span>
          <span className="mode__text">{m.text}</span>
        </button>
      ))}
    </section>
  );
}

interface StatusCardProps {
  core: CoreState;
  ov: Overview;
  retryIn: number | null;
  address: string | null;
  busy: boolean;
  onStart: () => void;
  onStop: () => void;
  autostart: Autostart | null;
}

function StatusCard({ core, ov, retryIn, address, busy, onStart, onStop, autostart }: StatusCardProps) {
  const view = describeStatus(core, ov, retryIn, address);
  // stopping through the service also stops it from starting on boot, so say so first
  const [confirmStop, setConfirmStop] = useState(false);
  useEffect(() => setConfirmStop(false), [core.process]);
  const running = core.process === "running" || core.process === "starting";
  const transitioning = core.process === "starting" || core.process === "stopping";

  return (
    <section className={`status status--${view.tone}`} aria-live="polite">
      <div className="status__indicator" aria-hidden="true">
        <span />
      </div>
      <div className="status__body">
        <h2>
          {view.title}
          {core.connectionInferred && (
            <span className="tag" title="Esta app se conectó a un Deskflow que ya estaba corriendo; el estado se deduce del registro hasta que cambie.">
              estimado
            </span>
          )}
        </h2>
        <p>{view.detail}</p>
        {core.process === "running" && (
          <p className="status__security">
            {core.tls ? (
              <>
                <LockIcon /> Conexión cifrada · {core.tls}
              </>
            ) : ov.tlsEnabled ? (
              <>
                <LockIcon /> Cifrado activado
              </>
            ) : (
              <span className="warn-text">Sin cifrado</span>
            )}
          </p>
        )}
        {autostart?.supported && (
          <p className={`status__autostart ${autostart.enabled ? "" : "status__autostart--off"}`}>
            {autostart.enabled
              ? "Arranca solo al prender la PC, ya en la pantalla de inicio de sesión."
              : "No arranca solo al prender la PC: iniciá Deskflow una vez para que lo recuerde."}
          </p>
        )}
        {confirmStop && (
          <p className="status__confirm">
            Si lo detenés, tampoco va a arrancar solo la próxima vez que prendas la PC.
          </p>
        )}
      </div>
      <div className="status__actions">
        {running ? (
          confirmStop ? (
            <div className="status__confirm-actions">
              <button className="btn btn--secondary" onClick={() => setConfirmStop(false)}>
                Cancelar
              </button>
              <button className="btn btn--danger" onClick={onStop} disabled={busy || transitioning}>
                Detener igual
              </button>
            </div>
          ) : (
            <button
              className="btn btn--secondary"
              onClick={() => (autostart?.enabled ? setConfirmStop(true) : onStop())}
              disabled={busy || transitioning}
            >
              Detener
            </button>
          )
        ) : (
          <button className="btn btn--primary" onClick={onStart} disabled={busy || ov.coreMode === "none"}>
            Iniciar
          </button>
        )}
      </div>
    </section>
  );
}

function LockIcon() {
  return (
    <svg viewBox="0 0 16 16" className="icon-inline" aria-hidden="true">
      <rect x="3" y="7" width="10" height="7" rx="1.5" />
      <path d="M5.5 7V5a2.5 2.5 0 0 1 5 0v2" />
    </svg>
  );
}

function Alerts({ core, ov, error, onDismissError }: { core: CoreState; ov: Overview; error: string | null; onDismissError: () => void }) {
  const items: { tone: "danger" | "warn" | "info"; text: ReactNode; onClose?: () => void }[] = [];
  if (error) items.push({ tone: "danger", text: error, onClose: onDismissError });
  else if (core.lastError) items.push({ tone: "danger", text: core.lastError });
  if (core.refusal && core.connection !== "Connected") items.push({ tone: "warn", text: describeRefusal(core.refusal, ov.computerName) });
  if (core.missingLayouts) items.push({ tone: "info", text: `Faltan distribuciones de teclado en esta computadora: ${core.missingLayouts}.` });
  if (core.ipc === "versionMismatch") {
    items.push({ tone: "info", text: "El Deskflow que está corriendo es de otra versión que el instalado. Algunos datos pueden faltar." });
  }
  if (items.length === 0) return null;

  return (
    <div className="alerts">
      {items.map((a, i) => (
        <div key={i} className={`alert alert--${a.tone}`} role={a.tone === "danger" ? "alert" : undefined}>
          <span>{a.text}</span>
          {a.onClose && (
            <button className="alert__close" onClick={a.onClose} aria-label="Cerrar">
              ×
            </button>
          )}
        </div>
      ))}
    </div>
  );
}

function ClientCard({ host, port, running, busy, onSave }: { host: string; port: number; running: boolean; busy: boolean; onSave: (h: string) => void }) {
  const [value, setValue] = useState(host);
  useEffect(() => setValue(host), [host]);
  const dirty = value.trim() !== host && value.trim() !== "";

  return (
    <section className="card">
      <h3>Computadora que comparte el teclado</h3>
      <form
        className="field-row"
        onSubmit={(e) => {
          e.preventDefault();
          if (dirty) onSave(value.trim());
        }}
      >
        <label className="field">
          <span className="field__label">Nombre o dirección IP</span>
          <input value={value} onChange={(e) => setValue(e.target.value)} placeholder="192.168.1.10 o mi-pc.local" spellCheck={false} />
        </label>
        <button className={`btn ${dirty ? "btn--primary" : "btn--secondary"}`} type="submit" disabled={!dirty || busy}>
          {running ? "Guardar y reconectar" : "Guardar"}
        </button>
      </form>
      <p className="muted small">Puerto {port}. Lo encontrás en la pantalla principal de Deskflow en la otra computadora.</p>
    </section>
  );
}

function Tabs({ tabs, current, badges, onChange }: { tabs: Tab[]; current: TabId; badges: Partial<Record<TabId, number>>; onChange: (id: TabId) => void }) {
  const refs = useRef<Partial<Record<TabId, HTMLButtonElement | null>>>({});
  const move = (by: number) => {
    const i = tabs.findIndex((t) => t.id === current);
    const next = tabs[(i + by + tabs.length) % tabs.length].id;
    onChange(next);
    refs.current[next]?.focus();
  };
  return (
    <nav className="tabs" role="tablist" aria-label="Secciones">
      {tabs.map((t) => {
        const badge = badges[t.id] ?? 0;
        return (
          <button
            key={t.id}
            ref={(el) => {
              refs.current[t.id] = el;
            }}
            id={`tab-${t.id}`}
            role="tab"
            aria-selected={t.id === current}
            aria-controls={`panel-${t.id}`}
            tabIndex={t.id === current ? 0 : -1}
            className={`tab ${t.id === current ? "tab--on" : ""}`}
            onClick={() => onChange(t.id)}
            onKeyDown={(e) => {
              if (e.key === "ArrowRight") move(1);
              else if (e.key === "ArrowLeft") move(-1);
              else return;
              e.preventDefault();
            }}
          >
            {t.label}
            {badge > 0 && (
              <span className="badge" title={badge === 1 ? "1 aviso desde que arrancó" : `${badge} avisos desde que arrancó`}>
                {badge}
              </span>
            )}
          </button>
        );
      })}
    </nav>
  );
}

function Panel({ id, current, children }: { id: TabId; current: TabId; children: ReactNode }) {
  return (
    <div className="panel" role="tabpanel" id={`panel-${id}`} aria-labelledby={`tab-${id}`} hidden={id !== current}>
      {children}
    </div>
  );
}

function ActivityLog({ lines, visible }: { lines: ReturnType<typeof parseLogLine>[]; visible: boolean }) {
  const [onlyProblems, setOnlyProblems] = useState(false);
  const listRef = useRef<HTMLOListElement>(null);
  const shown = onlyProblems ? lines.filter((l) => l.level === "error" || l.level === "warn") : lines;

  // a hidden list can't scroll, so go to the end each time it shows
  useEffect(() => {
    const el = listRef.current;
    if (visible && el) el.scrollTop = el.scrollHeight;
  }, [visible, shown.length]);

  return (
    <section className="card log">
      <div className="log__header">
        <h3>Actividad</h3>
        <label className="check">
          <input type="checkbox" checked={onlyProblems} onChange={(e) => setOnlyProblems(e.target.checked)} />
          Solo avisos y errores
        </label>
      </div>
      <ol className="log__list" ref={listRef}>
        {shown.length === 0 && <li className="muted">Sin actividad.</li>}
        {shown.slice(-500).map((l, i) => (
          <li key={i} className={`log__line log__line--${l.level}`}>
            {l.time && <time>{l.time}</time>}
            <span>{l.text}</span>
          </li>
        ))}
      </ol>
    </section>
  );
}
