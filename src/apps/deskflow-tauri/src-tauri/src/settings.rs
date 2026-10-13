//! Just enough of Qt's `QSettings::IniFormat` to read and update a handful of
//! plain keys while leaving every other line of the file untouched.

use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

use crate::paths;

pub struct IniFile {
    path: PathBuf,
    lines: Vec<String>,
}

impl IniFile {
    pub fn load(path: &Path) -> std::io::Result<Self> {
        let text = match std::fs::read_to_string(path) {
            Ok(t) => t,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => String::new(),
            Err(e) => return Err(e),
        };
        Ok(Self { path: path.to_path_buf(), lines: text.lines().map(str::to_owned).collect() })
    }

    /// Index range `[start, end)` of the body of `[section]`, if present.
    fn section_range(&self, section: &str) -> Option<(usize, usize)> {
        let header = format!("[{section}]");
        let start = self.lines.iter().position(|l| l.trim() == header)? + 1;
        let end = self.lines[start..]
            .iter()
            .position(|l| l.trim_start().starts_with('['))
            .map_or(self.lines.len(), |i| start + i);
        Some((start, end))
    }

    fn find_key(&self, section: &str, key: &str) -> Option<usize> {
        let (start, end) = self.section_range(section)?;
        (start..end).find(|&i| {
            self.lines[i].split_once('=').is_some_and(|(k, _)| k.trim() == key)
        })
    }

    pub fn get(&self, section: &str, key: &str) -> Option<String> {
        let i = self.find_key(section, key)?;
        let (_, v) = self.lines[i].split_once('=')?;
        Some(unquote(v.trim()))
    }

    pub fn set(&mut self, section: &str, key: &str, value: &str) {
        let line = format!("{key}={}", quote(value));
        if let Some(i) = self.find_key(section, key) {
            self.lines[i] = line;
            return;
        }
        match self.section_range(section) {
            Some((start, end)) => {
                // insert after the last non-empty line of the section
                let at = (start..end).rev().find(|&i| !self.lines[i].trim().is_empty()).map_or(start, |i| i + 1);
                self.lines.insert(at, line);
            }
            None => {
                if self.lines.last().is_some_and(|l| !l.trim().is_empty()) {
                    self.lines.push(String::new());
                }
                self.lines.push(format!("[{section}]"));
                self.lines.push(line);
            }
        }
    }

    pub fn save(&self) -> std::io::Result<()> {
        if let Some(dir) = self.path.parent() {
            std::fs::create_dir_all(dir)?;
        }
        let mut text = self.lines.join("\n");
        text.push('\n');
        std::fs::write(&self.path, text)
    }
}

fn unquote(v: &str) -> String {
    if v.len() >= 2 && v.starts_with('"') && v.ends_with('"') {
        let inner = &v[1..v.len() - 1];
        let mut out = String::with_capacity(inner.len());
        let mut chars = inner.chars();
        while let Some(c) = chars.next() {
            if c == '\\' {
                if let Some(n) = chars.next() {
                    out.push(n);
                }
            } else {
                out.push(c);
            }
        }
        return out;
    }
    v.to_owned()
}

fn quote(v: &str) -> String {
    let plain = v.chars().all(|c| c.is_ascii_alphanumeric() || "._-:@/[]".contains(c));
    if plain {
        v.to_owned()
    } else {
        format!("\"{}\"", v.replace('\\', "\\\\").replace('"', "\\\""))
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum CoreMode {
    None,
    Client,
    Server,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ProcessMode {
    Service,
    Desktop,
}

/// The subset of the Deskflow settings the prototype shows and edits.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Overview {
    pub settings_file: String,
    pub computer_name: String,
    pub core_mode: CoreMode,
    pub process_mode: ProcessMode,
    pub remote_host: String,
    pub port: u16,
    pub tls_enabled: bool,
    pub portable: bool,
}

fn host_name() -> String {
    std::env::var("COMPUTERNAME")
        .or_else(|_| std::env::var("HOSTNAME"))
        .unwrap_or_else(|_| "this-computer".into())
}

pub fn read_overview() -> std::io::Result<Overview> {
    let path = paths::settings_file();
    let ini = IniFile::load(&path)?;
    let core_mode = match ini.get("core", "coreMode").as_deref() {
        Some("1") => CoreMode::Client,
        Some("2") => CoreMode::Server,
        _ => CoreMode::None,
    };
    // default mirrors Settings::defaultValue(Core::ProcessMode)
    let default_process = if cfg!(windows) && !paths::is_portable() { ProcessMode::Service } else { ProcessMode::Desktop };
    let process_mode = match ini.get("core", "processMode").as_deref() {
        Some("0") => ProcessMode::Service,
        Some("1") => ProcessMode::Desktop,
        _ => default_process,
    };
    Ok(Overview {
        settings_file: path.to_string_lossy().into_owned(),
        computer_name: ini.get("core", "computerName").filter(|s| !s.is_empty()).unwrap_or_else(host_name),
        core_mode,
        process_mode,
        remote_host: ini.get("client", "remoteHost").unwrap_or_default(),
        port: ini.get("core", "port").and_then(|p| p.parse().ok()).unwrap_or(24800),
        tls_enabled: ini.get("security", "tlsEnabled").is_none_or(|v| v == "true"),
        portable: paths::is_portable(),
    })
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Changes {
    pub core_mode: Option<CoreMode>,
    pub remote_host: Option<String>,
}

pub fn apply_changes(changes: &Changes) -> std::io::Result<()> {
    let mut ini = IniFile::load(&paths::settings_file())?;
    if let Some(mode) = changes.core_mode {
        let n = match mode {
            CoreMode::None => "0",
            CoreMode::Client => "1",
            CoreMode::Server => "2",
        };
        ini.set("core", "coreMode", n);
    }
    if let Some(host) = &changes.remote_host {
        ini.set("client", "remoteHost", host.trim());
    }
    ini.save()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ini(text: &str) -> IniFile {
        IniFile { path: PathBuf::new(), lines: text.lines().map(str::to_owned).collect() }
    }

    #[test]
    fn reads_and_updates_without_touching_other_lines() {
        let mut f = ini("[core]\ncomputerName=a\ncoreMode=1\n\n[client]\nremoteHost=10.0.0.1\n\n[server]\nx=\"a, b\"");
        assert_eq!(f.get("core", "coreMode").as_deref(), Some("1"));
        assert_eq!(f.get("server", "x").as_deref(), Some("a, b"));
        f.set("core", "coreMode", "2");
        f.set("client", "remoteHost", "pc.local");
        f.set("core", "port", "24801");
        f.set("security", "tlsEnabled", "true");
        assert_eq!(
            f.lines.join("\n"),
            "[core]\ncomputerName=a\ncoreMode=2\nport=24801\n\n[client]\nremoteHost=pc.local\n\n[server]\nx=\"a, b\"\n\n[security]\ntlsEnabled=true"
        );
    }
}
