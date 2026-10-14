//! Locations of the Deskflow binaries, settings and IPC endpoints.
//!
//! These mirror the rules in `src/lib/common/Settings.cpp` so the prototype
//! reads the same files as the Qt GUI and the core.

use std::path::{Path, PathBuf};

pub const APP_NAME: &str = "Deskflow";

#[cfg(windows)]
const CORE_BIN: &str = "deskflow-core.exe";
#[cfg(not(windows))]
const CORE_BIN: &str = "deskflow-core";

/// Directory that holds `deskflow-core`.
///
/// Order: `DESKFLOW_DIR`, next to this executable, then the default install location.
pub fn core_dir() -> Option<PathBuf> {
    let mut candidates = Vec::new();
    if let Ok(dir) = std::env::var("DESKFLOW_DIR") {
        candidates.push(PathBuf::from(dir));
    }
    if let Some(dir) = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(Path::to_path_buf))
    {
        candidates.push(dir);
    }
    #[cfg(windows)]
    if let Ok(pf) = std::env::var("ProgramFiles") {
        candidates.push(PathBuf::from(pf).join(APP_NAME));
    }
    #[cfg(target_os = "macos")]
    candidates.push(PathBuf::from("/Applications/Deskflow.app/Contents/MacOS"));
    #[cfg(all(unix, not(target_os = "macos")))]
    candidates.extend([PathBuf::from("/usr/bin"), PathBuf::from("/usr/local/bin")]);

    candidates.into_iter().find(|d| d.join(CORE_BIN).is_file())
}

pub fn core_bin() -> Option<PathBuf> {
    core_dir().map(|d| d.join(CORE_BIN))
}

fn user_dir() -> Option<PathBuf> {
    #[cfg(windows)]
    return std::env::var("APPDATA")
        .ok()
        .map(|d| PathBuf::from(d).join(APP_NAME));
    #[cfg(target_os = "macos")]
    return std::env::var("HOME")
        .ok()
        .map(|h| PathBuf::from(h).join("Library").join(APP_NAME));
    #[cfg(all(unix, not(target_os = "macos")))]
    return std::env::var("HOME")
        .ok()
        .map(|h| PathBuf::from(h).join(".config").join(APP_NAME));
}

fn system_dir() -> PathBuf {
    #[cfg(windows)]
    return PathBuf::from(
        std::env::var("ProgramData").unwrap_or_else(|_| "C:\\ProgramData".into()),
    )
    .join(APP_NAME);
    #[cfg(target_os = "macos")]
    return PathBuf::from("/Library").join(APP_NAME);
    #[cfg(all(unix, not(target_os = "macos")))]
    return PathBuf::from("/etc").join(APP_NAME);
}

/// Portable installs keep their settings next to the binaries (Windows only).
pub fn portable_settings_file() -> Option<PathBuf> {
    if !cfg!(windows) {
        return None;
    }
    let file = core_dir()?
        .join("settings")
        .join(format!("{APP_NAME}.conf"));
    file.is_file().then_some(file)
}

/// The settings file the core and the Qt GUI would load.
pub fn settings_file() -> PathBuf {
    if let Some(portable) = portable_settings_file() {
        return portable;
    }
    #[cfg(not(windows))]
    if let Ok(xdg) = std::env::var("XDG_CONFIG_HOME") {
        if !xdg.is_empty() {
            return PathBuf::from(xdg)
                .join(APP_NAME)
                .join(format!("{APP_NAME}.conf"));
        }
    }
    let user = user_dir().map(|d| d.join(format!("{APP_NAME}.conf")));
    let system = system_dir().join(format!("{APP_NAME}.conf"));
    match user {
        Some(u) if u.is_file() => u,
        _ if system.is_file() => system,
        Some(u) => u,
        None => system,
    }
}

pub fn is_portable() -> bool {
    portable_settings_file().is_some()
}

/// Mirrors `Settings::settingsPath()`: on non-portable Windows installs this is the system dir.
pub fn settings_dir() -> PathBuf {
    if cfg!(windows) && !is_portable() {
        return system_dir();
    }
    settings_file()
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_else(system_dir)
}

/// Full path of a Qt `QLocalServer` endpoint with the given name.
pub fn ipc_endpoint(name: &str) -> String {
    #[cfg(windows)]
    return format!(r"\\.\pipe\{name}");
    #[cfg(not(windows))]
    return std::env::temp_dir()
        .join(name)
        .to_string_lossy()
        .into_owned();
}

pub const CORE_IPC: &str = "deskflow-core";
pub const DAEMON_IPC: &str = "deskflow-daemon";

/// Qt's state file (`Settings::m_stateSettings`), where the Wayland portal tokens live.
/// Only Linux uses those keys, so other platforms report none.
pub fn state_file() -> Option<PathBuf> {
    if !cfg!(all(unix, not(target_os = "macos"))) {
        return None;
    }
    let base = std::env::var("XDG_STATE_HOME")
        .ok()
        .filter(|v| !v.is_empty())
        .map(PathBuf::from)
        .or_else(|| {
            std::env::var("HOME")
                .ok()
                .map(|h| PathBuf::from(h).join(".local").join("state"))
        })?;
    Some(base.join(format!("{APP_NAME}.state")))
}

/// Settings file of the Windows service (`deskflow-daemon` runs as SYSTEM, so its Qt
/// settings live in the system profile). It remembers which config to start at boot.
pub fn daemon_settings_file() -> Option<PathBuf> {
    if !cfg!(windows) {
        return None;
    }
    let root = std::env::var("SystemRoot").unwrap_or_else(|_| r"C:\Windows".into());
    Some(
        PathBuf::from(root)
            .join("System32")
            .join("config")
            .join("systemprofile")
            .join("AppData")
            .join("Roaming")
            .join(APP_NAME)
            .join(format!("{APP_NAME}.conf")),
    )
}
