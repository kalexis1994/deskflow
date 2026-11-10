import { invoke } from "@tauri-apps/api/core";
import { isLang, resolveLanguage, setLanguage, type LanguageChoice } from "./index";

let choice: LanguageChoice = "auto";

/** What the person chose, which may be "auto". */
export function languageChoice(): LanguageChoice {
  return choice;
}

/** Windows' language, as far as one of ours goes. */
export function systemLanguage() {
  return resolveLanguage("auto", navigator.languages ?? [navigator.language]);
}

function apply(next: LanguageChoice) {
  choice = next;
  const lang = resolveLanguage(next, navigator.languages ?? [navigator.language]);
  setLanguage(lang);
  document.documentElement.lang = lang;
  // the tray and the errors from the Rust side follow, and the choice is kept there
  invoke("set_language", { choice: next, effective: lang }).catch(() => {});
}

/** Before the first render, so nothing shows in the wrong language. */
export async function initLanguage() {
  let saved = "auto";
  try {
    saved = await invoke<string>("language_choice");
  } catch {
    // outside Tauri, or an old backend: follow the system
  }
  apply(isLang(saved) ? saved : "auto");
}

export function chooseLanguage(next: LanguageChoice) {
  apply(next);
}
