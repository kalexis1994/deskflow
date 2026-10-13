import { useState } from "react";
import {
  MODIFIER_KEYS,
  MODIFIER_LABELS,
  aliasError,
  cornersOn,
  defaultOptions,
  isMacSwap,
  modifiersChanged,
  toggleMacSwap,
  type ComputerOptions,
  type Modifier,
} from "./options";

interface PanelProps {
  options: ComputerOptions;
  onChange: (o: ComputerOptions) => void;
}

const KEY_NAMES: Record<string, string> = {
  shift: "Shift",
  ctrl: "Ctrl",
  alt: "Alt",
  altgr: "AltGr",
  meta: "Meta",
  super: "⊞ / ⌘",
};

export function KeyboardPanel({ options, onChange }: PanelProps) {
  const setModifier = (key: (typeof MODIFIER_KEYS)[number], value: Modifier) =>
    onChange({ ...options, modifiers: { ...options.modifiers, [key]: value } });

  return (
    <>
      <div className="le__section">
        <h5>Teclas modificadoras</h5>
        <p className="muted small">Qué recibe esta computadora cuando apretás cada tecla.</p>
        <button
          className={`le__toggle ${isMacSwap(options) ? "le__toggle--on" : ""}`}
          aria-pressed={isMacSwap(options)}
          onClick={() => onChange(toggleMacSwap(options))}
        >
          <span className="le__switch" aria-hidden="true" />
          Intercambiar Ctrl y ⌘
          <span className="muted small">para controlar una Mac desde una PC, o al revés</span>
        </button>
        <div className="le__modgrid">
          {MODIFIER_KEYS.map((k) => (
            <label key={k} className={`le__modrow ${options.modifiers[k] !== k ? "le__modrow--changed" : ""}`}>
              <kbd>{KEY_NAMES[k]}</kbd>
              <span aria-hidden="true">→</span>
              <select value={options.modifiers[k]} onChange={(e) => setModifier(k, e.target.value as Modifier)} aria-label={`Al apretar ${KEY_NAMES[k]}`}>
                {(Object.keys(MODIFIER_LABELS) as Modifier[]).map((m) => (
                  <option key={m} value={m}>
                    {MODIFIER_LABELS[m]}
                  </option>
                ))}
              </select>
            </label>
          ))}
        </div>
        {modifiersChanged(options) && (
          <button className="le__link" onClick={() => onChange({ ...options, modifiers: defaultOptions().modifiers })}>
            Restablecer modificadoras
          </button>
        )}
      </div>

      <div className="le__section">
        <h5>Teclas de bloqueo</h5>
        <p className="muted small">Marcá una si en esta computadora queda activada al revés o hay que apretarla dos veces.</p>
        <Check label="Bloq Mayús" checked={options.fixCapsLock} onChange={(v) => onChange({ ...options, fixCapsLock: v })} />
        <Check label="Bloq Num" checked={options.fixNumLock} onChange={(v) => onChange({ ...options, fixNumLock: v })} />
        <Check label="Bloq Despl" checked={options.fixScrollLock} onChange={(v) => onChange({ ...options, fixScrollLock: v })} />
      </div>
    </>
  );
}

interface AdvancedProps extends PanelProps {
  name: string;
  /** Names and aliases of every computer, to keep aliases unique. */
  taken: { name: string; aliases: string[] }[];
}

export function AdvancedPanel({ options, onChange, name, taken }: AdvancedProps) {
  return (
    <>
      <Aliases options={options} onChange={onChange} name={name} taken={taken} />
      <DeadCorners options={options} onChange={onChange} />
      <div className="le__section">
        <h5>Solo Linux (X11)</h5>
        <Check
          label="Usar XTest con Xinerama"
          hint="Si el cursor aparece en el monitor equivocado en una computadora con varios monitores."
          checked={options.xtestXinerama}
          onChange={(v) => onChange({ ...options, xtestXinerama: v })}
        />
        <Check
          label="No robar el foco al entrar"
          hint="La ventana activa no recibe el foco apenas pasás a esta computadora."
          checked={options.weakX11Focus}
          onChange={(v) => onChange({ ...options, weakX11Focus: v })}
        />
      </div>
    </>
  );
}

function Aliases({ options, onChange, name, taken }: AdvancedProps) {
  const [text, setText] = useState("");
  const [error, setError] = useState<string | null>(null);

  const add = () => {
    const alias = text.trim();
    if (!alias) return;
    const err = aliasError(alias, name, taken);
    setError(err);
    if (err) return;
    onChange({ ...options, aliases: [...options.aliases, alias] });
    setText("");
  };

  return (
    <div className="le__section">
      <h5>Otros nombres</h5>
      <p className="muted small">Nombres con los que esta computadora también puede conectarse, por ejemplo si cambia su nombre de red.</p>
      {options.aliases.length > 0 && (
        <ul className="le__aliases">
          {options.aliases.map((a) => (
            <li key={a}>
              {a}
              <button aria-label={`Quitar ${a}`} onClick={() => onChange({ ...options, aliases: options.aliases.filter((x) => x !== a) })}>
                ×
              </button>
            </li>
          ))}
        </ul>
      )}
      <form
        className="le__inline"
        onSubmit={(e) => {
          e.preventDefault();
          add();
        }}
      >
        <input
          value={text}
          placeholder="otro-nombre"
          spellCheck={false}
          onChange={(e) => {
            setText(e.target.value);
            setError(null);
          }}
          aria-label="Nuevo alias"
        />
        <button className="btn btn--secondary btn--small" type="submit" disabled={!text.trim()}>
          Agregar
        </button>
      </form>
      {error && <p className="le__error">{error}</p>}
    </div>
  );
}

const CORNERS = [
  ["topLeft", "arriba a la izquierda"],
  ["topRight", "arriba a la derecha"],
  ["bottomLeft", "abajo a la izquierda"],
  ["bottomRight", "abajo a la derecha"],
] as const;

function DeadCorners({ options, onChange }: PanelProps) {
  const on = cornersOn(options);
  const setCorner = (k: (typeof CORNERS)[number][0], v: boolean) => {
    const next = { ...options, corners: { ...options.corners, [k]: v } };
    // a dead corner of size 0 does nothing; start from something useful
    if (v && next.cornerSize === 0) next.cornerSize = 20;
    onChange(next);
  };

  return (
    <div className="le__section">
      <h5>Esquinas muertas</h5>
      <p className="muted small">En las esquinas marcadas el mouse no pasa a otra pantalla. Sirve para el botón de Inicio o las esquinas activas.</p>
      <div className="le__corners">
        <div className="le__corners-screen" aria-hidden="true" />
        {CORNERS.map(([k, label]) => (
          <button
            key={k}
            className={`le__corner le__corner--${k} ${options.corners[k] ? "le__corner--on" : ""}`}
            aria-pressed={options.corners[k]}
            aria-label={`Esquina ${label}`}
            title={`Esquina ${label}`}
            onClick={() => setCorner(k, !options.corners[k])}
          />
        ))}
      </div>
      <label className={`le__range ${on ? "" : "le__range--off"}`}>
        <span className="field__label">Tamaño</span>
        <input
          type="range"
          min={0}
          max={200}
          step={5}
          value={options.cornerSize}
          disabled={!on}
          onChange={(e) => onChange({ ...options, cornerSize: Number(e.target.value) })}
        />
        <span className="le__range-value">{options.cornerSize} px</span>
      </label>
    </div>
  );
}

function Check({ label, hint, checked, onChange }: { label: string; hint?: string; checked: boolean; onChange: (v: boolean) => void }) {
  return (
    <label className="le__check">
      <input type="checkbox" checked={checked} onChange={(e) => onChange(e.target.checked)} />
      <span>
        {label}
        {hint && <span className="muted small le__check-hint">{hint}</span>}
      </span>
    </label>
  );
}
