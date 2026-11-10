/**
 * The words of the app in the person's language.
 *
 * Each language has a folder in `locales/` with one file per part of the app. Spanish
 * is the reference: its keys are the ones `t()` accepts, and a test keeps the other
 * languages with exactly the same keys and placeholders. The `native` files are read
 * by the Rust side too (`src-tauri/src/i18n.rs`), for the tray and its error messages.
 *
 * Texts take `{name}` placeholders. Plurals are keys ending in `_one`, `_other` (and
 * `_many`, `_few`… where a language needs them), chosen by `tn()` with `{count}`.
 */
import { useSyncExternalStore } from "react";
import about from "./locales/es/about.json";
import app from "./locales/es/app.json";
import clipboard from "./locales/es/clipboard.json";
import common from "./locales/es/common.json";
import hotkeys from "./locales/es/hotkeys.json";
import layout from "./locales/es/layout.json";
import native from "./locales/es/native.json";
import options from "./locales/es/options.json";
import security from "./locales/es/security.json";
import update from "./locales/es/update.json";

const REFERENCE = { about, app, clipboard, common, hotkeys, layout, native, options, security, update };
type Schema = typeof REFERENCE;
type Namespace = keyof Schema;

/** Every text there is, as `namespace.key`. */
export type Key = { [N in Namespace]: `${N}.${Extract<keyof Schema[N], string>}` }[Namespace];
/** Texts that change with a number: `namespace.key` for `key_one`, `key_other`… */
type PluralBase<K> = K extends `${infer Base}_other` ? Base : never;
export type PluralKey = PluralBase<Key>;

export const LANGUAGES = [
  { code: "es", name: "Español" },
  { code: "en", name: "English" },
  { code: "fr", name: "Français" },
  { code: "de", name: "Deutsch" },
  { code: "pt", name: "Português" },
] as const;
export type Lang = (typeof LANGUAGES)[number]["code"];
/** What the person chose: a language, or whatever Windows uses. */
export type LanguageChoice = Lang | "auto";

type Dict = Record<string, string>;
const files = import.meta.glob<Dict>("./locales/*/*.json", { eager: true, import: "default" });

/** Every language's texts, flattened to `namespace.key`. */
export const DICTIONARIES: Record<string, Dict> = {};
for (const [path, dict] of Object.entries(files)) {
  const match = path.match(/\/locales\/(\w+)\/(\w+)\.json$/);
  if (!match) continue;
  const [, lang, ns] = match;
  const target = (DICTIONARIES[lang] ??= {});
  for (const [key, text] of Object.entries(dict)) target[`${ns}.${key}`] = text;
}

export function isLang(value: string): value is Lang {
  return LANGUAGES.some((l) => l.code === value);
}

/** The language to show: the one chosen, or the first of the system's we have, or English. */
export function resolveLanguage(choice: string, preferred: readonly string[]): Lang {
  if (isLang(choice)) return choice;
  for (const tag of preferred) {
    const base = tag.toLowerCase().split(/[-_]/)[0];
    if (isLang(base)) return base;
  }
  return "en";
}

let current: Lang = "es";
const listeners = new Set<() => void>();

export function language(): Lang {
  return current;
}

export function setLanguage(lang: Lang) {
  if (lang === current) return;
  current = lang;
  if (typeof document !== "undefined") document.documentElement.lang = lang;
  for (const listener of listeners) listener();
}

function subscribe(listener: () => void) {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

/** Re-renders when the language changes; the root uses it so everything follows. */
export function useLanguage(): Lang {
  return useSyncExternalStore(subscribe, language);
}

export type Vars = Record<string, string | number>;

function fill(text: string, vars?: Vars): string {
  if (!vars) return text;
  return text.replace(/\{(\w+)\}/g, (whole, name: string) => (name in vars ? String(vars[name]) : whole));
}

function lookup(lang: string, key: string): string | undefined {
  return DICTIONARIES[lang]?.[key];
}

/** The text for `key` in the current language, falling back to Spanish. */
export function t(key: Key, vars?: Vars): string {
  return fill(lookup(current, key) ?? lookup("es", key) ?? key, vars);
}

function pluralText(lang: string, key: string, count: number): string | undefined {
  const category = new Intl.PluralRules(lang).select(count);
  return lookup(lang, `${key}_${category}`) ?? lookup(lang, `${key}_other`);
}

/** The text for `count` of something: `{count}` is filled in, besides `vars`. */
export function tn(key: PluralKey, count: number, vars?: Vars): string {
  const text = pluralText(current, key, count) ?? pluralText("es", key, count) ?? key;
  return fill(text, { count, ...vars });
}
