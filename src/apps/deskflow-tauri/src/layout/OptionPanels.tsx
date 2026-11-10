import { useState } from "react";
import { t, type Key } from "../i18n";
import {
  MODIFIERS,
  MODIFIER_KEYS,
  aliasError,
  cornersOn,
  defaultOptions,
  isMacSwap,
  modifierLabel,
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
  alt: "Alt",
  altgr: "AltGr",
  meta: "Meta",
  super: "⊞ / ⌘",
};

/** Ctrl and Shift read differently on some keyboards (Strg, Umschalt, Maj). */
function keyName(k: string): string {
  if (k === "ctrl") return t("hotkeys.key.ctrl");
  if (k === "shift") return t("hotkeys.key.shift");
  return KEY_NAMES[k];
}

export function KeyboardPanel({ options, onChange }: PanelProps) {
  const setModifier = (key: (typeof MODIFIER_KEYS)[number], value: Modifier) =>
    onChange({ ...options, modifiers: { ...options.modifiers, [key]: value } });

  return (
    <>
      <div className="le__section">
        <h5>{t("layout.keyboard.modifiersTitle")}</h5>
        <p className="muted small">{t("layout.keyboard.modifiersHint")}</p>
        <button
          className={`le__toggle ${isMacSwap(options) ? "le__toggle--on" : ""}`}
          aria-pressed={isMacSwap(options)}
          onClick={() => onChange(toggleMacSwap(options))}
        >
          <span className="le__switch" aria-hidden="true" />
          {t("layout.keyboard.macSwap")}
          <span className="muted small">{t("layout.keyboard.macSwapHint")}</span>
        </button>
        <div className="le__modgrid">
          {MODIFIER_KEYS.map((k) => (
            <label key={k} className={`le__modrow ${options.modifiers[k] !== k ? "le__modrow--changed" : ""}`}>
              <kbd>{keyName(k)}</kbd>
              <span aria-hidden="true">→</span>
              <select value={options.modifiers[k]} onChange={(e) => setModifier(k, e.target.value as Modifier)} aria-label={t("layout.keyboard.pressing", { key: keyName(k) })}>
                {MODIFIERS.map((m) => (
                  <option key={m} value={m}>
                    {modifierLabel(m)}
                  </option>
                ))}
              </select>
            </label>
          ))}
        </div>
        {modifiersChanged(options) && (
          <button className="le__link" onClick={() => onChange({ ...options, modifiers: defaultOptions().modifiers })}>
            {t("layout.keyboard.resetModifiers")}
          </button>
        )}
      </div>

      <div className="le__section">
        <h5>{t("layout.keyboard.locksTitle")}</h5>
        <p className="muted small">{t("layout.keyboard.locksHint")}</p>
        <Check label={t("layout.keyboard.capsLock")} checked={options.fixCapsLock} onChange={(v) => onChange({ ...options, fixCapsLock: v })} />
        <Check label={t("layout.keyboard.numLock")} checked={options.fixNumLock} onChange={(v) => onChange({ ...options, fixNumLock: v })} />
        <Check label={t("layout.keyboard.scrollLock")} checked={options.fixScrollLock} onChange={(v) => onChange({ ...options, fixScrollLock: v })} />
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
        <h5>{t("layout.advanced.linuxTitle")}</h5>
        <Check
          label={t("layout.advanced.xtest")}
          hint={t("layout.advanced.xtestHint")}
          checked={options.xtestXinerama}
          onChange={(v) => onChange({ ...options, xtestXinerama: v })}
        />
        <Check
          label={t("layout.advanced.weakFocus")}
          hint={t("layout.advanced.weakFocusHint")}
          checked={options.weakX11Focus}
          onChange={(v) => onChange({ ...options, weakX11Focus: v })}
        />
      </div>
    </>
  );
}

function Aliases({ options, onChange, name, taken }: AdvancedProps) {
  const [text, setText] = useState("");
  // the alias that was refused; its message is worked out on render so it follows the language
  const [refused, setRefused] = useState<string | null>(null);
  const error = refused === null ? null : aliasError(refused, name, taken);

  const add = () => {
    const alias = text.trim();
    if (!alias) return;
    if (aliasError(alias, name, taken)) return setRefused(alias);
    setRefused(null);
    onChange({ ...options, aliases: [...options.aliases, alias] });
    setText("");
  };

  return (
    <div className="le__section">
      <h5>{t("layout.aliases.title")}</h5>
      <p className="muted small">{t("layout.aliases.hint")}</p>
      {options.aliases.length > 0 && (
        <ul className="le__aliases">
          {options.aliases.map((a) => (
            <li key={a}>
              {a}
              <button aria-label={t("layout.aliases.remove", { name: a })} onClick={() => onChange({ ...options, aliases: options.aliases.filter((x) => x !== a) })}>
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
          placeholder={t("layout.aliases.placeholder")}
          spellCheck={false}
          onChange={(e) => {
            setText(e.target.value);
            setRefused(null);
          }}
          aria-label={t("layout.aliases.new")}
        />
        <button className="btn btn--secondary btn--small" type="submit" disabled={!text.trim()}>
          {t("layout.add")}
        </button>
      </form>
      {error && <p className="le__error">{error}</p>}
    </div>
  );
}

const CORNERS = [
  ["topLeft", "layout.corners.topLeft"],
  ["topRight", "layout.corners.topRight"],
  ["bottomLeft", "layout.corners.bottomLeft"],
  ["bottomRight", "layout.corners.bottomRight"],
] as const satisfies readonly (readonly [string, Key])[];

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
      <h5>{t("layout.corners.title")}</h5>
      <p className="muted small">{t("layout.corners.hint")}</p>
      <div className="le__corners">
        <div className="le__corners-screen" aria-hidden="true" />
        {CORNERS.map(([k, labelKey]) => (
          <button
            key={k}
            className={`le__corner le__corner--${k} ${options.corners[k] ? "le__corner--on" : ""}`}
            aria-pressed={options.corners[k]}
            aria-label={t(labelKey)}
            title={t(labelKey)}
            onClick={() => setCorner(k, !options.corners[k])}
          />
        ))}
      </div>
      <label className={`le__range ${on ? "" : "le__range--off"}`}>
        <span className="field__label">{t("layout.corners.size")}</span>
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
