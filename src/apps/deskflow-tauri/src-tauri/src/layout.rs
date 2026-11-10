//! Free-form screen layout.
//!
//! Each computer is a rectangle on a shared canvas. Wherever two rectangles touch,
//! we emit a link for the shared part of the edge, with the matching ranges on both
//! sides (`right(0,62.5) = laptop(20,100)`), which `src/lib/server/Config.cpp` maps
//! proportionally. The layout itself lives in a JSON file of our own; the core only
//! sees the generated server config plus one `[computer_<name>]` group per computer.

use std::collections::HashSet;
#[cfg(test)]
use std::collections::VecDeque;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

use crate::computer::{self, ComputerOptions};
use crate::hotkeys;
use crate::i18n;
use crate::paths;
use crate::settings::{self, IniFile};

pub const DEFAULT_W: f64 = 160.0;
pub const DEFAULT_H: f64 = 90.0;
/// Edges closer than this (in layout units) count as touching.
const EPS: f64 = 0.5;
const MIN_SIZE: f64 = 20.0;

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Screen {
    pub name: String,
    pub x: f64,
    pub y: f64,
    pub w: f64,
    pub h: f64,
    /// Per-computer options. Live in the settings file, not in the layout file:
    /// filled in on load, written to `[computer_<name>]` on save.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub options: Option<ComputerOptions>,
}

impl Screen {
    fn right(&self) -> f64 {
        self.x + self.w
    }
    fn bottom(&self) -> f64 {
        self.y + self.h
    }
    fn overlaps(&self, o: &Screen) -> bool {
        self.x < o.right() - EPS
            && o.x < self.right() - EPS
            && self.y < o.bottom() - EPS
            && o.y < self.bottom() - EPS
    }
}

#[derive(Debug, Default, Serialize, Deserialize)]
struct LayoutFile {
    version: u32,
    /// Name of the server when the layout was saved, to follow renames of this computer.
    server: String,
    screens: Vec<Screen>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Side {
    Right,
    Left,
    Up,
    Down,
}

impl Side {
    fn name(self) -> &'static str {
        match self {
            Side::Right => "right",
            Side::Left => "left",
            Side::Up => "up",
            Side::Down => "down",
        }
    }
}

/// A one-way link: leaving `from` through `side` within `[from_start, from_end]`
/// enters `to` within `[to_start, to_end]`. Ranges are fractions of the edge length.
#[derive(Debug, Clone, PartialEq)]
pub struct Link {
    pub from: String,
    pub side: Side,
    pub from_start: f64,
    pub from_end: f64,
    pub to: String,
    pub to_start: f64,
    pub to_end: f64,
}

pub fn links(screens: &[Screen]) -> Vec<Link> {
    let mut out = Vec::new();
    for a in screens {
        for b in screens {
            if a.name == b.name {
                continue;
            }
            // vertical edges: overlap along y
            let (lo, hi) = (a.y.max(b.y), a.bottom().min(b.bottom()));
            if hi - lo > EPS {
                let ranges = (
                    (lo - a.y) / a.h,
                    (hi - a.y) / a.h,
                    (lo - b.y) / b.h,
                    (hi - b.y) / b.h,
                );
                if (a.right() - b.x).abs() < EPS {
                    out.push(link(a, Side::Right, ranges, b));
                }
                if (a.x - b.right()).abs() < EPS {
                    out.push(link(a, Side::Left, ranges, b));
                }
            }
            // horizontal edges: overlap along x
            let (lo, hi) = (a.x.max(b.x), a.right().min(b.right()));
            if hi - lo > EPS {
                let ranges = (
                    (lo - a.x) / a.w,
                    (hi - a.x) / a.w,
                    (lo - b.x) / b.w,
                    (hi - b.x) / b.w,
                );
                if (a.bottom() - b.y).abs() < EPS {
                    out.push(link(a, Side::Down, ranges, b));
                }
                if (a.y - b.bottom()).abs() < EPS {
                    out.push(link(a, Side::Up, ranges, b));
                }
            }
        }
    }
    out
}

fn link(a: &Screen, side: Side, (fs, fe, ts, te): (f64, f64, f64, f64), b: &Screen) -> Link {
    Link {
        from: a.name.clone(),
        side,
        from_start: fs,
        from_end: fe,
        to: b.name.clone(),
        to_start: ts,
        to_end: te,
    }
}

/// Names of computers the cursor can never reach from the server (the UI does the same live).
#[cfg(test)]
pub fn unreachable(screens: &[Screen], server: &str) -> Vec<String> {
    let links = links(screens);
    let mut seen = HashSet::from([server.to_owned()]);
    let mut queue = VecDeque::from([server.to_owned()]);
    while let Some(name) = queue.pop_front() {
        for l in links.iter().filter(|l| l.from == name) {
            if seen.insert(l.to.clone()) {
                queue.push_back(l.to.clone());
            }
        }
    }
    screens
        .iter()
        .filter(|s| !seen.contains(&s.name))
        .map(|s| s.name.clone())
        .collect()
}

/// Same rule as the Qt GUI's ComputerNameValidator: `^[\w._-]{1,255}$`.
pub fn is_valid_name(name: &str) -> bool {
    !name.is_empty()
        && name.chars().count() <= 255
        && name
            .chars()
            .all(|c| c.is_alphanumeric() || "_.-".contains(c))
}

pub fn validate(screens: &[Screen], server: &str) -> Result<(), String> {
    let mut names = HashSet::new();
    for s in screens {
        if !is_valid_name(&s.name) {
            return Err(i18n::tf("error.layout.invalidName", &[("name", &s.name)]));
        }
        if !names.insert(s.name.to_lowercase()) {
            return Err(i18n::tf("error.layout.duplicateName", &[("name", &s.name)]));
        }
        if !(s.w >= MIN_SIZE && s.h >= MIN_SIZE && s.x.is_finite() && s.y.is_finite()) {
            return Err(i18n::tf("error.layout.invalidSize", &[("name", &s.name)]));
        }
    }
    if !screens.iter().any(|s| s.name == server) {
        return Err(i18n::t("error.layout.missingThisComputer"));
    }
    for (i, a) in screens.iter().enumerate() {
        if let Some(b) = screens[i + 1..].iter().find(|b| a.overlaps(b)) {
            return Err(i18n::tf(
                "error.layout.overlap",
                &[("a", &a.name), ("b", &b.name)],
            ));
        }
    }
    Ok(())
}

fn pct(f: f64) -> String {
    let s = format!("{:.2}", (f * 100.0).clamp(0.0, 100.0));
    s.trim_end_matches('0').trim_end_matches('.').to_owned()
}

fn interval(start: &str, end: &str) -> Option<String> {
    if start == "0" && end == "100" {
        return Some(String::new());
    }
    // the core rejects empty ranges; rounding can produce one for slivers
    (start.parse::<f64>().ok()? < end.parse::<f64>().ok()?).then(|| format!("({start},{end})"))
}

/// The server config consumed by the core (`section: links` + `section: options`).
pub fn config_text(screens: &[Screen], options: &str) -> String {
    let mut links = links(screens);
    links.sort_by(|a, b| {
        a.side
            .cmp(&b.side)
            .then(a.from_start.total_cmp(&b.from_start))
    });
    let mut out = String::from("section: links\n");
    for s in screens {
        out.push_str(&format!("\t{}:\n", s.name));
        // ranges on one edge must not overlap after rounding, or the core refuses the
        // whole config ("overlapping range"): clamp each start to the previous end
        let mut previous: Option<(Side, f64)> = None;
        for l in links.iter().filter(|l| l.from == s.name) {
            let mut start = pct(l.from_start);
            let end = pct(l.from_end);
            if let Some((side, prev_end)) = previous
                && side == l.side
                && start.parse::<f64>().unwrap_or(0.0) < prev_end
            {
                start = pct(prev_end / 100.0);
            }
            if let (Some(src), Some(dst)) = (
                interval(&start, &end),
                interval(&pct(l.to_start), &pct(l.to_end)),
            ) {
                out.push_str(&format!("\t\t{}{src} = {}{dst}\n", l.side.name(), l.to));
                previous = Some((l.side, end.parse().unwrap_or(100.0)));
            }
        }
    }
    out.push_str("end\n\nsection: options\n");
    out.push_str(options);
    out.push_str("end\n\n");
    out
}

// ---- files -----------------------------------------------------------------------------

pub fn layout_file() -> PathBuf {
    let settings = paths::settings_file();
    settings
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_default()
        .join("deskflow-layout.json")
}

fn generated_config_file() -> PathBuf {
    paths::settings_dir().join("deskflow-server-layout.conf")
}

fn fallback_config_file() -> PathBuf {
    layout_file().with_file_name("deskflow-server-layout.conf")
}

/// Mirrors `Settings::serverConfigFile()`.
pub fn server_config_file(ini: &IniFile) -> PathBuf {
    let external = ini
        .get("server", "externalConfig")
        .is_some_and(|v| v == "true");
    match ini.get("server", "externalConfigFile") {
        Some(f) if external && !f.is_empty() => PathBuf::from(f),
        _ => paths::settings_dir().join("deskflow-server.conf"),
    }
}

fn is_our_config(path: &Path) -> bool {
    let norm = |p: &Path| p.to_string_lossy().replace('\\', "/").to_lowercase();
    norm(path) == norm(&generated_config_file()) || norm(path) == norm(&fallback_config_file())
}

fn qt_path(p: &Path) -> String {
    p.to_string_lossy().replace('\\', "/")
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LayoutView {
    pub screens: Vec<Screen>,
    pub server_name: String,
    /// Set when the core is pointed at a hand-written config we must not overwrite.
    pub foreign_config: Option<String>,
    /// True until the layout has been saved from this app at least once.
    pub imported: bool,
}

fn read_layout_file() -> Option<LayoutFile> {
    let text = std::fs::read_to_string(layout_file()).ok()?;
    serde_json::from_str(&text).ok()
}

/// Builds a free layout from the Qt GUI's grid (`internalConfig/screens`).
fn import_grid(ini: &IniFile, server: &str) -> Vec<Screen> {
    let width = ini
        .get("server", "gridWidth")
        .and_then(|v| v.parse::<usize>().ok())
        .filter(|&w| w > 0)
        .unwrap_or(5);
    let mut screens: Vec<Screen> = Vec::new();
    for (key, _) in ini.entries("internalConfig") {
        let Some(index) = key
            .strip_prefix("screens\\")
            .and_then(|k| k.strip_suffix("\\name"))
        else {
            continue;
        };
        let Ok(index) = index.parse::<usize>() else {
            continue;
        };
        let Some(name) = ini.get("internalConfig", &key).filter(|n| !n.is_empty()) else {
            continue;
        };
        let i = index - 1;
        screens.push(Screen {
            options: None,
            name,
            x: (i % width) as f64 * DEFAULT_W,
            y: (i / width) as f64 * DEFAULT_H,
            w: DEFAULT_W,
            h: DEFAULT_H,
        });
    }
    if !screens.iter().any(|s| s.name == server) {
        place_new(&mut screens, server);
    }
    screens
}

/// Adds a default-sized screen to the right of everything else.
fn place_new(screens: &mut Vec<Screen>, name: &str) {
    let x = screens.iter().map(Screen::right).fold(0.0, f64::max);
    let y = screens.first().map_or(0.0, |s| s.y);
    screens.push(Screen {
        name: name.to_owned(),
        x,
        y,
        w: DEFAULT_W,
        h: DEFAULT_H,
        options: None,
    });
}

pub fn load() -> Result<LayoutView, String> {
    let overview = settings::read_overview().map_err(|e| e.to_string())?;
    let server = overview.computer_name;
    let mut ini = IniFile::load(&paths::settings_file()).map_err(|e| e.to_string())?;
    // computers Deskflow 1.26 kept only in its grid: the core needs them in [computer_*]
    if !computer::upgrade_legacy(&mut ini).is_empty() {
        ini.save()
            .map_err(|e| i18n::tf("error.layout.upgradeSettings", &[("reason", &e)]))?;
    }

    let (mut screens, imported) = match read_layout_file() {
        Some(file) => {
            let mut screens = file.screens;
            // this computer was renamed outside the editor: follow it
            if !screens.iter().any(|s| s.name == server)
                && let Some(s) = screens.iter_mut().find(|s| s.name == file.server)
            {
                s.name = server.clone();
            }
            (screens, false)
        }
        None => (import_grid(&ini, &server), true),
    };
    if !screens.iter().any(|s| s.name == server) {
        place_new(&mut screens, &server);
    }

    let current = server_config_file(&ini);
    let external = ini
        .get("server", "externalConfig")
        .is_some_and(|v| v == "true");
    let foreign_config =
        (external && !is_our_config(&current)).then(|| current.to_string_lossy().into_owned());
    for s in &mut screens {
        s.options = Some(computer::read(&ini, &s.name));
    }
    Ok(LayoutView {
        screens,
        server_name: server,
        foreign_config,
        imported,
    })
}

/// Every name the core accepts for a computer: canonical names plus aliases.
fn known_names(ini: &IniFile, screens: &[Screen]) -> HashSet<String> {
    let mut known = HashSet::new();
    for s in screens {
        known.insert(s.name.clone());
        let options = s
            .options
            .clone()
            .unwrap_or_else(|| computer::read(ini, &s.name));
        known.extend(options.aliases);
    }
    known
}

/// Writes the server config the core will read. Hotkeys come from the settings
/// file (`internalConfig/hotkeys`), shared with the Qt GUI.
fn write_config(ini: &IniFile, screens: &[Screen]) -> std::io::Result<PathBuf> {
    let options = hotkeys::options_text(&hotkeys::read(ini), &known_names(ini, screens));
    let text = config_text(screens, &options);
    let primary = generated_config_file();
    let write = |path: &Path| -> std::io::Result<()> {
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir)?;
        }
        std::fs::write(path, &text)
    };
    match write(&primary) {
        Ok(()) => Ok(primary),
        // e.g. no write access to ProgramData for this user
        Err(_) => write(&fallback_config_file()).map(|()| fallback_config_file()),
    }
}

/// Keeps the Qt GUI's grid roughly in sync, so its editor shows the same computers
/// and does not drop their `[computer_*]` groups if it is used again.
fn write_grid_projection(ini: &mut IniFile, screens: &[Screen], server: &str) {
    let width = ini
        .get("server", "gridWidth")
        .and_then(|v| v.parse::<i64>().ok())
        .filter(|&w| w > 0)
        .unwrap_or(5);
    let height = ini
        .get("server", "gridHeight")
        .and_then(|v| v.parse::<i64>().ok())
        .filter(|&h| h > 0)
        .unwrap_or(3);
    let mut cells: Vec<Option<&str>> = vec![None; (width * height) as usize];
    let Some(srv) = screens.iter().find(|s| s.name == server) else {
        return;
    };
    let (cx, cy) = (width / 2, height / 2);
    let (sx, sy) = (srv.x + srv.w / 2.0, srv.y + srv.h / 2.0);

    let mut ordered: Vec<&Screen> = screens.iter().collect();
    let dist = |s: &Screen| (s.x + s.w / 2.0 - sx).hypot(s.y + s.h / 2.0 - sy);
    ordered.sort_by(|a, b| dist(a).total_cmp(&dist(b)));
    for s in ordered {
        let col = cx + ((s.x + s.w / 2.0 - sx) / srv.w).round() as i64;
        let row = cy + ((s.y + s.h / 2.0 - sy) / srv.h).round() as i64;
        // nearest free cell to the wanted one
        let free = (0..width * height)
            .filter(|&i| cells[i as usize].is_none())
            .min_by_key(|&i| (i % width - col).pow(2) + (i / width - row).pow(2));
        if let Some(i) = free {
            cells[i as usize] = Some(&s.name);
        }
    }

    ini.remove_keys("internalConfig", |k| k.starts_with("screens\\"));
    for (i, name) in cells.iter().enumerate() {
        ini.set(
            "internalConfig",
            &format!("screens\\{}\\name", i + 1),
            name.unwrap_or(""),
        );
    }
    ini.set("internalConfig", "screens\\size", &cells.len().to_string());
}

#[derive(Debug, Deserialize)]
pub struct Rename {
    pub from: String,
    pub to: String,
}

pub fn save(screens: Vec<Screen>, renames: &[Rename]) -> Result<LayoutView, String> {
    let io = |e: std::io::Error| i18n::tf("error.layout.save", &[("reason", &e)]);
    let old_server = settings::read_overview().map_err(io)?.computer_name;
    let new_server = renames
        .iter()
        .find(|r| r.from == old_server)
        .map_or(old_server.clone(), |r| r.to.clone());
    validate(&screens, &new_server)?;
    let options: Vec<(&str, &ComputerOptions)> = screens
        .iter()
        .filter_map(|s| s.options.as_ref().map(|o| (s.name.as_str(), o)))
        .collect();
    computer::validate(&options)?;

    let mut ini = IniFile::load(&paths::settings_file()).map_err(io)?;

    // move per-computer options (modifiers, corners, aliases...) along with renames
    for r in renames.iter().filter(|r| r.from != r.to) {
        let entries = ini.entries(&format!("computer_{}", r.from));
        ini.remove_section(&format!("computer_{}", r.from));
        for (k, v) in entries {
            if k != "name" {
                ini.set_raw(&format!("computer_{}", r.to), &k, &v);
            }
        }
    }
    if new_server != old_server {
        ini.set("core", "computerName", &new_server);
    }

    // the core learns the computer names from these groups
    let names: HashSet<String> = screens
        .iter()
        .map(|s| format!("computer_{}", s.name))
        .collect();
    for section in ini.sections() {
        if section.starts_with("computer_") && !names.contains(&section) {
            ini.remove_section(&section);
        }
    }
    for s in &screens {
        ini.set(&format!("computer_{}", s.name), "name", &s.name);
        if let Some(o) = &s.options {
            computer::write(&mut ini, &s.name, o);
        }
    }

    // hotkeys must follow renames, and must not name computers that are gone:
    // the core rejects the whole config over a single unknown name
    let mut keys = hotkeys::read(&ini);
    let renamed: Vec<(String, String)> = renames
        .iter()
        .map(|r| (r.from.clone(), r.to.clone()))
        .collect();
    if hotkeys::retarget(&mut keys, &renamed, &known_names(&ini, &screens)) {
        hotkeys::write(&mut ini, &keys);
    }

    write_grid_projection(&mut ini, &screens, &new_server);
    let config = write_config(&ini, &screens).map_err(io)?;
    ini.set("server", "externalConfig", "true");
    ini.set("server", "externalConfigFile", &qt_path(&config));
    ini.save().map_err(io)?;

    let screens = screens
        .into_iter()
        .map(|s| Screen { options: None, ..s })
        .collect();
    let file = LayoutFile {
        version: 1,
        server: new_server,
        screens,
    };
    let json = serde_json::to_string_pretty(&file).map_err(|e| e.to_string())?;
    std::fs::write(layout_file(), json).map_err(io)?;
    load()
}

/// Regenerates the server config from the saved layout before the core starts,
/// as the Qt GUI does with its grid. No-op if the editor was never used or the
/// user points the core at a config of their own.
/// Makes the core pick up settings that live in the server config (hotkeys):
/// regenerates it, or adopts the current layout if the editor was never used.
pub fn regenerate() -> Result<(), String> {
    let view = load()?;
    if read_layout_file().is_some() && view.foreign_config.is_none() {
        persist()
    } else {
        save(view.screens, &[]).map(|_| ())
    }
}

/// Before the core starts as a server: the config it reads must exist and name only
/// computers it knows. A layout never saved in the editor (e.g. an install that only
/// ran the Qt GUI) is saved as imported, so the core doesn't read an older config.
pub fn prepare_start() -> Result<(), String> {
    if read_layout_file().is_some() {
        return persist();
    }
    let view = load()?;
    if view.foreign_config.is_some() {
        // a config file the person chose themselves: theirs to keep
        return Ok(());
    }
    save(view.screens, &[]).map(|_| ())
}

pub fn persist() -> Result<(), String> {
    if read_layout_file().is_none() {
        return Ok(());
    }
    let view = load()?;
    if view.foreign_config.is_some() {
        return Ok(());
    }
    let mut ini = IniFile::load(&paths::settings_file()).map_err(|e| e.to_string())?;
    let config = write_config(&ini, &view.screens).map_err(|e| e.to_string())?;
    if ini.get("server", "externalConfigFile").map(PathBuf::from) != Some(config.clone()) {
        ini.set("server", "externalConfig", "true");
        ini.set("server", "externalConfigFile", &qt_path(&config));
        ini.save().map_err(|e| e.to_string())?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn s(name: &str, x: f64, y: f64, w: f64, h: f64) -> Screen {
        Screen {
            name: name.into(),
            x,
            y,
            w,
            h,
            options: None,
        }
    }

    #[test]
    fn side_by_side_same_size_links_whole_edges() {
        let screens = [
            s("a", 0.0, 0.0, 160.0, 90.0),
            s("b", 160.0, 0.0, 160.0, 90.0),
        ];
        let text = config_text(&screens, "");
        assert_eq!(
            text,
            "section: links\n\ta:\n\t\tright = b\n\tb:\n\t\tleft = a\nend\n\nsection: options\nend\n\n"
        );
    }

    #[test]
    fn different_sizes_map_proportionally() {
        // b is half as tall as a and aligned with a's bottom half
        let screens = [
            s("a", 0.0, 0.0, 160.0, 100.0),
            s("b", 160.0, 50.0, 80.0, 50.0),
        ];
        let text = config_text(&screens, "");
        assert!(text.contains("\t\tright(50,100) = b\n"), "{text}");
        assert!(text.contains("\t\tleft = a(50,100)\n"), "{text}");
    }

    #[test]
    fn partial_vertical_overlap() {
        let screens = [
            s("top", 0.0, 0.0, 200.0, 100.0),
            s("laptop", 120.0, 100.0, 120.0, 60.0),
        ];
        let text = config_text(
            &screens,
            "\tkeystroke(Control+Alt+Left) = switchInDirection(left)\n",
        );
        assert!(
            text.contains("\t\tdown(60,100) = laptop(0,66.67)\n"),
            "{text}"
        );
        assert!(text.contains("\t\tup(0,66.67) = top(60,100)\n"), "{text}");
        assert!(text.ends_with(
            "section: options\n\tkeystroke(Control+Alt+Left) = switchInDirection(left)\nend\n\n"
        ));
    }

    #[test]
    fn corners_only_touching_are_not_linked() {
        let screens = [
            s("a", 0.0, 0.0, 100.0, 100.0),
            s("b", 100.0, 100.0, 100.0, 100.0),
        ];
        assert!(links(&screens).is_empty());
        assert_eq!(unreachable(&screens, "a"), ["b"]);
    }

    #[test]
    fn several_neighbours_on_one_edge() {
        let screens = [
            s("main", 0.0, 0.0, 100.0, 100.0),
            s("r1", 100.0, 0.0, 50.0, 40.0),
            s("r2", 100.0, 40.0, 50.0, 60.0),
        ];
        let text = config_text(&screens, "");
        assert!(
            text.contains("\tmain:\n\t\tright(0,40) = r1\n\t\tright(40,100) = r2\n"),
            "{text}"
        );
        assert!(unreachable(&screens, "main").is_empty());
    }

    #[test]
    fn neighbours_within_tolerance_never_produce_overlapping_ranges() {
        // r1 and r2 overlap by 0.4 units, which still counts as touching
        let screens = [
            s("main", 0.0, 0.0, 100.0, 100.0),
            s("r1", 100.0, 0.0, 50.0, 40.2),
            s("r2", 100.0, 39.8, 50.0, 60.2),
        ];
        let text = config_text(&screens, "");
        assert!(
            text.contains("\t\tright(0,40.2) = r1\n\t\tright(40.2,100) = r2\n"),
            "{text}"
        );
    }

    #[test]
    fn validation() {
        let ok = [
            s("a", 0.0, 0.0, 100.0, 100.0),
            s("b-2.x", 100.0, 0.0, 100.0, 100.0),
        ];
        assert!(validate(&ok, "a").is_ok());
        assert!(validate(&ok, "zz").is_err());
        assert!(validate(&[s("a b", 0.0, 0.0, 100.0, 100.0)], "a b").is_err());
        assert!(
            validate(
                &[
                    s("a", 0.0, 0.0, 100.0, 100.0),
                    s("A", 200.0, 0.0, 100.0, 100.0)
                ],
                "a"
            )
            .is_err()
        );
        assert!(
            validate(
                &[
                    s("a", 0.0, 0.0, 100.0, 100.0),
                    s("b", 50.0, 50.0, 100.0, 100.0)
                ],
                "a"
            )
            .is_err()
        );
    }

    /// End to end against a throwaway portable install (Windows only, where portable
    /// mode exists): import from the Qt grid, save with a rename, regenerate.
    #[cfg(windows)]
    #[test]
    fn save_roundtrip_in_portable_install() {
        let dir = std::env::temp_dir().join(format!("deskflow-layout-test-{}", std::process::id()));
        let settings_dir = dir.join("settings");
        std::fs::create_dir_all(&settings_dir).unwrap();
        std::fs::write(dir.join("deskflow-core.exe"), b"").unwrap();
        std::fs::write(
            settings_dir.join("Deskflow.conf"),
            "[core]\ncomputerName=srv\ncoreMode=2\n\n[computer_old]\nname=old\nmodifierAlt=Meta\n\n\
             [computer_gone]\nname=gone\n\n[internalConfig]\n\
             hotkeys\\1\\keys\\1\\key=16777264\nhotkeys\\1\\keys\\size=1\n\
             hotkeys\\1\\actions\\1\\type=3\nhotkeys\\1\\actions\\1\\switchScreenName=old\n\
             hotkeys\\1\\actions\\2\\type=3\nhotkeys\\1\\actions\\2\\switchScreenName=gone\n\
             hotkeys\\1\\actions\\size=2\nhotkeys\\size=1\nscreens\\8\\name=srv\n\
             screens\\9\\name=old\nscreens\\size=15\n\n[server]\ngridWidth=5\ngridHeight=3\n",
        )
        .unwrap();
        std::fs::write(
            settings_dir.join("deskflow-server.conf"),
            "section: links\nend\n\nsection: options\n\tkeystroke(F1) = lockCursorToScreen(toggle)\nend\n\n",
        )
        .unwrap();
        // SAFETY: no other test reads DESKFLOW_DIR
        unsafe { std::env::set_var("DESKFLOW_DIR", &dir) };

        let view = load().unwrap();
        assert!(view.imported);
        let geometry: Vec<Screen> = view
            .screens
            .iter()
            .map(|x| Screen {
                options: None,
                ..x.clone()
            })
            .collect();
        assert_eq!(
            geometry,
            [
                s("srv", 320.0, 90.0, DEFAULT_W, DEFAULT_H),
                s("old", 480.0, 90.0, DEFAULT_W, DEFAULT_H)
            ]
        );
        assert_eq!(
            view.screens[1].options.as_ref().unwrap().modifiers.alt,
            "meta",
            "options are loaded"
        );

        let mut lap = s("lap", 0.0, 100.0, 100.0, 60.0);
        let mut lap_options = ComputerOptions {
            aliases: vec!["lap.local".into()],
            ..Default::default()
        };
        lap_options.corners.top_left = true;
        lap.options = Some(lap_options.clone());
        let screens = vec![
            s("srv", 0.0, 0.0, 200.0, 100.0),
            s("new", 200.0, 50.0, 100.0, 50.0),
            lap,
        ];
        let view = save(
            screens,
            &[Rename {
                from: "old".into(),
                to: "new".into(),
            }],
        )
        .unwrap();
        assert!(!view.imported);
        assert_eq!(view.screens.len(), 3);

        let ini = IniFile::load(&settings_dir.join("Deskflow.conf")).unwrap();
        assert_eq!(
            ini.get("computer_new", "modifierAlt").as_deref(),
            Some("Meta"),
            "options follow the rename"
        );
        assert_eq!(ini.get("computer_lap", "name").as_deref(), Some("lap"));
        assert_eq!(computer::read(&ini, "lap"), lap_options);
        assert_eq!(
            view.screens[2].options.as_ref(),
            Some(&lap_options),
            "options come back on load"
        );
        let json = std::fs::read_to_string(layout_file()).unwrap();
        assert!(
            !json.contains("aliases"),
            "options live in the settings file only: {json}"
        );
        assert!(
            !ini.sections()
                .iter()
                .any(|s| s == "computer_old" || s == "computer_gone")
        );
        assert_eq!(ini.get("server", "externalConfig").as_deref(), Some("true"));
        let keys = hotkeys::read(&ini);
        assert_eq!(
            keys[0].actions.len(),
            1,
            "the action for the removed computer is gone"
        );
        assert_eq!(
            keys[0].actions[0].target, "new",
            "hotkeys follow the rename"
        );
        assert_eq!(
            ini.get("internalConfig", "screens\\8\\name").as_deref(),
            Some("srv"),
            "server stays in the grid centre"
        );

        let config_path = PathBuf::from(ini.get("server", "externalConfigFile").unwrap());
        assert_eq!(
            qt_path(&config_path),
            qt_path(&settings_dir.join("deskflow-server-layout.conf"))
        );
        let config = std::fs::read_to_string(&config_path).unwrap();
        assert!(
            config.contains("\tsrv:\n\t\tright(50,100) = new\n\t\tdown(0,50) = lap\n"),
            "{config}"
        );
        assert!(
            config.contains("section: options\n\tkeystroke(F1) = switchToScreen(new)\nend"),
            "hotkeys come from the settings file: {config}"
        );

        // regenerating before a start keeps the same content
        std::fs::remove_file(&config_path).unwrap();
        persist().unwrap();
        assert_eq!(std::fs::read_to_string(&config_path).unwrap(), config);

        unsafe { std::env::remove_var("DESKFLOW_DIR") };
        let _ = std::fs::remove_dir_all(&dir);
    }
}
