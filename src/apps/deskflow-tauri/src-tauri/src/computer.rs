//! Per-computer options stored in `[computer_<name>]` and read by the core in
//! `src/lib/server/Config.cpp` (`Config::addComputer`).

use serde::{Deserialize, Serialize};

use crate::i18n;
use crate::layout::is_valid_name;
use crate::settings::IniFile;

pub const MODIFIERS: [&str; 7] = ["shift", "ctrl", "alt", "altgr", "meta", "super", "none"];

/// What the client receives for each modifier pressed on the server.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Modifiers {
    pub shift: String,
    pub ctrl: String,
    pub alt: String,
    pub altgr: String,
    pub meta: String,
    #[serde(rename = "super")]
    pub super_: String,
}

impl Default for Modifiers {
    fn default() -> Self {
        Self {
            shift: "shift".into(),
            ctrl: "ctrl".into(),
            alt: "alt".into(),
            altgr: "altgr".into(),
            meta: "meta".into(),
            super_: "super".into(),
        }
    }
}

/// Corners where leaving the screen does not switch to the neighbour.
#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Corners {
    pub top_left: bool,
    pub top_right: bool,
    pub bottom_left: bool,
    pub bottom_right: bool,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ComputerOptions {
    pub aliases: Vec<String>,
    pub modifiers: Modifiers,
    pub corners: Corners,
    /// Size of the dead corners, in pixels.
    pub corner_size: u32,
    pub fix_caps_lock: bool,
    pub fix_num_lock: bool,
    pub fix_scroll_lock: bool,
    pub xtest_xinerama: bool,
    pub weak_x11_focus: bool,
}

fn section(name: &str) -> String {
    format!("computer_{name}")
}

/// Qt writes a `QStringList` as `a, b`, and an empty one as `@Invalid()`.
fn parse_list(raw: &str) -> Vec<String> {
    let raw = raw.trim();
    if raw.is_empty() || raw == "@Invalid()" {
        return Vec::new();
    }
    raw.split(',')
        .map(|s| s.trim().trim_matches('"').to_owned())
        .filter(|s| !s.is_empty())
        .collect()
}

fn format_list(items: &[String]) -> String {
    if items.is_empty() {
        "@Invalid()".into()
    } else {
        items.join(", ")
    }
}

fn modifier(ini: &IniFile, sec: &str, key: &str, default: &str) -> String {
    ini.get(sec, key)
        .map(|v| v.to_lowercase())
        .filter(|v| MODIFIERS.contains(&v.as_str()))
        .unwrap_or_else(|| default.to_owned())
}

pub fn read(ini: &IniFile, name: &str) -> ComputerOptions {
    let sec = section(name);
    let flag = |key: &str| ini.get(&sec, key).is_some_and(|v| v == "true");
    let d = Modifiers::default();
    ComputerOptions {
        aliases: ini
            .entries(&sec)
            .into_iter()
            .find(|(k, _)| k == "aliases")
            .map(|(_, v)| parse_list(&v))
            .unwrap_or_default(),
        modifiers: Modifiers {
            shift: modifier(ini, &sec, "modifierShift", &d.shift),
            ctrl: modifier(ini, &sec, "modifierCtrl", &d.ctrl),
            alt: modifier(ini, &sec, "modifierAlt", &d.alt),
            altgr: modifier(ini, &sec, "modifierAltGr", &d.altgr),
            meta: modifier(ini, &sec, "modifierMeta", &d.meta),
            super_: modifier(ini, &sec, "modifierSuper", &d.super_),
        },
        corners: Corners {
            top_left: flag("switchCornerTopLeft"),
            top_right: flag("switchCornerTopRight"),
            bottom_left: flag("switchCornerBottomLeft"),
            bottom_right: flag("switchCornerBottomRight"),
        },
        corner_size: ini
            .get(&sec, "switchCornerSize")
            .and_then(|v| v.parse().ok())
            .unwrap_or(0),
        fix_caps_lock: flag("halfDuplexCapsLock"),
        fix_num_lock: flag("halfDuplexNumLock"),
        fix_scroll_lock: flag("halfDuplexScrollLock"),
        xtest_xinerama: flag("xtestIsXineramaUnaware"),
        weak_x11_focus: flag("weakX11Focus"),
    }
}

/// Writes every key, like the Qt GUI does when a computer is saved.
pub fn write(ini: &mut IniFile, name: &str, o: &ComputerOptions) {
    let sec = section(name);
    let b = |v: bool| if v { "true" } else { "false" };
    ini.set_raw(&sec, "aliases", &format_list(&o.aliases));
    ini.set(&sec, "halfDuplexCapsLock", b(o.fix_caps_lock));
    ini.set(&sec, "halfDuplexNumLock", b(o.fix_num_lock));
    ini.set(&sec, "halfDuplexScrollLock", b(o.fix_scroll_lock));
    ini.set(&sec, "xtestIsXineramaUnaware", b(o.xtest_xinerama));
    ini.set(&sec, "weakX11Focus", b(o.weak_x11_focus));
    ini.set(&sec, "switchCornerSize", &o.corner_size.to_string());
    ini.set(&sec, "switchCornerTopLeft", b(o.corners.top_left));
    ini.set(&sec, "switchCornerTopRight", b(o.corners.top_right));
    ini.set(&sec, "switchCornerBottomLeft", b(o.corners.bottom_left));
    ini.set(&sec, "switchCornerBottomRight", b(o.corners.bottom_right));
    ini.set(&sec, "modifierShift", &o.modifiers.shift);
    ini.set(&sec, "modifierCtrl", &o.modifiers.ctrl);
    ini.set(&sec, "modifierAlt", &o.modifiers.alt);
    ini.set(&sec, "modifierAltGr", &o.modifiers.altgr);
    ini.set(&sec, "modifierMeta", &o.modifiers.meta);
    ini.set(&sec, "modifierSuper", &o.modifiers.super_);
}

/// Modifier names by `KeyboardModifier` value (src/lib/common/KeyboardModifier.h).
const LEGACY_MODIFIERS: [&str; 7] = ["shift", "ctrl", "alt", "meta", "super", "altgr", "none"];

/// Copies computers kept in the Qt grid (`internalConfig/screens\N\...`, Deskflow 1.26
/// and older) to `[computer_<name>]`, which is all the core reads since 1.27.
///
/// The 1.27 Qt GUI does this when it starts (`Settings::upgradeSettings`), but an
/// install it never opened still has the old form, and the core then refuses the
/// layout ("unknown computer name"). Computers that already have a group are left
/// alone. Returns the names copied.
pub fn upgrade_legacy(ini: &mut IniFile) -> Vec<String> {
    const G: &str = "internalConfig";
    let mut upgraded = Vec::new();
    for (key, _) in ini.entries(G) {
        let Some(index) = key
            .strip_prefix("screens\\")
            .and_then(|k| k.strip_suffix("\\name"))
        else {
            continue;
        };
        let Some(name) = ini.get(G, &key).filter(|n| !n.is_empty()) else {
            continue;
        };
        if ini.get(&section(&name), "name").is_some() {
            continue;
        }

        let item = |rest: &str| ini.get(G, &format!("screens\\{index}\\{rest}"));
        // a Qt array: <array>\size, then <array>\1\<key> ... <array>\<size>\<key>
        let array = |array: &str, key: &str| -> Vec<String> {
            let size = item(&format!("{array}\\size"))
                .and_then(|s| s.parse::<usize>().ok())
                .unwrap_or(0);
            (1..=size)
                .map(|i| item(&format!("{array}\\{i}\\{key}")).unwrap_or_default())
                .collect()
        };
        let flags = |name: &str, key: &str| -> Vec<bool> {
            array(name, key).iter().map(|v| v == "true").collect()
        };

        let fixes = flags("fixArray", "fix");
        let corners = flags("switchCornerArray", "switchCorner");
        let flag = |list: &[bool], i: usize| list.get(i).copied().unwrap_or(false);

        // slot order: shift, ctrl, alt, meta, super, altgr; -1 means "same key"
        let modifiers = array("modifierArray", "modifier");
        let modifier = |slot: usize| -> String {
            let value = modifiers
                .get(slot)
                .and_then(|v| v.parse::<i64>().ok())
                .unwrap_or(-1);
            let index = if value < 0 { slot } else { value as usize };
            LEGACY_MODIFIERS
                .get(index)
                .copied()
                .unwrap_or(LEGACY_MODIFIERS[slot])
                .to_owned()
        };

        let mut aliases: Vec<String> = Vec::new();
        for alias in array("aliasArray", "alias") {
            let alias = alias.trim().to_owned();
            if !alias.is_empty() && alias != name && !aliases.contains(&alias) {
                aliases.push(alias);
            }
        }

        let options = ComputerOptions {
            aliases,
            modifiers: Modifiers {
                shift: modifier(0),
                ctrl: modifier(1),
                alt: modifier(2),
                meta: modifier(3),
                super_: modifier(4),
                altgr: modifier(5),
            },
            corners: Corners {
                top_left: flag(&corners, 0),
                top_right: flag(&corners, 1),
                bottom_left: flag(&corners, 2),
                bottom_right: flag(&corners, 3),
            },
            corner_size: item("switchCornerSize")
                .and_then(|v| v.parse().ok())
                .unwrap_or(0),
            fix_caps_lock: flag(&fixes, 0),
            fix_num_lock: flag(&fixes, 1),
            fix_scroll_lock: flag(&fixes, 2),
            xtest_xinerama: flag(&fixes, 3),
            weak_x11_focus: false,
        };
        ini.set(&section(&name), "name", &name);
        write(ini, &name, &options);
        upgraded.push(name);
    }
    upgraded
}

fn looks_like_ip(s: &str) -> bool {
    s.parse::<std::net::IpAddr>().is_ok()
}

/// Checks the options of every computer together, since aliases must be unique overall.
pub fn validate(computers: &[(&str, &ComputerOptions)]) -> Result<(), String> {
    let mut taken: std::collections::HashMap<String, &str> = computers
        .iter()
        .map(|(n, _)| (n.to_lowercase(), *n))
        .collect();
    for (name, o) in computers {
        let m = &o.modifiers;
        for v in [&m.shift, &m.ctrl, &m.alt, &m.altgr, &m.meta, &m.super_] {
            if !MODIFIERS.contains(&v.as_str()) {
                return Err(i18n::tf(
                    "error.computer.unknownModifier",
                    &[("key", v), ("name", name)],
                ));
            }
        }
        if o.corner_size > 1000 {
            return Err(i18n::tf("error.computer.cornerTooBig", &[("name", name)]));
        }
        for alias in &o.aliases {
            if !is_valid_name(alias) || looks_like_ip(alias) {
                return Err(i18n::tf("error.computer.invalidAlias", &[("alias", alias)]));
            }
            if let Some(owner) = taken.insert(alias.to_lowercase(), name) {
                return Err(if owner == *name {
                    i18n::tf(
                        "error.computer.repeatedAlias",
                        &[("alias", alias), ("name", name)],
                    )
                } else {
                    i18n::tf(
                        "error.computer.aliasTaken",
                        &[("alias", alias), ("owner", &owner)],
                    )
                });
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    fn ini(text: &str) -> IniFile {
        let path = std::env::temp_dir().join(format!(
            "computer-test-{}-{}.conf",
            std::process::id(),
            text.len()
        ));
        std::fs::write(&path, text).unwrap();
        let f = IniFile::load(&path).unwrap();
        let _ = std::fs::remove_file(PathBuf::from(&path));
        f
    }

    /// What Deskflow 1.26's Qt GUI leaves in Deskflow.conf
    const QT_1_26: &str = r"[core]
computerName=ALEXIS-WORK
lastVersion=1.26.0.0
coreMode=2

[internalConfig]
screens\1\name=
screens\2\name=ALEXIS-PC
screens\2\aliasArray\1\alias=alexis-server
screens\2\aliasArray\2\alias=ALEXIS-PC
screens\2\aliasArray\size=2
screens\2\fixArray\1\fix=false
screens\2\fixArray\2\fix=true
screens\2\fixArray\3\fix=false
screens\2\fixArray\4\fix=false
screens\2\fixArray\size=4
screens\2\modifierArray\1\modifier=-1
screens\2\modifierArray\2\modifier=3
screens\2\modifierArray\3\modifier=-1
screens\2\modifierArray\4\modifier=1
screens\2\modifierArray\5\modifier=-1
screens\2\modifierArray\6\modifier=-1
screens\2\modifierArray\size=6
screens\2\switchCornerArray\1\switchCorner=true
screens\2\switchCornerArray\2\switchCorner=false
screens\2\switchCornerArray\3\switchCorner=false
screens\2\switchCornerArray\4\switchCorner=false
screens\2\switchCornerArray\size=4
screens\2\switchCornerSize=12
screens\3\name=ALEXIS-WORK
screens\size=15
";

    #[test]
    fn upgrades_computers_from_the_qt_1_26_grid() {
        let mut f = ini(QT_1_26);
        let mut names = upgrade_legacy(&mut f);
        names.sort();
        assert_eq!(names, ["ALEXIS-PC", "ALEXIS-WORK"]);

        assert_eq!(
            f.get("computer_ALEXIS-PC", "name").as_deref(),
            Some("ALEXIS-PC")
        );
        let pc = read(&f, "ALEXIS-PC");
        assert_eq!(pc.aliases, ["alexis-server"]); // its own name isn't an alias
        assert!(pc.fix_num_lock && !pc.fix_caps_lock);
        assert_eq!(pc.modifiers.ctrl, "meta"); // 3 = Meta
        assert_eq!(pc.modifiers.meta, "ctrl"); // 1 = Ctrl
        assert_eq!(pc.modifiers.shift, "shift"); // -1 = same key
        assert!(pc.corners.top_left && !pc.corners.top_right);
        assert_eq!(pc.corner_size, 12);

        let work = read(&f, "ALEXIS-WORK");
        assert_eq!(work, ComputerOptions::default());

        // once there, never again (nor over what the person changed since)
        f.set("computer_ALEXIS-PC", "modifierCtrl", "alt");
        assert!(upgrade_legacy(&mut f).is_empty());
        assert_eq!(read(&f, "ALEXIS-PC").modifiers.ctrl, "alt");
    }

    #[test]
    fn defaults_when_missing() {
        let o = read(&ini("[computer_a]\nname=a\n"), "a");
        assert_eq!(o, ComputerOptions::default());
    }

    #[test]
    fn reads_qt_written_values() {
        let f = ini(
            "[computer_mac]\naliases=mac.local, macbook\nhalfDuplexCapsLock=true\nmodifierCtrl=super\n\
             modifierSuper=ctrl\nname=mac\nswitchCornerSize=12\nswitchCornerTopLeft=true\n",
        );
        let o = read(&f, "mac");
        assert_eq!(o.aliases, ["mac.local", "macbook"]);
        assert!(o.fix_caps_lock && !o.fix_num_lock);
        assert_eq!(
            (
                o.modifiers.ctrl.as_str(),
                o.modifiers.super_.as_str(),
                o.modifiers.alt.as_str()
            ),
            ("super", "ctrl", "alt")
        );
        assert_eq!(o.corner_size, 12);
        assert!(o.corners.top_left && !o.corners.bottom_right);
        assert!(
            read(&ini("[computer_x]\naliases=@Invalid()\n"), "x")
                .aliases
                .is_empty()
        );
    }

    #[test]
    fn write_then_read_roundtrips() {
        let mut f = ini("[computer_pc]\nname=pc\n");
        let mut o = ComputerOptions {
            aliases: vec!["pc2".into(), "pc.lan".into()],
            corner_size: 30,
            ..Default::default()
        };
        o.corners.bottom_right = true;
        o.modifiers.alt = "none".into();
        o.weak_x11_focus = true;
        write(&mut f, "pc", &o);
        assert_eq!(read(&f, "pc"), o);
        assert_eq!(f.get("computer_pc", "name").as_deref(), Some("pc"));

        write(&mut f, "pc", &ComputerOptions::default());
        assert_eq!(
            f.entries("computer_pc")
                .iter()
                .find(|(k, _)| k == "aliases")
                .unwrap()
                .1,
            "@Invalid()"
        );
    }

    #[test]
    fn accepts_the_payload_the_ui_sends() {
        // captured from the UI (src/layout/options.ts)
        let json = r#"{"aliases":["macbook","laptop-trabajo"],"cornerSize":20,
            "corners":{"bottomLeft":false,"bottomRight":false,"topLeft":true,"topRight":false},
            "fixCapsLock":false,"fixNumLock":false,"fixScrollLock":false,
            "modifiers":{"alt":"alt","altgr":"altgr","ctrl":"super","meta":"meta","shift":"shift","super":"ctrl"},
            "weakX11Focus":false,"xtestXinerama":false}"#;
        let o: ComputerOptions = serde_json::from_str(json).unwrap();
        assert_eq!(o.aliases, ["macbook", "laptop-trabajo"]);
        assert!(o.corners.top_left && o.corner_size == 20);
        assert_eq!(
            (o.modifiers.ctrl.as_str(), o.modifiers.super_.as_str()),
            ("super", "ctrl")
        );
        assert_eq!(
            serde_json::to_value(&o).unwrap(),
            serde_json::from_str::<serde_json::Value>(json).unwrap()
        );
    }

    #[test]
    fn validates_aliases_across_computers() {
        let a = ComputerOptions {
            aliases: vec!["laptop".into()],
            ..Default::default()
        };
        let ok = ComputerOptions::default();
        assert!(validate(&[("pc", &a), ("mac", &ok)]).is_ok());
        assert!(
            validate(&[("pc", &a), ("laptop", &ok)]).is_err(),
            "alias equal to another computer's name"
        );
        let ip = ComputerOptions {
            aliases: vec!["192.168.1.4".into()],
            ..Default::default()
        };
        assert!(validate(&[("pc", &ip)]).is_err());
        let space = ComputerOptions {
            aliases: vec!["mi pc".into()],
            ..Default::default()
        };
        assert!(validate(&[("pc", &space)]).is_err());
        let mut bad = ComputerOptions::default();
        bad.modifiers.ctrl = "hyper".into();
        assert!(validate(&[("pc", &bad)]).is_err());
    }
}
