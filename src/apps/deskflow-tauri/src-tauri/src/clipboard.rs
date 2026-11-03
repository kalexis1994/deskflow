//! Shared clipboard activity reported by the core (`clipboard=<json>` over IPC, see
//! `src/lib/deskflow/ClipboardUtil.cpp` and, for files, `src/lib/deskflow/files/FileReport.cpp`),
//! the progress of copies of files (`fileProgress=<json>`) and the preferences around them.

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

/// A copy of files: how many and which.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase", default)]
pub struct FilesSummary {
    pub count: u64,
    pub folders: u64,
    pub bytes: u64,
    /// The first few top-level names.
    pub names: Vec<String>,
    /// How many top-level files and folders there are in all.
    pub roots: u64,
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
    /// A copy of files, when that is what it was.
    pub files: Option<FilesSummary>,
    /// Why it failed, in English, as the core logs it.
    pub detail: Option<String>,
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

/// A copy of files on its way, out of or into this computer.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase", default)]
pub struct FileProgress {
    /// send or receive.
    pub direction: String,
    /// The other computer, when the core knows it.
    pub computer: Option<String>,
    pub count: u64,
    pub bytes: u64,
    pub done: u64,
}

#[derive(Deserialize)]
struct ProgressMessage {
    #[serde(default)]
    done: serde_json::Value,
}

/// The progress the core reports: `None` once it says the copy is over (or for
/// garbage).
pub fn parse_progress(json: &str) -> Option<FileProgress> {
    let message: ProgressMessage = serde_json::from_str(json).ok()?;
    if message.done == serde_json::Value::Bool(true) {
        return None;
    }
    serde_json::from_str(json).ok()
}

/// Same default as CoreSettings::Core::FileClipboardMaxSize.
pub const DEFAULT_FILES_LIMIT_MB: u32 = 500;

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct ClipboardPrefs {
    /// Whether the core includes the start of copied text in its reports.
    pub preview: bool,
    /// The server's settings; on a client the server it connects to decides.
    pub sharing: bool,
    pub limit_mb: u32,
    /// Copying files, on this computer: each computer decides for itself.
    pub files: bool,
    pub files_limit_mb: u32,
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
        files: ini.get("core", "fileClipboard").is_none_or(|v| v == "true"),
        files_limit_mb: ini
            .get("core", "fileClipboardMaxSize")
            .and_then(|v| v.parse::<u32>().ok())
            .filter(|mb| *mb > 0)
            .unwrap_or(DEFAULT_FILES_LIMIT_MB),
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

/// The core reads these when it starts (and a client when it connects): they apply
/// from the next start.
pub fn set_files(enabled: bool, limit_mb: u32) -> Result<ClipboardPrefs, String> {
    if !(1..=100_000).contains(&limit_mb) {
        return Err("El límite tiene que estar entre 1 MB y 100 000 MB.".into());
    }
    let mut ini = IniFile::load(&paths::settings_file()).map_err(|e| e.to_string())?;
    ini.set(
        "core",
        "fileClipboard",
        if enabled { "true" } else { "false" },
    );
    ini.set("core", "fileClipboardMaxSize", &limit_mb.to_string());
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

    #[test]
    fn parses_a_copy_of_files() {
        let mut list = Vec::new();
        let json = r#"{"event":"received","computer":"laptop","dropped":[],"formats":["files"],"size":3502,"files":{"count":5,"folders":1,"bytes":3502,"names":["fotos","informe.pdf","c.txt"],"roots":4}}"#;
        assert!(push(&mut list, json, 1));
        let files = list[0].files.as_ref().unwrap();
        assert_eq!(files.count, 5);
        assert_eq!(files.folders, 1);
        assert_eq!(files.names, vec!["fotos", "informe.pdf", "c.txt"]);
        assert_eq!(files.roots, 4);
        assert_eq!(list[0].formats, vec!["files"]);

        let failed = r#"{"event":"failed","reason":"noUser","detail":"no user is logged on","dropped":[],"formats":["files"]}"#;
        assert!(push(&mut list, failed, 2));
        assert_eq!(list[0].reason.as_deref(), Some("noUser"));
        assert_eq!(list[0].detail.as_deref(), Some("no user is logged on"));
        assert_eq!(list[0].files, None);
    }

    #[test]
    fn progress_and_its_end() {
        let p = parse_progress(
            r#"{"direction":"send","computer":"laptop","count":2,"bytes":150,"done":60}"#,
        )
        .unwrap();
        assert_eq!(p.direction, "send");
        assert_eq!(p.computer.as_deref(), Some("laptop"));
        assert_eq!((p.count, p.bytes, p.done), (2, 150, 60));
        assert_eq!(
            parse_progress(r#"{"direction":"receive","count":1,"bytes":9,"done":0}"#)
                .unwrap()
                .computer,
            None
        );
        assert_eq!(parse_progress(r#"{"done":true}"#), None);
        assert_eq!(parse_progress("garbage"), None);
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

    #[test]
    fn files_default_on_up_to_500_mb() {
        let mut ini = empty_ini();
        let p = read_prefs(&ini);
        assert!(p.files);
        assert_eq!(p.files_limit_mb, 500);

        ini.set("core", "fileClipboard", "false");
        ini.set("core", "fileClipboardMaxSize", "2000");
        let p = read_prefs(&ini);
        assert!(!p.files);
        assert_eq!(p.files_limit_mb, 2000);

        ini.set("core", "fileClipboardMaxSize", "nonsense");
        assert_eq!(read_prefs(&ini).files_limit_mb, 500);
    }
}
