import { useCallback, useEffect, useLayoutEffect, useRef, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { hasMainKey, heldModifiers, keyLabel, sequenceFromEvent } from "./keys";
import {
  ACTION_KINDS,
  DIRECTION_LABELS,
  LOCK_LABELS,
  CTRL_ALT_DEL,
  PRESETS,
  addPreset,
  blocking,
  describeAction,
  hotkeyProblems,
  newAction,
  sendsKeys,
  type Action,
  type ActionKind,
  type Direction,
  type Hotkey,
  type LockMode,
} from "./model";
import "./HotkeysCard.css";

interface HotkeysView {
  hotkeys: Hotkey[];
  computers: string[];
  server: string;
}

interface Props {
  running: boolean;
  /** Changes when the layout is saved, since that can rename or drop computers. */
  layoutVersion: number;
}

const same = (a: Hotkey[], b: Hotkey[]) => JSON.stringify(a) === JSON.stringify(b);

export function HotkeysCard({ running, layoutVersion }: Props) {
  const [saved, setSaved] = useState<Hotkey[] | null>(null);
  const [draft, setDraft] = useState<Hotkey[]>([]);
  const [computers, setComputers] = useState<string[]>([]);
  const [server, setServer] = useState("");
  const [editing, setEditing] = useState<number | null>(null);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState<{ tone: "ok" | "danger"; text: string } | null>(null);
  const dirtyRef = useRef(false);

  const apply = (v: HotkeysView) => {
    setSaved(v.hotkeys);
    setDraft(v.hotkeys);
    setComputers(v.computers);
    setServer(v.server);
  };

  const load = useCallback(() => {
    invoke<HotkeysView>("get_hotkeys")
      .then(apply)
      .catch((e) => setMessage({ tone: "danger", text: String(e) }));
  }, []);

  useEffect(() => {
    // after a layout save the backend may have renamed or dropped targets; reload unless mid-edit
    if (!dirtyRef.current) load();
  }, [load, layoutVersion]);

  if (!saved) return message ? <div className="alert alert--danger">{message.text}</div> : null;

  const dirty = !same(draft, saved);
  dirtyRef.current = dirty;
  const canSave = dirty && !draft.some((h) => blocking(h, computers));

  const update = (next: Hotkey[]) => {
    setDraft(next);
    setMessage(null);
  };
  const setHotkey = (i: number, hk: Hotkey) => update(draft.map((h, j) => (j === i ? hk : h)));
  const remove = (i: number) => {
    update(draft.filter((_, j) => j !== i));
    setEditing(null);
  };
  const addNew = () => {
    update([...draft, { keys: [], actions: [newAction("switchToScreen", computers)] }]);
    setEditing(draft.length);
  };

  const save = async () => {
    setBusy(true);
    try {
      apply(await invoke<HotkeysView>("save_hotkeys", { hotkeys: draft }));
      setEditing(null);
      setMessage({ tone: "ok", text: running ? "Guardado. Deskflow se reinició para aplicarlo." : "Guardado. Se aplica la próxima vez que inicies Deskflow." });
    } catch (e) {
      setMessage({ tone: "danger", text: String(e) });
    } finally {
      setBusy(false);
    }
  };

  const presets = PRESETS.filter((p) => addPreset(draft, p, computers, server).length > draft.length);

  return (
    <section className="card hk">
      <div className="hk__head">
        <div>
          <h3>Atajos de teclado</h3>
          <p className="muted small">Combinaciones que apretás en esta computadora para moverte entre pantallas o mandar teclas a otra.</p>
        </div>
        <button className="btn btn--secondary btn--small" onClick={addNew}>
          + Nuevo atajo
        </button>
      </div>

      {draft.length === 0 && <p className="muted hk__empty">Todavía no hay atajos. Podés empezar con alguno de estos:</p>}

      {draft.length > 0 && (
        <ul className="hk__list">
          {draft.map((hk, i) => {
            const problems = hotkeyProblems(hk, computers, draft);
            const open = editing === i;
            return (
              <li key={i} className={`hk__item ${open ? "hk__item--open" : ""}`}>
                <div className="hk__row">
                  <button className="hk__summary" onClick={() => setEditing(open ? null : i)} aria-expanded={open}>
                    <Combo keys={hk.keys} placeholder="Sin combinación" />
                    <span className="hk__arrow" aria-hidden="true">
                      →
                    </span>
                    <span className="hk__actions-text">{hk.actions.length ? hk.actions.map(describeAction).join(" · ") : "No hace nada"}</span>
                  </button>
                  <button className="hk__icon" title="Quitar atajo" aria-label="Quitar atajo" onClick={() => remove(i)}>
                    ×
                  </button>
                </div>
                {problems.length > 0 && (
                  <ul className="hk__problems">
                    {problems.map((p) => (
                      <li key={p}>{p}</li>
                    ))}
                  </ul>
                )}
                {open && <HotkeyEditor hotkey={hk} computers={computers} onChange={(h) => setHotkey(i, h)} onDone={() => setEditing(null)} />}
              </li>
            );
          })}
        </ul>
      )}

      {presets.length > 0 && (
        <div className="hk__presets">
          {draft.length > 0 && <span className="muted small">Sugerencias:</span>}
          {presets.map((p) => (
            <button key={p.id} className="hk__preset" onClick={() => update(addPreset(draft, p, computers, server))}>
              <strong>{p.title}</strong>
              <span className="muted small">{p.text}</span>
            </button>
          ))}
        </div>
      )}

      <div className="so__footer">
        {message && <span className={`le__msg le__msg--${message.tone}`}>{message.text}</span>}
        {dirty && !message && <span className="le__msg">{canSave ? "Cambios sin guardar" : "Corregí los atajos marcados para guardar"}</span>}
        <button className="btn btn--secondary btn--small" disabled={!dirty || busy} onClick={() => update(saved)}>
          Descartar
        </button>
        <button className="btn btn--primary btn--small" disabled={!canSave || busy} onClick={save}>
          {busy ? "Guardando…" : running ? "Guardar y aplicar" : "Guardar"}
        </button>
      </div>
    </section>
  );
}

function Combo({ keys, placeholder }: { keys: number[]; placeholder: string }) {
  if (!keys.length) return <span className="muted small">{placeholder}</span>;
  return (
    <span className="hk__combo">
      {keys.map((k, i) => (
        <kbd key={i}>{keyLabel(k)}</kbd>
      ))}
    </span>
  );
}

function HotkeyEditor({ hotkey, computers, onChange, onDone }: { hotkey: Hotkey; computers: string[]; onChange: (h: Hotkey) => void; onDone: () => void }) {
  const setAction = (i: number, a: Action) => onChange({ ...hotkey, actions: hotkey.actions.map((x, j) => (j === i ? a : x)) });
  return (
    <div className="hk__editor">
      <div className="hk__field">
        <span className="field__label">Combinación que lo activa</span>
        <KeyRecorder keys={hotkey.keys} autoStart={!hotkey.keys.length} onChange={(keys) => onChange({ ...hotkey, keys })} />
      </div>

      <div className="hk__field">
        <span className="field__label">Qué hace</span>
        {hotkey.actions.map((a, i) => (
          <ActionEditor
            key={i}
            action={a}
            computers={computers}
            onChange={(next) => setAction(i, next)}
            onRemove={() => onChange({ ...hotkey, actions: hotkey.actions.filter((_, j) => j !== i) })}
          />
        ))}
        <button className="le__link" onClick={() => onChange({ ...hotkey, actions: [...hotkey.actions, newAction("switchToScreen", computers)] })}>
          + Agregar otra acción
        </button>
      </div>

      <div className="hk__editor-footer">
        <button className="btn btn--secondary btn--small" onClick={onDone}>
          Listo
        </button>
      </div>
    </div>
  );
}

function KeyRecorder({ keys, autoStart, onChange }: { keys: number[]; autoStart?: boolean; onChange: (keys: number[]) => void }) {
  // a new hotkey starts out recording, so the very first keypress is caught
  const [recording, setRecording] = useState(!!autoStart);
  const [held, setHeld] = useState<number[]>([]);
  const ref = useRef<HTMLButtonElement>(null);

  useLayoutEffect(() => {
    if (autoStart) ref.current?.focus();
    // only when the editor opens
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const stop = () => {
    setRecording(false);
    setHeld([]);
  };

  return (
    <button
      ref={ref}
      type="button"
      data-keycapture
      className={`hk__recorder ${recording ? "hk__recorder--on" : ""}`}
      onClick={() => setRecording(true)}
      onBlur={stop}
      onKeyDown={(e) => {
        if (!recording) {
          if (e.key === "Enter" || e.key === " ") {
            e.preventDefault();
            setRecording(true);
          }
          return;
        }
        // keep every key for ourselves while recording (also away from the layout editor's shortcuts)
        e.preventDefault();
        e.stopPropagation();
        const seq = sequenceFromEvent(e);
        const mods = heldModifiers(e);
        if (e.key === "Escape" && mods.length === 0) return stop();
        if (seq && hasMainKey(seq)) {
          onChange(seq);
          stop();
        } else {
          setHeld(mods);
        }
      }}
      onKeyUp={(e) => recording && setHeld(heldModifiers(e))}
    >
      {recording ? (
        held.length ? (
          <Combo keys={held} placeholder="" />
        ) : (
          <span className="muted">Apretá la combinación… (Esc para cancelar)</span>
        )
      ) : (
        <>
          <Combo keys={keys} placeholder="Hacé clic y apretá la combinación" />
          {keys.length > 0 && <span className="muted small hk__change">Cambiar</span>}
        </>
      )}
    </button>
  );
}

function ActionEditor({ action, computers, onChange, onRemove }: { action: Action; computers: string[]; onChange: (a: Action) => void; onRemove: () => void }) {
  const known = ACTION_KINDS.some((k) => k.kind === action.kind);
  const setKind = (kind: ActionKind) => onChange({ ...newAction(kind, computers), onRelease: action.onRelease });
  const toggleComputer = (name: string) =>
    onChange({ ...action, computers: action.computers.includes(name) ? action.computers.filter((c) => c !== name) : [...action.computers, name] });

  return (
    <div className="hk__action">
      <div className="hk__action-row">
        <select value={action.kind} onChange={(e) => setKind(e.target.value as ActionKind)} aria-label="Tipo de acción" disabled={!known}>
          {!known && <option value={action.kind}>Botón del mouse (de la app clásica)</option>}
          {ACTION_KINDS.map((k) => (
            <option key={k.kind} value={k.kind}>
              {k.label}
            </option>
          ))}
        </select>
        <button className="hk__icon" title="Quitar acción" aria-label="Quitar acción" onClick={onRemove}>
          ×
        </button>
      </div>

      {action.kind === "switchToScreen" && (
        <select value={action.target} onChange={(e) => onChange({ ...action, target: e.target.value })} aria-label="Computadora de destino">
          {!computers.includes(action.target) && <option value={action.target}>{action.target ? `${action.target} (ya no está)` : "Elegí una…"}</option>}
          {computers.map((c) => (
            <option key={c} value={c}>
              {c}
            </option>
          ))}
        </select>
      )}

      {action.kind === "switchInDirection" && (
        <Segmented<Direction>
          value={action.direction}
          options={(["left", "right", "up", "down"] as Direction[]).map((d) => [d, { left: "←", right: "→", up: "↑", down: "↓" }[d], DIRECTION_LABELS[d]])}
          onChange={(direction) => onChange({ ...action, direction })}
        />
      )}

      {action.kind === "lockCursorToScreen" && (
        <Segmented<LockMode>
          value={action.lockMode}
          options={(["toggle", "on", "off"] as LockMode[]).map((m) => [m, LOCK_LABELS[m], LOCK_LABELS[m]])}
          onChange={(lockMode) => onChange({ ...action, lockMode })}
        />
      )}

      {sendsKeys(action.kind) && (
        <>
          <div className="hk__keys-row">
            <KeyRecorder keys={action.keys} onChange={(keys) => onChange({ ...action, keys })} />
            <button
              className="le__chip"
              title="Ctrl+Alt+Supr no se puede grabar: lo atrapa Windows"
              onClick={() => onChange({ ...action, keys: CTRL_ALT_DEL })}
            >
              Ctrl+Alt+Supr
            </button>
          </div>
          <div className="hk__targets" role="group" aria-label="Computadoras donde funciona">
            <span className="muted small">Funciona en:</span>
            <button className={`le__chip ${action.computers.length === 0 ? "le__chip--on" : ""}`} onClick={() => onChange({ ...action, computers: [] })}>
              Cualquiera
            </button>
            {computers.map((c) => (
              <button key={c} className={`le__chip ${action.computers.includes(c) ? "le__chip--on" : ""}`} onClick={() => toggleComputer(c)}>
                {c}
              </button>
            ))}
          </div>
          <p className="muted small">Las teclas van solo a la computadora donde está el mouse.</p>
        </>
      )}

      <label className="le__check hk__release">
        <input type="checkbox" checked={action.onRelease} onChange={(e) => onChange({ ...action, onRelease: e.target.checked })} />
        <span>Hacerlo al soltar el atajo, no al apretarlo</span>
      </label>
    </div>
  );
}

function Segmented<T extends string>({ value, options, onChange }: { value: T; options: [T, string, string][]; onChange: (v: T) => void }) {
  return (
    <div className="hk__segmented" role="radiogroup">
      {options.map(([v, label, title]) => (
        <button key={v} role="radio" aria-checked={value === v} title={title} aria-label={title} className={value === v ? "hk__seg--on" : ""} onClick={() => onChange(v)}>
          {label}
        </button>
      ))}
    </div>
  );
}
