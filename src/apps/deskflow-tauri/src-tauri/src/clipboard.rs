//! Shared clipboard activity reported by the core (`clipboard=<json>` over IPC, see
//! `src/lib/deskflow/ClipboardUtil.cpp`) and the preferences around it.

use serde::{Deserialize, Serialize};

use crate::paths;
use crate::server_options;
use crate::settings::IniFile;

/// How many recent events the panel keeps; only in memory.
pub const HISTORY: usize = 5;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
pub struct ClipboardImage {
    pub width: u32,
    pub height: u32,
}

/// One thing that happened to the shared clipboard.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase", default)]
pub struct ClipboardActivity {
    /// sent, received, shared, skipped or failed.
    pub event: String,
    /// The other computer, when the core knows it.
    pub computer: Option<String>,
    /// skipped/failed: tooLarge, nothingToShare or busy.
    pub reason: Option<String>,
    /// text, HTML, bitmap: what was shared.
    pub formats: Vec<String>,
    /// Left out to fit the size limit.
    pub dropped: Vec<String>,
    /// Bytes on the wire.
    pub size: Option<u64>,
    pub chars: Option<u64>,
    pub lines: Option<u64>,
    pub image: Option<ClipboardImage>,
    /// Start of the copied text, only when previews are on.
    pub text: Option<String>,
    /// Unix time in ms when it reached this app.
    #[serde(skip_deserializing)]
    pub at: u64,
}

/// Adds an event from the core, newest first. A GUI that (re)connects gets the
/// latest event again; that replay is not a new event and is dropped.
pub fn push(list: &mut Vec<ClipboardActivity>, json: &str, now: u64) -> bool {
    let Ok(mut activity) = serde_json::from_str::<ClipboardActivity>(json) else {
        return false;
    };
    if let Some(latest) = list.first() {
        activity.at = latest.at;
        if activity == *latest {
            return false;
        }
    }
    activity.at = now;
    list.insert(0, activity);
    list.truncate(HISTORY);
    true
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct ClipboardPrefs {
    /// Whether the core includes the start of copied text in its reports.
    pub preview: bool,
    /// The server's settings; on a client the server it connects to decides.
    pub sharing: bool,
    pub limit_mb: u32,
}

fn read_prefs(ini: &IniFile) -> ClipboardPrefs {
    let options = server_options::read(ini);
    ClipboardPrefs {
        // same default as CoreSettings::Core::ClipboardPreview
        preview: ini
            .get("core", "clipboardPreview")
            .is_none_or(|v| v == "true"),
        sharing: options.clipboard,
        limit_mb: options.clipboard_size_mb,
    }
}

pub fn prefs() -> Result<ClipboardPrefs, String> {
    let ini = IniFile::load(&paths::settings_file()).map_err(|e| e.to_string())?;
    Ok(read_prefs(&ini))
}

/// The core reads the setting again on its next report; no restart needed.
pub fn set_preview(enabled: bool) -> Result<ClipboardPrefs, String> {
    let mut ini = IniFile::load(&paths::settings_file()).map_err(|e| e.to_string())?;
    ini.set(
        "core",
        "clipboardPreview",
        if enabled { "true" } else { "false" },
    );
    ini.save().map_err(|e| e.to_string())?;
    Ok(read_prefs(&ini))
}

#[cfg(test)]
mod tests {
    use super::*;

    const SENT: &str = r#"{"event":"sent","dropped":[],"size":42,"chars":11,"lines":1,"text":"hola\nmundo","formats":["text"]}"#;

    #[test]
    fn parses_what_the_core_sends() {
        let mut list = Vec::new();
        let json = r#"{"event":"shared","computer":"laptop","dropped":["bitmap"],"size":1200,"chars":5,"lines":1,"text":"hola","image":{"width":1920,"height":1080},"formats":["text","HTML"]}"#;
        assert!(push(&mut list, json, 1000));
        let a = &list[0];
        assert_eq!(a.event, "shared");
        assert_eq!(a.computer.as_deref(), Some("laptop"));
        assert_eq!(a.dropped, vec!["bitmap"]);
        assert_eq!(a.formats, vec!["text", "HTML"]);
        assert_eq!(
            a.image,
            Some(ClipboardImage {
                width: 1920,
                height: 1080
            })
        );
        assert_eq!(a.text.as_deref(), Some("hola"));
        assert_eq!(a.at, 1000);
    }

    #[test]
    fn minimal_events_and_garbage() {
        let mut list = Vec::new();
        assert!(push(
            &mut list,
            r#"{"event":"skipped","reason":"nothingToShare","dropped":[],"formats":[]}"#,
            1
        ));
        assert_eq!(list[0].reason.as_deref(), Some("nothingToShare"));
        assert_eq!(list[0].size, None);
        assert!(!push(&mut list, "not json", 2));
        assert_eq!(list.len(), 1);
    }

    #[test]
    fn replay_on_reconnect_is_not_a_new_event() {
        let mut list = Vec::new();
        assert!(push(&mut list, SENT, 1));
        assert!(!push(&mut list, SENT, 5000));
        assert_eq!(list.len(), 1);
        assert_eq!(list[0].at, 1);
    }

    #[test]
    fn keeps_the_latest_few_newest_first() {
        let mut list = Vec::new();
        for i in 0..8 {
            let json = format!(r#"{{"event":"sent","size":{i},"dropped":[],"formats":["text"]}}"#);
            assert!(push(&mut list, &json, i));
        }
        assert_eq!(list.len(), HISTORY);
        assert_eq!(list[0].size, Some(7));
        assert_eq!(list[HISTORY - 1].size, Some(3));
    }

    fn empty_ini() -> IniFile {
        IniFile::load(&std::env::temp_dir().join(format!(
            "clipboard-prefs-{}-missing.conf",
            std::process::id()
        )))
        .unwrap()
    }

    #[test]
    fn preview_defaults_on_and_reads_the_server_options() {
        let mut ini = empty_ini();
        let p = read_prefs(&ini);
        assert!(p.preview);
        assert!(p.sharing);
        assert_eq!(p.limit_mb, 3);

        ini.set("core", "clipboardPreview", "false");
        ini.set("server", "enableClipboard", "false");
        ini.set("server", "clipboardSize", "10");
        let p = read_prefs(&ini);
        assert!(!p.preview);
        assert!(!p.sharing);
        assert_eq!(p.limit_mb, 10);
    }
}
