//! The words of the tray and of the errors the window shows, in the person's language.
//!
//! They come from the `native` files of the window's translations
//! (`src/i18n/locales/<lang>/native.json`), so each text lives in one place. The window
//! decides the language, "auto" meaning Windows' own, and tells this side with
//! `set_language`; until then the tray speaks Spanish, or the language saved before.

use std::collections::HashMap;
use std::fmt::Display;
use std::path::PathBuf;
use std::sync::{OnceLock, RwLock};

use tauri::{AppHandle, Manager};

pub const LANGUAGES: [&str; 5] = ["es", "en", "fr", "de", "pt"];

const NATIVE: [(&str, &str); 5] = [
    ("es", include_str!("../../src/i18n/locales/es/native.json")),
    ("en", include_str!("../../src/i18n/locales/en/native.json")),
    ("fr", include_str!("../../src/i18n/locales/fr/native.json")),
    ("de", include_str!("../../src/i18n/locales/de/native.json")),
    ("pt", include_str!("../../src/i18n/locales/pt/native.json")),
];

type Table = HashMap<String, String>;

fn tables() -> &'static HashMap<&'static str, Table> {
    static TABLES: OnceLock<HashMap<&'static str, Table>> = OnceLock::new();
    TABLES.get_or_init(|| {
        NATIVE
            .iter()
            .map(|(lang, json)| (*lang, serde_json::from_str(json).unwrap_or_default()))
            .collect()
    })
}

static CURRENT: RwLock<&str> = RwLock::new("es");

fn current() -> &'static str {
    *CURRENT.read().unwrap()
}

/// Sets the language the tray and errors use; false if it isn't one there is.
pub fn set_current(lang: &str) -> bool {
    let Some(known) = LANGUAGES.iter().find(|l| **l == lang) else {
        return false;
    };
    *CURRENT.write().unwrap() = known;
    true
}

fn lookup(lang: &str, key: &str) -> String {
    let tables = tables();
    tables
        .get(lang)
        .and_then(|t| t.get(key))
        .or_else(|| tables.get("es").and_then(|t| t.get(key)))
        .cloned()
        .unwrap_or_else(|| key.to_owned())
}

fn fill(mut text: String, vars: &[(&str, &dyn Display)]) -> String {
    for (name, value) in vars {
        text = text.replace(&format!("{{{name}}}"), &value.to_string());
    }
    text
}

/// `native.json`'s text for `key` in `lang`, with `{name}` placeholders filled in.
pub fn text_in(lang: &str, key: &str, vars: &[(&str, &dyn Display)]) -> String {
    fill(lookup(lang, key), vars)
}

/// The text for `key` in the current language.
pub fn t(key: &str) -> String {
    text_in(current(), key, &[])
}

/// The text for `key` in the current language, with `{name}` placeholders filled in.
pub fn tf(key: &str, vars: &[(&str, &dyn Display)]) -> String {
    text_in(current(), key, vars)
}

fn choice_file(app: &AppHandle) -> Option<PathBuf> {
    Some(app.path().app_config_dir().ok()?.join("language"))
}

fn valid_choice(choice: &str) -> bool {
    choice == "auto" || LANGUAGES.contains(&choice)
}

/// What was chosen before: a language, or "auto".
pub fn saved_choice(app: &AppHandle) -> String {
    choice_file(app)
        .and_then(|f| std::fs::read_to_string(f).ok())
        .map(|text| text.trim().to_owned())
        .filter(|choice| valid_choice(choice))
        .unwrap_or_else(|| "auto".into())
}

/// Speaks the saved language from the start, when it is a fixed one.
pub fn load(app: &AppHandle) {
    set_current(&saved_choice(app));
}

#[tauri::command]
pub fn language_choice(app: AppHandle) -> String {
    saved_choice(&app)
}

/// Keeps `choice` and speaks `effective`, the language the window resolved it to.
#[tauri::command]
pub fn set_language(app: AppHandle, choice: String, effective: String) -> Result<(), String> {
    if !valid_choice(&choice) || !set_current(&effective) {
        return Err(format!("unknown language {choice} / {effective}"));
    }
    if saved_choice(&app) != choice {
        let file = choice_file(&app).ok_or("no config folder")?;
        if let Some(dir) = file.parent() {
            std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
        }
        std::fs::write(file, &choice).map_err(|e| e.to_string())?;
    }
    crate::tray::relabel(&app);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_language_has_every_text() {
        let tables = tables();
        let reference = &tables["es"];
        assert!(!reference.is_empty());
        for lang in LANGUAGES {
            let mut keys: Vec<_> = tables[lang].keys().collect();
            let mut expected: Vec<_> = reference.keys().collect();
            keys.sort();
            expected.sort();
            assert_eq!(keys, expected, "{lang}");
        }
    }

    #[test]
    fn fills_placeholders_and_falls_back() {
        assert_eq!(
            text_in("en", "tray.updateTo", &[("version", &"0.2.0")]),
            "Update to version 0.2.0…"
        );
        assert_eq!(text_in("xx", "tray.quit", &[]), "Salir");
        assert_eq!(text_in("en", "no.such.key", &[]), "no.such.key");
    }

    /// Every `t("…")`/`tf("…"` in the sources names a text that exists.
    #[test]
    fn the_sources_only_ask_for_texts_that_exist() {
        let reference = &tables()["es"];
        let dir = concat!(env!("CARGO_MANIFEST_DIR"), "/src");
        for entry in std::fs::read_dir(dir).unwrap() {
            let path = entry.unwrap().path();
            let source = std::fs::read_to_string(&path).unwrap();
            for call in ["i18n::t(\"", "i18n::tf(\""] {
                for (at, _) in source.match_indices(call) {
                    let rest = &source[at + call.len()..];
                    let key = &rest[..rest.find('"').unwrap()];
                    assert!(
                        reference.contains_key(key),
                        "{} asks for {key}",
                        path.display()
                    );
                }
            }
        }
    }
}
