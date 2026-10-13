import { useEffect, useState, type ReactNode } from "react";
import { invoke } from "@tauri-apps/api/core";
import "./ServerOptionsCard.css";

// Mirrors src-tauri/src/server_options.rs
export interface ServerOptions {
  clipboard: boolean;
  clipboardSizeMb: number;
  switchDelay: boolean;
  switchDelayMs: number;
  doubleTap: boolean;
  doubleTapMs: number;
  lockKey: boolean;
  startLocked: boolean;
  relativeMoves: boolean;
  heartbeat: boolean;
  heartbeatMs: number;
  protocol: "barrier" | "synergy";
  win32KeepForeground: boolean;
}

export const DEFAULT_SERVER_OPTIONS: ServerOptions = {
  clipboard: true,
  clipboardSizeMb: 3,
  switchDelay: false,
  switchDelayMs: 250,
  doubleTap: false,
  doubleTapMs: 250,
  lockKey: true,
  startLocked: false,
  relativeMoves: false,
  heartbeat: false,
  heartbeatMs: 5000,
  protocol: "barrier",
  win32KeepForeground: true,
};

const same = (a: ServerOptions, b: ServerOptions) => JSON.stringify(a) === JSON.stringify(b);

export function ServerOptionsCard({ running }: { running: boolean }) {
  const [saved, setSaved] = useState<ServerOptions | null>(null);
  const [draft, setDraft] = useState<ServerOptions | null>(null);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState<{ tone: "ok" | "danger"; text: string } | null>(null);

  useEffect(() => {
    invoke<ServerOptions>("get_server_options")
      .then((o) => {
        setSaved(o);
        setDraft(o);
      })
      .catch((e) => setMessage({ tone: "danger", text: String(e) }));
  }, []);

  if (!draft || !saved) {
    return message ? <div className="alert alert--danger">{message.text}</div> : null;
  }

  const set = (patch: Partial<ServerOptions>) => {
    setDraft({ ...draft, ...patch });
    setMessage(null);
  };
  const dirty = !same(draft, saved);
  const isDefault = same(draft, DEFAULT_SERVER_OPTIONS);

  const save = async () => {
    setBusy(true);
    try {
      const o = await invoke<ServerOptions>("save_server_options", { options: draft });
      setSaved(o);
      setDraft(o);
      setMessage({ tone: "ok", text: running ? "Guardado. Deskflow se reinició para aplicarlo." : "Guardado. Se aplica la próxima vez que inicies Deskflow." });
    } catch (e) {
      setMessage({ tone: "danger", text: String(e) });
    } finally {
      setBusy(false);
    }
  };

  return (
    <section className="card so">
      <div className="so__head">
        <div>
          <h3>Opciones del servidor</h3>
          <p className="muted small">Valen para todas las computadoras conectadas a esta.</p>
        </div>
        {!isDefault && (
          <button className="le__link" onClick={() => set(DEFAULT_SERVER_OPTIONS)}>
            Volver a los valores por defecto
          </button>
        )}
      </div>

      <div className="so__grid">
        <Group title="Portapapeles">
          <Switch
            label="Compartir el portapapeles"
            hint="Copiás en una computadora y pegás en otra."
            on={draft.clipboard}
            onChange={(v) => set({ clipboard: v, clipboardSizeMb: v && draft.clipboardSizeMb < 1 ? 3 : draft.clipboardSizeMb })}
          >
            <Num label="Hasta" value={draft.clipboardSizeMb} min={1} max={10000} step={1} unit="MB" onChange={(v) => set({ clipboardSizeMb: v })} />
          </Switch>
        </Group>

        <Group title="Pasar de una pantalla a otra">
          <Switch
            label="Esperar en el borde antes de pasar"
            hint="Evita pasar sin querer cuando el mouse roza el borde."
            on={draft.switchDelay}
            onChange={(v) => set({ switchDelay: v })}
          >
            <Num label="Esperar" value={draft.switchDelayMs} min={10} max={10000} step={10} unit="ms" onChange={(v) => set({ switchDelayMs: v })} />
          </Switch>
          <Switch
            label="Pasar solo con doble toque en el borde"
            hint="Tenés que llevar el mouse al borde dos veces seguidas."
            on={draft.doubleTap}
            onChange={(v) => set({ doubleTap: v })}
          >
            <Num label="Entre toques" value={draft.doubleTapMs} min={10} max={10000} step={10} unit="ms" onChange={(v) => set({ doubleTapMs: v })} />
          </Switch>
        </Group>

        <Group title="Fijar el mouse en una pantalla">
          <Switch
            label="Bloq Despl fija el mouse en la pantalla actual"
            hint="Útil para juegos o programas a pantalla completa."
            on={draft.lockKey}
            onChange={(v) => set({ lockKey: v })}
          />
          <Switch label="Empezar con el mouse fijado" on={draft.startLocked} onChange={(v) => set({ startLocked: v })} />
          <Switch
            label="Movimientos relativos del mouse"
            hint="Para juegos en 3D que capturan el mouse."
            on={draft.relativeMoves}
            onChange={(v) => set({ relativeMoves: v })}
          />
        </Group>

        <Group title="Conexión">
          <Switch
            label="Comprobar que las computadoras sigan conectadas"
            hint="Reconecta antes si una deja de responder."
            on={draft.heartbeat}
            onChange={(v) => set({ heartbeat: v })}
          >
            <Num
              label="Cada"
              value={draft.heartbeatMs / 1000}
              min={1}
              max={30}
              step={1}
              unit="s"
              onChange={(v) => set({ heartbeatMs: v * 1000 })}
            />
          </Switch>
          <div className="so__field">
            <span className="so__label">Compatibilidad</span>
            <div className="so__choices" role="radiogroup" aria-label="Protocolo de red">
              <Choice
                on={draft.protocol === "barrier"}
                onClick={() => set({ protocol: "barrier" })}
                title="Deskflow, Barrier, Input Leap"
                text="También Synergy 1. La opción normal."
              />
              <Choice on={draft.protocol === "synergy"} onClick={() => set({ protocol: "synergy" })} title="Synergy 3" text="Si alguna computadora usa Synergy 3." />
            </div>
          </div>
        </Group>

        <Group title="Windows">
          <Switch
            label="No robar la ventana activa"
            hint="Al volver a esta computadora, la ventana que estabas usando sigue al frente."
            on={draft.win32KeepForeground}
            onChange={(v) => set({ win32KeepForeground: v })}
          />
        </Group>
      </div>

      <div className="so__footer">
        {message && <span className={`le__msg le__msg--${message.tone}`}>{message.text}</span>}
        {dirty && !message && <span className="le__msg">Cambios sin guardar</span>}
        <button className="btn btn--secondary btn--small" disabled={!dirty || busy} onClick={() => setDraft(saved)}>
          Descartar
        </button>
        <button className="btn btn--primary btn--small" disabled={!dirty || busy} onClick={save}>
          {busy ? "Guardando…" : running ? "Guardar y aplicar" : "Guardar"}
        </button>
      </div>
    </section>
  );
}

function Group({ title, children }: { title: string; children: ReactNode }) {
  return (
    <div className="so__group">
      <h4>{title}</h4>
      {children}
    </div>
  );
}

function Switch({
  label,
  hint,
  on,
  onChange,
  children,
}: {
  label: string;
  hint?: string;
  on: boolean;
  onChange: (v: boolean) => void;
  children?: ReactNode;
}) {
  return (
    <div className="so__row">
      <button className={`so__switch ${on ? "so__switch--on" : ""}`} role="switch" aria-checked={on} aria-label={label} onClick={() => onChange(!on)}>
        <span className="le__switch" aria-hidden="true" />
        <span className="so__text">
          <span>{label}</span>
          {hint && <span className="muted small">{hint}</span>}
        </span>
      </button>
      {children && on && <div className="so__extra">{children}</div>}
    </div>
  );
}

function Num({
  label,
  value,
  min,
  max,
  step,
  unit,
  onChange,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  unit: string;
  onChange: (v: number) => void;
}) {
  const [text, setText] = useState(String(value));
  useEffect(() => setText(String(value)), [value]);
  const commit = () => {
    const n = Number(text);
    const v = Number.isFinite(n) ? Math.min(max, Math.max(min, Math.round(n / step) * step)) : value;
    setText(String(v));
    if (v !== value) onChange(v);
  };
  return (
    <label className="so__num">
      <span className="muted small">{label}</span>
      <input
        aria-label={`${label} (${unit})`}
        type="number"
        min={min}
        max={max}
        step={step}
        value={text}
        onChange={(e) => setText(e.target.value)}
        onBlur={commit}
        onKeyDown={(e) => e.key === "Enter" && (e.target as HTMLInputElement).blur()}
      />
      <span className="muted small">{unit}</span>
    </label>
  );
}

function Choice({ on, onClick, title, text }: { on: boolean; onClick: () => void; title: string; text: string }) {
  return (
    <button role="radio" aria-checked={on} aria-label={title} className={`so__choice ${on ? "so__choice--on" : ""}`} onClick={onClick}>
      <span className="so__radio" aria-hidden="true" />
      <span className="so__text">
        <span>{title}</span>
        <span className="muted small">{text}</span>
      </span>
    </button>
  );
}
