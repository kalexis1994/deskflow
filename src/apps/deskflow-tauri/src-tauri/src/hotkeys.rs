//! Server hotkeys.
//!
//! Stored like the Qt GUI does (`internalConfig/hotkeys`, see `src/lib/common/Hotkey.cpp`,
//! `Action.cpp` and `KeySequence.cpp`) so both GUIs share them, and written to the
//! `section: options` of the server config in the syntax `Config::readSectionOptions`
//! parses (`src/lib/server/Config.cpp`).
//!
//! Keys are kept as Qt key codes, which is what the settings file holds. Values below
//! are from Qt's `qnamespace.h`.

use std::collections::HashSet;

use serde::{Deserialize, Serialize};

use crate::i18n;
use crate::settings::IniFile;

pub const SHIFT: i64 = 0x0200_0000;
pub const CONTROL: i64 = 0x0400_0000;
pub const ALT: i64 = 0x0800_0000;
pub const META: i64 = 0x1000_0000;
const KEYPAD: i64 = 0x2000_0000;
const KEY_F1: i64 = 0x0100_0030;
const KEY_F35: i64 = 0x0100_0052;

/// Qt key code to core key name; the same set the Qt GUI can record.
const SPECIAL_KEYS: &[(i64, &str)] = &[
    (0x20, "Space"),
    (0x0100_0000, "Escape"),
    (0x0100_0001, "Tab"),
    (0x0100_0002, "LeftTab"),
    (0x0100_0003, "BackSpace"),
    (0x0100_0004, "Return"),
    (0x0100_0005, "KP_Enter"),
    (0x0100_0006, "Insert"),
    (0x0100_0007, "Delete"),
    (0x0100_0008, "Pause"),
    (0x0100_0009, "Print"),
    (0x0100_000a, "SysReq"),
    (0x0100_000b, "Clear"),
    (0x0100_0010, "Home"),
    (0x0100_0011, "End"),
    (0x0100_0012, "Left"),
    (0x0100_0013, "Up"),
    (0x0100_0014, "Right"),
    (0x0100_0015, "Down"),
    (0x0100_0016, "PageUp"),
    (0x0100_0017, "PageDown"),
    (0x0100_0024, "CapsLock"),
    (0x0100_0025, "NumLock"),
    (0x0100_0026, "ScrollLock"),
    (0x0100_0055, "Menu"),
    (0x0100_0058, "Help"),
    (0x0100_0061, "WWWBack"),
    (0x0100_0062, "WWWForward"),
    (0x0100_0063, "WWWStop"),
    (0x0100_0064, "WWWRefresh"),
    (0x0100_0070, "AudioDown"),
    (0x0100_0071, "AudioMute"),
    (0x0100_0072, "AudioUp"),
    (0x0100_0080, "AudioPlay"),
    (0x0100_0081, "AudioStop"),
    (0x0100_0082, "AudioPrev"),
    (0x0100_0083, "AudioNext"),
    (0x0100_0090, "WWWHome"),
    (0x0100_0091, "WWWFavorites"),
    (0x0100_0092, "WWWSearch"),
    (0x0100_0093, "Sleep"),
    (0x0100_00a0, "AppMail"),
    (0x0100_00a1, "AppMedia"),
    (0x0100_00a2, "AppUser1"),
    (0x0100_00a3, "AppUser2"),
    (0x0101_0000, "Select"),
];

/// Punctuation goes by name: a literal `)` or `,` would break the config syntax.
const PUNCTUATION: &[(char, &str)] = &[
    ('!', "Exclaim"),
    ('"', "DoubleQuote"),
    ('#', "Number"),
    ('$', "Dollar"),
    ('%', "Percent"),
    ('&', "Ampersand"),
    ('\'', "Apostrophe"),
    ('(', "ParenthesisL"),
    (')', "ParenthesisR"),
    ('*', "Asterisk"),
    ('+', "Plus"),
    (',', "Comma"),
    ('-', "Minus"),
    ('.', "Period"),
    ('/', "Slash"),
    (':', "Colon"),
    (';', "Semicolon"),
    ('<', "Less"),
    ('=', "Equal"),
    ('>', "Greater"),
    ('?', "Question"),
    ('@', "At"),
    ('[', "BracketL"),
    ('\\', "Backslash"),
    (']', "BracketR"),
    ('^', "Circumflex"),
    ('_', "Underscore"),
    ('`', "Grave"),
    ('{', "BraceL"),
    ('|', "Bar"),
    ('}', "BraceR"),
    ('~', "Tilde"),
];

pub fn is_modifier(key: i64) -> bool {
    key == SHIFT || key == CONTROL || key == ALT || key == META
}

fn is_mouse_button(key: i64) -> bool {
    key > 0 && key < 0x20
}

/// Core name of one entry of a Qt key sequence, as `KeySequence::keyToString` would
/// write it (except punctuation, see `PUNCTUATION`).
pub fn key_name(key: i64) -> Option<String> {
    if key <= 0 {
        return None;
    }
    if is_mouse_button(key) {
        // Qt::LeftButton = 1, RightButton = 2, MiddleButton = 4
        return Some(
            match key {
                1 => "1",
                2 => "2",
                4 => "3",
                _ => "4",
            }
            .into(),
        );
    }
    for (flag, name) in [
        (SHIFT, "Shift"),
        (CONTROL, "Control"),
        (ALT, "Alt"),
        (META, "Meta"),
    ] {
        if key & flag != 0 {
            return Some(name.into());
        }
    }
    let key = key & !KEYPAD;
    if let Some((_, name)) = SPECIAL_KEYS.iter().find(|(k, _)| *k == key) {
        return Some((*name).into());
    }
    if key < 0x80 {
        let c = char::from(key as u8);
        if c.is_ascii_alphanumeric() {
            return Some(c.to_ascii_lowercase().to_string());
        }
        return PUNCTUATION
            .iter()
            .find(|(p, _)| *p == c)
            .map(|(_, n)| (*n).into());
    }
    if (KEY_F1..=KEY_F35).contains(&key) {
        return Some(format!("F{}", key - KEY_F1 + 1));
    }
    if key < 0x10000 {
        let c = char::from_u32(key as u32)?.to_lowercase().next()?;
        return Some(format!("\\u{:04x}", c as u32));
    }
    None
}

/// `Control+Alt+Left`. None if any key cannot be expressed or there is no real key.
pub fn sequence_text(keys: &[i64]) -> Option<String> {
    let last = *keys.last()?;
    if is_modifier(last) {
        return None;
    }
    let names: Option<Vec<String>> = keys.iter().map(|&k| key_name(k)).collect();
    Some(names?.join("+"))
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum ActionKind {
    KeyDown,
    KeyUp,
    Keystroke,
    SwitchToScreen,
    SwitchInDirection,
    SwitchToNextScreen,
    LockCursorToScreen,
    RestartServer,
    MouseDown,
    MouseUp,
    Mousebutton,
}

/// Same order as `Action::Type`, which is what the settings file stores.
const KINDS: [ActionKind; 11] = [
    ActionKind::KeyDown,
    ActionKind::KeyUp,
    ActionKind::Keystroke,
    ActionKind::SwitchToScreen,
    ActionKind::SwitchInDirection,
    ActionKind::SwitchToNextScreen,
    ActionKind::LockCursorToScreen,
    ActionKind::RestartServer,
    ActionKind::MouseDown,
    ActionKind::MouseUp,
    ActionKind::Mousebutton,
];

impl ActionKind {
    fn index(self) -> usize {
        KINDS.iter().position(|k| *k == self).unwrap_or(2)
    }
    fn sends_keys(self) -> bool {
        matches!(
            self,
            ActionKind::KeyDown | ActionKind::KeyUp | ActionKind::Keystroke
        )
    }
    fn sends_mouse(self) -> bool {
        matches!(
            self,
            ActionKind::MouseDown | ActionKind::MouseUp | ActionKind::Mousebutton
        )
    }
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum Direction {
    #[default]
    Left,
    Right,
    Up,
    Down,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum LockMode {
    #[default]
    Toggle,
    On,
    Off,
}

const DIRECTIONS: [(Direction, &str); 4] = [
    (Direction::Left, "left"),
    (Direction::Right, "right"),
    (Direction::Up, "up"),
    (Direction::Down, "down"),
];
const LOCK_MODES: [(LockMode, &str); 3] = [
    (LockMode::Toggle, "toggle"),
    (LockMode::On, "on"),
    (LockMode::Off, "off"),
];

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Action {
    pub kind: ActionKind,
    /// Keys or mouse button to send, for the keystroke and mouse kinds.
    #[serde(default)]
    pub keys: Vec<i64>,
    /// Computers where the keys may go; empty means any. The server sends them only to
    /// the one with the mouse, when it is one of these.
    #[serde(default)]
    pub computers: Vec<String>,
    /// Destination of `switchToScreen`.
    #[serde(default)]
    pub target: String,
    #[serde(default)]
    pub direction: Direction,
    #[serde(default)]
    pub lock_mode: LockMode,
    /// Run when the hotkey is released instead of pressed.
    #[serde(default)]
    pub on_release: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Hotkey {
    pub keys: Vec<i64>,
    pub actions: Vec<Action>,
}

// ---- settings file (Qt's QSettings arrays, 1-based) -------------------------------------

const SECTION: &str = "internalConfig";

fn size(ini: &IniFile, prefix: &str) -> usize {
    ini.get(SECTION, &format!("{prefix}size"))
        .and_then(|v| v.parse().ok())
        .unwrap_or(0)
}

fn read_keys(ini: &IniFile, prefix: &str) -> Vec<i64> {
    (1..=size(ini, prefix))
        .filter_map(|i| {
            ini.get(SECTION, &format!("{prefix}{i}\\key"))
                .and_then(|v| v.parse().ok())
        })
        .filter(|&k| k != 0)
        .collect()
}

pub fn read(ini: &IniFile) -> Vec<Hotkey> {
    (1..=size(ini, "hotkeys\\"))
        .map(|i| {
            let p = format!("hotkeys\\{i}\\");
            let actions = (1..=size(ini, &format!("{p}actions\\")))
                .map(|j| {
                    let q = format!("{p}actions\\{j}\\");
                    let get = |k: &str| ini.get(SECTION, &format!("{q}{k}"));
                    let index = |k: &str| get(k).and_then(|v| v.parse::<usize>().ok()).unwrap_or(0);
                    let has_computers = get("hasScreens").is_some_and(|v| v == "true");
                    let names = format!("{q}typeScreenNames\\");
                    Action {
                        kind: KINDS
                            .get(index("type"))
                            .copied()
                            .unwrap_or(ActionKind::Keystroke),
                        keys: read_keys(ini, &format!("{q}keys\\")),
                        computers: if has_computers {
                            (1..=size(ini, &names))
                                .filter_map(|k| {
                                    ini.get(SECTION, &format!("{names}{k}\\typeScreenName"))
                                })
                                .filter(|n| !n.is_empty())
                                .collect()
                        } else {
                            Vec::new()
                        },
                        target: get("switchScreenName").unwrap_or_default(),
                        direction: DIRECTIONS
                            .get(index("switchInDirection"))
                            .map_or_else(Direction::default, |d| d.0),
                        lock_mode: LOCK_MODES
                            .get(index("lockCursorToScreen"))
                            .map_or_else(LockMode::default, |m| m.0),
                        on_release: get("activeOnRelease").is_some_and(|v| v == "true"),
                    }
                })
                .collect();
            Hotkey {
                keys: read_keys(ini, &format!("{p}keys\\")),
                actions,
            }
        })
        .collect()
}

pub fn write(ini: &mut IniFile, hotkeys: &[Hotkey]) {
    ini.remove_keys(SECTION, |k| k.starts_with("hotkeys\\"));
    let b = |v: bool| if v { "true" } else { "false" };
    let mut set = |key: String, value: &str| ini.set(SECTION, &key, value);
    for (i, hk) in hotkeys.iter().enumerate() {
        let p = format!("hotkeys\\{}\\", i + 1);
        for (k, key) in hk.keys.iter().enumerate() {
            set(format!("{p}keys\\{}\\key", k + 1), &key.to_string());
        }
        set(format!("{p}keys\\size"), &hk.keys.len().to_string());
        for (j, a) in hk.actions.iter().enumerate() {
            let q = format!("{p}actions\\{}\\", j + 1);
            for (k, key) in a.keys.iter().enumerate() {
                set(format!("{q}keys\\{}\\key", k + 1), &key.to_string());
            }
            set(format!("{q}keys\\size"), &a.keys.len().to_string());
            set(format!("{q}type"), &a.kind.index().to_string());
            for (k, name) in a.computers.iter().enumerate() {
                set(
                    format!("{q}typeScreenNames\\{}\\typeScreenName", k + 1),
                    name,
                );
            }
            set(
                format!("{q}typeScreenNames\\size"),
                &a.computers.len().to_string(),
            );
            set(format!("{q}switchScreenName"), &a.target);
            let dir = DIRECTIONS
                .iter()
                .position(|d| d.0 == a.direction)
                .unwrap_or(0);
            set(format!("{q}switchInDirection"), &dir.to_string());
            let lock = LOCK_MODES
                .iter()
                .position(|m| m.0 == a.lock_mode)
                .unwrap_or(0);
            set(format!("{q}lockCursorToScreen"), &lock.to_string());
            set(format!("{q}activeOnRelease"), b(a.on_release));
            set(format!("{q}hasScreens"), b(!a.computers.is_empty()));
            set(
                format!("{q}restartServer"),
                b(a.kind == ActionKind::RestartServer),
            );
        }
        set(format!("{p}actions\\size"), &hk.actions.len().to_string());
    }
    set("hotkeys\\size".into(), &hotkeys.len().to_string());
}

// ---- server config ---------------------------------------------------------------------

/// One action as the core parses it, or None if it cannot be written safely
/// (unknown computer, missing keys...): a single bad action makes the core reject
/// the whole config, so those are left out.
fn action_text(a: &Action, known: &HashSet<String>) -> Option<String> {
    let name = match a.kind {
        ActionKind::KeyDown => "keyDown",
        ActionKind::KeyUp => "keyUp",
        ActionKind::Keystroke => "keystroke",
        ActionKind::SwitchToScreen => "switchToScreen",
        ActionKind::SwitchInDirection => "switchInDirection",
        ActionKind::SwitchToNextScreen => "switchToNextScreen",
        ActionKind::LockCursorToScreen => "lockCursorToScreen",
        ActionKind::RestartServer => "restartServer",
        ActionKind::MouseDown => "mouseDown",
        ActionKind::MouseUp => "mouseUp",
        ActionKind::Mousebutton => "mousebutton",
    };
    if a.kind.sends_keys() {
        if a.keys.iter().any(|&k| is_mouse_button(k)) {
            return None;
        }
        let keys = sequence_text(&a.keys)?;
        let computers: Vec<&str> = a
            .computers
            .iter()
            .filter(|c| known.contains(*c))
            .map(String::as_str)
            .collect();
        if !a.computers.is_empty() && computers.is_empty() {
            // every computer it was for is gone; working on any instead would surprise
            return None;
        }
        let to = if computers.is_empty() {
            "*".to_owned()
        } else {
            computers.join(":")
        };
        return Some(format!("{name}({keys},{to})"));
    }
    if a.kind.sends_mouse() {
        return a
            .keys
            .last()
            .filter(|&&k| is_mouse_button(k))
            .and(sequence_text(&a.keys))
            .map(|s| format!("{name}({s})"));
    }
    match a.kind {
        ActionKind::SwitchToScreen => known
            .contains(&a.target)
            .then(|| format!("{name}({})", a.target)),
        ActionKind::SwitchInDirection => Some(format!(
            "{name}({})",
            DIRECTIONS
                .iter()
                .find(|d| d.0 == a.direction)
                .map_or("left", |d| d.1)
        )),
        ActionKind::LockCursorToScreen => Some(format!(
            "{name}({})",
            LOCK_MODES
                .iter()
                .find(|m| m.0 == a.lock_mode)
                .map_or("toggle", |m| m.1)
        )),
        _ => Some(name.to_owned()),
    }
}

/// Body of `section: options`. `known` holds every computer name and alias.
pub fn options_text(hotkeys: &[Hotkey], known: &HashSet<String>) -> String {
    let mut out = String::new();
    for hk in hotkeys {
        let Some(trigger) = sequence_text(&hk.keys) else {
            continue;
        };
        let condition = if hk.keys.iter().any(|&k| is_mouse_button(k)) {
            "mousebutton"
        } else {
            "keystroke"
        };
        let (mut press, mut release) = (Vec::new(), Vec::new());
        for a in &hk.actions {
            if let Some(text) = action_text(a, known) {
                if a.on_release {
                    release.push(text)
                } else {
                    press.push(text)
                }
            }
        }
        if press.is_empty() && release.is_empty() {
            continue;
        }
        out.push_str(&format!("\t{condition}({trigger}) = {}", press.join(", ")));
        if !release.is_empty() {
            out.push_str(&format!(" ; {}", release.join(", ")));
        }
        out.push('\n');
    }
    out
}

/// Follows renames and drops references to computers that no longer exist.
/// Returns true if anything changed.
pub fn retarget(
    hotkeys: &mut [Hotkey],
    renames: &[(String, String)],
    known: &HashSet<String>,
) -> bool {
    let rename = |n: &str| {
        renames
            .iter()
            .find(|(from, _)| from == n)
            .map_or(n.to_owned(), |(_, to)| to.clone())
    };
    let before = hotkeys.to_vec();
    for hk in hotkeys.iter_mut() {
        hk.actions.retain_mut(|a| {
            a.computers = a
                .computers
                .iter()
                .map(|c| rename(c))
                .filter(|c| known.contains(c))
                .collect();
            if a.kind == ActionKind::SwitchToScreen {
                a.target = rename(&a.target);
                return known.contains(&a.target);
            }
            true
        });
    }
    before != hotkeys
}

pub fn validate(hotkeys: &[Hotkey], known: &HashSet<String>) -> Result<(), String> {
    for (i, hk) in hotkeys.iter().enumerate() {
        let n = i + 1;
        if sequence_text(&hk.keys).is_none() {
            return Err(i18n::tf("error.hotkeys.noKey", &[("n", &n)]));
        }
        for a in &hk.actions {
            if a.kind.sends_keys()
                && (sequence_text(&a.keys).is_none() || a.keys.iter().any(|&k| is_mouse_button(k)))
            {
                return Err(i18n::tf("error.hotkeys.badSendKeys", &[("n", &n)]));
            }
            if a.kind == ActionKind::SwitchToScreen && !known.contains(&a.target) {
                return Err(i18n::tf(
                    "error.hotkeys.unknownTarget",
                    &[("n", &n), ("name", &a.target)],
                ));
            }
            if let Some(c) = a.computers.iter().find(|c| !known.contains(*c)) {
                return Err(i18n::tf(
                    "error.hotkeys.unknownComputer",
                    &[("n", &n), ("name", c)],
                ));
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    const KEY_LEFT: i64 = 0x0100_0012;
    const KEY_A: i64 = 0x41;

    fn known(names: &[&str]) -> HashSet<String> {
        names.iter().map(|s| s.to_string()).collect()
    }

    fn action(kind: ActionKind) -> Action {
        Action {
            kind,
            keys: vec![],
            computers: vec![],
            target: String::new(),
            direction: Direction::Left,
            lock_mode: LockMode::Toggle,
            on_release: false,
        }
    }

    #[test]
    fn key_names_match_the_qt_gui() {
        assert_eq!(
            sequence_text(&[CONTROL, ALT, KEY_LEFT]).as_deref(),
            Some("Control+Alt+Left")
        );
        assert_eq!(sequence_text(&[SHIFT, KEY_A]).as_deref(), Some("Shift+a"));
        assert_eq!(
            sequence_text(&[META, 0x0100_0030 + 4]).as_deref(),
            Some("Meta+F5")
        );
        assert_eq!(
            sequence_text(&[CONTROL, 0x2c]).as_deref(),
            Some("Control+Comma")
        );
        assert_eq!(
            sequence_text(&[CONTROL, 0x29]).as_deref(),
            Some("Control+ParenthesisR"),
            "never a raw )"
        );
        assert_eq!(sequence_text(&[0xf1]).as_deref(), Some("\\u00f1"), "ñ");
        assert_eq!(
            sequence_text(&[CONTROL, 0x0100_0005 | KEYPAD]).as_deref(),
            Some("Control+KP_Enter")
        );
        assert_eq!(
            sequence_text(&[CONTROL, 1]).as_deref(),
            Some("Control+1"),
            "left mouse button"
        );
        assert_eq!(
            sequence_text(&[CONTROL, ALT]),
            None,
            "modifiers alone are not a hotkey"
        );
        assert_eq!(sequence_text(&[]), None);
    }

    #[test]
    fn options_text_matches_core_syntax() {
        let mut switch = action(ActionKind::SwitchToScreen);
        switch.target = "laptop".into();
        let mut keys = action(ActionKind::Keystroke);
        keys.keys = vec![CONTROL, KEY_A];
        keys.computers = vec!["laptop".into(), "mac".into()];
        let mut lock = action(ActionKind::LockCursorToScreen);
        lock.lock_mode = LockMode::On;
        let mut unlock = action(ActionKind::LockCursorToScreen);
        unlock.lock_mode = LockMode::Off;
        unlock.on_release = true;
        let hotkeys = vec![
            Hotkey {
                keys: vec![CONTROL, ALT, KEY_LEFT],
                actions: vec![switch, keys],
            },
            Hotkey {
                keys: vec![0x0100_0026],
                actions: vec![lock, unlock, action(ActionKind::SwitchToNextScreen)],
            },
            Hotkey {
                keys: vec![CONTROL, KEY_A],
                actions: vec![],
            },
        ];
        assert_eq!(
            options_text(&hotkeys, &known(&["srv", "laptop", "mac"])),
            "\tkeystroke(Control+Alt+Left) = switchToScreen(laptop), keystroke(Control+a,laptop:mac)\n\
             \tkeystroke(ScrollLock) = lockCursorToScreen(on), switchToNextScreen ; lockCursorToScreen(off)\n"
        );
    }

    /// The core turns Ctrl+Alt+Delete received by a Windows client into SendSAS
    /// (`Computer::keyDown` -> `MSWindowsKeyState::fakeCtrlAltDel`).
    #[test]
    fn remote_ctrl_alt_del_preset() {
        let mut send = action(ActionKind::Keystroke);
        send.keys = vec![CONTROL, ALT, 0x0100_0007];
        send.computers = vec!["alexis-server".into()];
        let hk = vec![Hotkey {
            keys: vec![CONTROL, ALT, 0x0100_0011],
            actions: vec![send],
        }];
        assert_eq!(
            options_text(&hk, &known(&["escritorio", "alexis-server"])),
            "\tkeystroke(Control+Alt+End) = keystroke(Control+Alt+Delete,alexis-server)\n"
        );
    }

    #[test]
    fn unknown_computers_are_left_out_instead_of_breaking_the_config() {
        let mut gone = action(ActionKind::SwitchToScreen);
        gone.target = "old-pc".into();
        let mut partial = action(ActionKind::Keystroke);
        partial.keys = vec![KEY_A];
        partial.computers = vec!["old-pc".into(), "laptop".into()];
        let hk = vec![Hotkey {
            keys: vec![CONTROL, KEY_A],
            actions: vec![gone, partial],
        }];
        assert_eq!(
            options_text(&hk, &known(&["laptop"])),
            "\tkeystroke(Control+a) = keystroke(a,laptop)\n"
        );
        assert!(validate(&hk, &known(&["laptop"])).is_err());
    }

    #[test]
    fn retarget_follows_renames_and_drops_removed_computers() {
        let mut to_old = action(ActionKind::SwitchToScreen);
        to_old.target = "old".into();
        let mut to_gone = action(ActionKind::SwitchToScreen);
        to_gone.target = "gone".into();
        let mut send = action(ActionKind::Keystroke);
        send.keys = vec![KEY_A];
        send.computers = vec!["old".into(), "gone".into()];
        let mut hk = vec![Hotkey {
            keys: vec![CONTROL, KEY_A],
            actions: vec![to_old, to_gone, send],
        }];
        assert!(retarget(
            &mut hk,
            &[("old".into(), "new".into())],
            &known(&["srv", "new"])
        ));
        assert_eq!(hk[0].actions.len(), 2);
        assert_eq!(hk[0].actions[0].target, "new");
        assert_eq!(hk[0].actions[1].computers, ["new"]);
        assert!(
            !retarget(&mut hk, &[], &known(&["srv", "new"])),
            "nothing left to change"
        );
    }

    #[test]
    fn settings_roundtrip_in_qt_format() {
        let path = std::env::temp_dir().join(format!("hotkeys-test-{}.conf", std::process::id()));
        std::fs::write(
            &path,
            "[internalConfig]\nhotkeys\\size=0\nscreens\\1\\name=srv\nscreens\\size=1\n",
        )
        .unwrap();
        let mut ini = IniFile::load(&path).unwrap();
        let _ = std::fs::remove_file(&path);
        assert!(read(&ini).is_empty());

        let mut send = action(ActionKind::KeyDown);
        send.keys = vec![SHIFT, KEY_A];
        send.computers = vec!["laptop".into()];
        let mut dir = action(ActionKind::SwitchInDirection);
        dir.direction = Direction::Down;
        dir.on_release = true;
        let hotkeys = vec![Hotkey {
            keys: vec![CONTROL, ALT, KEY_LEFT],
            actions: vec![send, dir, action(ActionKind::RestartServer)],
        }];
        write(&mut ini, &hotkeys);
        assert_eq!(read(&ini), hotkeys);

        // exactly the keys Qt's QSettings arrays use
        assert_eq!(
            ini.get(SECTION, "hotkeys\\1\\keys\\2\\key").as_deref(),
            Some("134217728")
        );
        assert_eq!(
            ini.get(SECTION, "hotkeys\\1\\actions\\1\\type").as_deref(),
            Some("0")
        );
        assert_eq!(
            ini.get(
                SECTION,
                "hotkeys\\1\\actions\\1\\typeScreenNames\\1\\typeScreenName"
            )
            .as_deref(),
            Some("laptop")
        );
        assert_eq!(
            ini.get(SECTION, "hotkeys\\1\\actions\\1\\hasScreens")
                .as_deref(),
            Some("true")
        );
        assert_eq!(
            ini.get(SECTION, "hotkeys\\1\\actions\\2\\switchInDirection")
                .as_deref(),
            Some("3")
        );
        assert_eq!(
            ini.get(SECTION, "hotkeys\\1\\actions\\3\\type").as_deref(),
            Some("7")
        );
        assert_eq!(
            ini.get(SECTION, "screens\\1\\name").as_deref(),
            Some("srv"),
            "the layout grid is untouched"
        );

        write(&mut ini, &[]);
        assert!(read(&ini).is_empty());
        assert!(
            ini.entries(SECTION)
                .iter()
                .all(|(k, _)| !k.starts_with("hotkeys\\") || k == "hotkeys\\size")
        );
    }

    #[test]
    fn accepts_the_payload_the_ui_sends() {
        let json = r#"[{"keys":[67108864,134217728,16777234],"actions":[
            {"kind":"switchToScreen","keys":[],"computers":[],"target":"laptop","direction":"left","lockMode":"toggle","onRelease":false},
            {"kind":"lockCursorToScreen","keys":[],"computers":[],"target":"","direction":"left","lockMode":"off","onRelease":true}]}]"#;
        let hk: Vec<Hotkey> = serde_json::from_str(json).unwrap();
        assert_eq!(hk[0].keys, [CONTROL, ALT, KEY_LEFT]);
        assert_eq!(hk[0].actions[1].lock_mode, LockMode::Off);
    }
}
