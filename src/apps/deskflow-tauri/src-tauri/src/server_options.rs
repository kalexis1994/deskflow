//! Global server options in `[server]`, as written by the Qt GUI's server dialog
//! (`ServerConfigDialog`) and read by the core in `Config::read` (`src/lib/server/Config.cpp`).

use serde::{Deserialize, Serialize};

use crate::paths;
use crate::settings::IniFile;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Protocol {
    /// Deskflow, Barrier, Input Leap and Synergy 1.
    Barrier,
    /// Synergy 3.
    Synergy,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ServerOptions {
    pub clipboard: bool,
    /// Largest clipboard to share, in MiB.
    pub clipboard_size_mb: u32,
    pub switch_delay: bool,
    pub switch_delay_ms: u32,
    pub double_tap: bool,
    pub double_tap_ms: u32,
    /// Scroll Lock locks the cursor to the current computer (the Qt key is the inverse).
    pub lock_key: bool,
    pub start_locked: bool,
    pub relative_moves: bool,
    pub heartbeat: bool,
    pub heartbeat_ms: u32,
    pub protocol: Protocol,
    pub win32_keep_foreground: bool,
}

impl Default for ServerOptions {
    /// Same defaults as `Settings::defaultValue` in `src/lib/common/Settings.cpp`.
    fn default() -> Self {
        Self {
            clipboard: true,
            clipboard_size_mb: 3,
            switch_delay: false,
            switch_delay_ms: 250,
            double_tap: false,
            double_tap_ms: 250,
            lock_key: true,
            start_locked: false,
            relative_moves: false,
            heartbeat: false,
            heartbeat_ms: 5000,
            protocol: Protocol::Barrier,
            win32_keep_foreground: true,
        }
    }
}

const S: &str = "server";

pub fn read(ini: &IniFile) -> ServerOptions {
    let d = ServerOptions::default();
    let flag = |key: &str, default: bool| ini.get(S, key).map_or(default, |v| v == "true");
    let num = |key: &str, default: u32| {
        ini.get(S, key)
            .and_then(|v| v.parse().ok())
            .unwrap_or(default)
    };
    ServerOptions {
        clipboard: flag("enableClipboard", d.clipboard),
        clipboard_size_mb: num("clipboardSize", d.clipboard_size_mb),
        switch_delay: flag("enableSwitchDelay", d.switch_delay),
        switch_delay_ms: num("switchDelay", d.switch_delay_ms),
        double_tap: flag("enableSwitchDoubleTap", d.double_tap),
        double_tap_ms: num("switchDoubleTap", d.double_tap_ms),
        lock_key: !flag("disableLockToComputer", !d.lock_key),
        start_locked: flag("defaultLockToComputerState", d.start_locked),
        relative_moves: flag("relativeMouseMoves", d.relative_moves),
        heartbeat: flag("enableHeartbeat", d.heartbeat),
        heartbeat_ms: num("heartbeat", d.heartbeat_ms),
        protocol: match ini.get(S, "protocol").as_deref() {
            Some("synergy") => Protocol::Synergy,
            _ => Protocol::Barrier,
        },
        win32_keep_foreground: flag("win32KeepForeground", d.win32_keep_foreground),
    }
}

/// Same limits as the spin boxes of the Qt dialog.
pub fn validate(o: &ServerOptions) -> Result<(), String> {
    let check = |ok: bool, msg: &str| if ok { Ok(()) } else { Err(msg.to_owned()) };
    check(
        o.clipboard_size_mb <= 10_000,
        "El límite del portapapeles puede ser de hasta 10000 MB.",
    )?;
    check(
        !o.clipboard || o.clipboard_size_mb >= 1,
        "Si compartís el portapapeles, el límite tiene que ser de al menos 1 MB.",
    )?;
    check(
        (10..=10_000).contains(&o.switch_delay_ms),
        "La espera para cambiar de pantalla va de 10 a 10000 ms.",
    )?;
    check(
        (10..=10_000).contains(&o.double_tap_ms),
        "El tiempo del doble toque va de 10 a 10000 ms.",
    )?;
    check(
        (1_000..=30_000).contains(&o.heartbeat_ms),
        "El intervalo de chequeo va de 1 a 30 segundos.",
    )?;
    Ok(())
}

pub fn write(ini: &mut IniFile, o: &ServerOptions) {
    let b = |v: bool| if v { "true" } else { "false" };
    ini.set(S, "enableClipboard", b(o.clipboard));
    ini.set(S, "clipboardSize", &o.clipboard_size_mb.to_string());
    ini.set(S, "enableSwitchDelay", b(o.switch_delay));
    ini.set(S, "switchDelay", &o.switch_delay_ms.to_string());
    ini.set(S, "enableSwitchDoubleTap", b(o.double_tap));
    ini.set(S, "switchDoubleTap", &o.double_tap_ms.to_string());
    ini.set(S, "disableLockToComputer", b(!o.lock_key));
    ini.set(S, "defaultLockToComputerState", b(o.start_locked));
    ini.set(S, "relativeMouseMoves", b(o.relative_moves));
    ini.set(S, "enableHeartbeat", b(o.heartbeat));
    ini.set(S, "heartbeat", &o.heartbeat_ms.to_string());
    ini.set(
        S,
        "protocol",
        if o.protocol == Protocol::Synergy {
            "synergy"
        } else {
            "barrier"
        },
    );
    ini.set(S, "win32KeepForeground", b(o.win32_keep_foreground));
}

/// The Wayland portal grants clipboard access per session; like the Qt GUI, forget
/// it when clipboard sharing is toggled so the next start asks again.
fn reset_portal_clipboard(ini: &mut IniFile) {
    let stale = |k: &str| k == "xdpRestoreToken" || k == "xdpClipboardRetried";
    ini.remove_keys(S, stale);
    if let Some(state) = paths::state_file()
        && let Ok(mut f) = IniFile::load(&state)
        && f.entries(S).iter().any(|(k, _)| stale(k))
    {
        f.remove_keys(S, stale);
        let _ = f.save();
    }
}

pub fn load() -> Result<ServerOptions, String> {
    let ini = IniFile::load(&paths::settings_file()).map_err(|e| e.to_string())?;
    Ok(read(&ini))
}

pub fn save(o: &ServerOptions) -> Result<ServerOptions, String> {
    validate(o)?;
    let mut ini = IniFile::load(&paths::settings_file()).map_err(|e| e.to_string())?;
    if read(&ini).clipboard != o.clipboard {
        reset_portal_clipboard(&mut ini);
    }
    write(&mut ini, o);
    ini.save().map_err(|e| format!("No se pudo guardar: {e}"))?;
    Ok(read(&ini))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ini(text: &str) -> IniFile {
        let path = std::env::temp_dir().join(format!(
            "server-options-{}-{}.conf",
            std::process::id(),
            text.len()
        ));
        std::fs::write(&path, text).unwrap();
        let f = IniFile::load(&path).unwrap();
        let _ = std::fs::remove_file(&path);
        f
    }

    #[test]
    fn defaults_match_qt() {
        assert_eq!(read(&ini("")), ServerOptions::default());
    }

    #[test]
    fn reads_the_users_qt_settings() {
        // the [server] section of a real Deskflow.conf
        let f = ini(
            "[server]\nenableClipboard=true\nclipboardSize=3\ndefaultLockToComputerState=false\ndisableLockToComputer=false\n\
             enableHeartbeat=false\nenableSwitchDelay=false\nenableSwitchDoubleTap=false\nheartbeat=5000\ngridWidth=5\n\
             gridHeight=3\nprotocol=barrier\nrelativeMouseMoves=false\nswitchDelay=250\nswitchDoubleTap=250\n\
             win32KeepForeground=false\n",
        );
        let o = read(&f);
        assert!(o.clipboard && o.lock_key && !o.win32_keep_foreground);
        assert_eq!(o.protocol, Protocol::Barrier);
    }

    #[test]
    fn write_then_read_roundtrips_and_keeps_other_keys() {
        let mut f = ini("[server]\ngridWidth=5\nprotocol=barrier\n");
        let o = ServerOptions {
            clipboard: false,
            switch_delay: true,
            switch_delay_ms: 400,
            double_tap: true,
            lock_key: false,
            start_locked: true,
            heartbeat: true,
            heartbeat_ms: 3000,
            protocol: Protocol::Synergy,
            ..Default::default()
        };
        write(&mut f, &o);
        assert_eq!(read(&f), o);
        assert_eq!(f.get(S, "disableLockToComputer").as_deref(), Some("true"));
        assert_eq!(f.get(S, "gridWidth").as_deref(), Some("5"));
    }

    #[test]
    fn validation_uses_the_qt_limits() {
        assert!(validate(&ServerOptions::default()).is_ok());
        assert!(
            validate(&ServerOptions {
                heartbeat_ms: 500,
                ..Default::default()
            })
            .is_err()
        );
        assert!(
            validate(&ServerOptions {
                switch_delay_ms: 5,
                ..Default::default()
            })
            .is_err()
        );
        assert!(
            validate(&ServerOptions {
                clipboard_size_mb: 0,
                ..Default::default()
            })
            .is_err()
        );
        assert!(
            validate(&ServerOptions {
                clipboard: false,
                clipboard_size_mb: 0,
                ..Default::default()
            })
            .is_ok()
        );
    }

    #[test]
    fn accepts_the_payload_the_ui_sends() {
        let json = r#"{"clipboard":true,"clipboardSizeMb":3,"switchDelay":false,"switchDelayMs":250,
            "doubleTap":true,"doubleTapMs":300,"lockKey":true,"startLocked":false,"relativeMoves":false,
            "heartbeat":false,"heartbeatMs":5000,"protocol":"synergy","win32KeepForeground":true}"#;
        let o: ServerOptions = serde_json::from_str(json).unwrap();
        assert!(o.double_tap && o.double_tap_ms == 300 && o.protocol == Protocol::Synergy);
    }
}
