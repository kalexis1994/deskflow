//! Tracks the running Deskflow core: live state over IPC, its log, and start/stop.

use std::collections::{HashSet, VecDeque};
use std::io::{Read, Seek, SeekFrom};
use std::path::PathBuf;
use std::process::Stdio;
use std::sync::{Arc, Mutex};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use serde::Serialize;
use tauri::{AppHandle, Emitter};
use tokio::io::{AsyncBufReadExt, AsyncRead, AsyncWriteExt, BufReader};
use tokio::process::{Child, Command};

use crate::clipboard::{self, ClipboardActivity};
use crate::ipc::{self, Connection, Handshake};
use crate::layout;
use crate::paths;
use crate::settings::{self, CoreMode, ProcessMode};
use crate::tls;

const LOG_CAPACITY: usize = 2000;
const LOG_HISTORY_BYTES: u64 = 256 * 1024;
const POLL: Duration = Duration::from_millis(500);

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Default)]
#[serde(rename_all = "camelCase")]
pub enum IpcStatus {
    #[default]
    Disconnected,
    Connected,
    VersionMismatch,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Default)]
#[serde(rename_all = "camelCase")]
pub enum ProcessStatus {
    #[default]
    Stopped,
    Starting,
    Running,
    Stopping,
}

/// Everything the UI needs to draw the status screen. Emitted whole on every change.
#[derive(Debug, Clone, Serialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct CoreState {
    pub ipc: IpcStatus,
    pub process: ProcessStatus,
    /// Core `ConnectionState` name: Disconnected, Connecting, Connected or Listening.
    pub connection: Option<String>,
    /// True when `connection` was guessed from the log because we attached to a core
    /// that had already announced its state to another client.
    pub connection_inferred: bool,
    pub clients: Vec<String>,
    pub tls: Option<String>,
    pub peer_fingerprint: Option<String>,
    /// A peer presented a fingerprint we do not trust yet; the person must decide.
    pub fingerprint_prompt: Option<FingerprintPrompt>,
    pub unrecognised_client: Option<String>,
    pub refusal: Option<String>,
    /// Unix time in ms at which the client will retry the connection.
    pub retry_at: Option<u64>,
    pub missing_layouts: Option<String>,
    pub core_version: Option<String>,
    pub last_error: Option<String>,
    /// Recent shared clipboard activity, newest first (kept while the app runs).
    pub clipboard: Vec<ClipboardActivity>,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct FingerprintPrompt {
    /// Lower-case hex SHA-256.
    pub fingerprint: String,
    /// Which side asks: a client meeting a new server, or a server meeting a new client.
    pub mode: CoreMode,
}

pub struct Hub {
    app: AppHandle,
    /// `<version>+<sha>` of the installed core, used for the IPC hello.
    pub version_id: String,
    state: Mutex<CoreState>,
    log: Mutex<VecDeque<String>>,
    core_writer: tokio::sync::Mutex<Option<ipc::Writer>>,
    child: tokio::sync::Mutex<Option<Child>>,
    /// Fingerprints rejected in this session, so they are not asked about again.
    dismissed: Mutex<HashSet<String>>,
}

fn now_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |d| d.as_millis() as u64)
}

/// Reads `deskflow-core --version`, e.g. `deskflow-core.exe: 1.27.0.5 (3aec96d5)` -> `1.27.0.5+3aec96d5`.
pub fn detect_version_id() -> Option<String> {
    let mut cmd = std::process::Command::new(paths::core_bin()?);
    cmd.arg("--version");
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
    }
    let out = cmd.output().ok()?;
    let text = format!(
        "{}\n{}",
        String::from_utf8_lossy(&out.stdout),
        String::from_utf8_lossy(&out.stderr)
    );
    parse_version_id(&text)
}

/// Accepts both `deskflow-core v1.27.0.5 (3aec96d5), protocol v1.8` and `deskflow-core: 1.27.0.5 (3aec96d5)`.
fn parse_version_id(text: &str) -> Option<String> {
    text.lines().find_map(|line| {
        let words: Vec<&str> = line.split_whitespace().collect();
        words.windows(2).find_map(|pair| {
            let version = pair[0].trim_start_matches('v');
            let sha = pair[1]
                .trim_end_matches(',')
                .strip_prefix('(')?
                .strip_suffix(')')?;
            let numeric = version.split('.').count() >= 3
                && version.split('.').all(|p| p.parse::<u32>().is_ok());
            numeric.then(|| format!("{version}+{sha}"))
        })
    })
}

impl Hub {
    pub fn new(app: AppHandle, version_id: Option<String>) -> Arc<Self> {
        let state = CoreState {
            core_version: version_id.as_ref().map(|v| v.replace('+', " · ")),
            ..Default::default()
        };
        Arc::new(Self {
            app,
            version_id: version_id.unwrap_or_else(|| "unknown+unknown".into()),
            state: Mutex::new(state),
            log: Mutex::new(VecDeque::with_capacity(LOG_CAPACITY)),
            core_writer: tokio::sync::Mutex::new(None),
            child: tokio::sync::Mutex::new(None),
            dismissed: Mutex::new(HashSet::new()),
        })
    }

    pub fn snapshot(&self) -> CoreState {
        self.state.lock().unwrap().clone()
    }

    pub fn log_lines(&self) -> Vec<String> {
        self.log.lock().unwrap().iter().cloned().collect()
    }

    fn update(&self, f: impl FnOnce(&mut CoreState)) {
        let snapshot = {
            let mut s = self.state.lock().unwrap();
            f(&mut s);
            s.clone()
        };
        let _ = self.app.emit("core-state", snapshot);
    }

    fn push_log(&self, lines: Vec<String>) {
        if lines.is_empty() {
            return;
        }
        // same check as CoreProcess::checkSecureSocket in the Qt GUI
        let tls = lines.iter().rev().find_map(|l| {
            let lower = l.to_lowercase();
            let i = lower.find("network encryption protocol: ")?;
            Some(
                l[i + "network encryption protocol: ".len()..]
                    .trim()
                    .to_owned(),
            )
        });
        {
            let mut log = self.log.lock().unwrap();
            for l in &lines {
                if log.len() == LOG_CAPACITY {
                    log.pop_front();
                }
                log.push_back(l.clone());
            }
        }
        let _ = self.app.emit("core-log", lines);

        // the history may arrive after we attached to the core, so refine the guess
        let inferred = self
            .snapshot()
            .connection_inferred
            .then(|| infer_connection(&self.log_lines()))
            .flatten();
        if tls.is_some() || inferred.is_some() {
            self.update(|s| {
                if let Some(tls) = tls {
                    s.tls = Some(tls);
                }
                if let (true, Some(state)) = (s.connection_inferred, inferred) {
                    s.connection = Some(state.to_owned());
                }
            });
        }
    }

    // ---- live state over the core IPC -------------------------------------------------

    pub async fn monitor_core(self: Arc<Self>) {
        loop {
            if let Ok((conn, handshake)) =
                Connection::connect(paths::CORE_IPC, &self.version_id).await
            {
                self.run_core_session(conn, handshake).await;
            }
            tokio::time::sleep(Duration::from_secs(1)).await;
        }
    }

    async fn run_core_session(&self, conn: Connection, handshake: Handshake) {
        let Connection { mut reader, writer } = conn;
        *self.core_writer.lock().await = Some(writer);

        let inferred = infer_connection(&self.log_lines());
        self.update(|s| {
            s.ipc = match handshake {
                Handshake::Ok => IpcStatus::Connected,
                Handshake::VersionMismatch(_) => IpcStatus::VersionMismatch,
            };
            s.process = ProcessStatus::Running;
            s.last_error = None;
            if s.connection.is_none() || s.connection.as_deref() == Some("Disconnected") {
                s.connection = inferred.map(str::to_owned).or(Some("Connecting".into()));
                s.connection_inferred = true;
            }
        });

        while let Ok(Some(line)) = reader.next_line().await {
            let (command, args) = ipc::parse(&line);
            if command == "bye" {
                break;
            }
            self.apply_event(command, args);
        }

        *self.core_writer.lock().await = None;
        let desktop_child_alive = self.child.lock().await.is_some();
        self.update(|s| {
            s.ipc = IpcStatus::Disconnected;
            if !desktop_child_alive {
                s.process = ProcessStatus::Stopped;
            }
            s.connection = None;
            s.connection_inferred = false;
            s.clients.clear();
            s.tls = None;
            s.retry_at = None;
        });
    }

    /// Decides whether a fingerprint the core just checked needs the person's approval.
    fn fingerprint_prompt(&self, fingerprint: &str) -> Option<FingerprintPrompt> {
        let fingerprint = tls::normalize(fingerprint)?;
        let ov = settings::read_overview().ok()?;
        let tls = tls::info().ok()?;
        // a server only checks its clients when peer checking is on
        let checked = ov.core_mode == CoreMode::Client
            || (ov.core_mode == CoreMode::Server && tls.check_peers);
        if !tls.enabled || !checked || tls::is_trusted(ov.core_mode, &fingerprint) {
            return None;
        }
        if self.dismissed.lock().unwrap().contains(&fingerprint) {
            return None;
        }
        Some(FingerprintPrompt {
            fingerprint,
            mode: ov.core_mode,
        })
    }

    pub fn clear_fingerprint_prompt(&self) {
        self.update(|s| s.fingerprint_prompt = None);
    }

    pub fn dismiss_fingerprint(&self, fingerprint: &str) {
        if let Some(f) = tls::normalize(fingerprint) {
            self.dismissed.lock().unwrap().insert(f);
        }
        self.clear_fingerprint_prompt();
    }

    fn apply_event(&self, command: &str, args: &str) {
        let args = args.to_owned();
        let prompt = (command == "peerFingerprint").then(|| self.fingerprint_prompt(&args));
        self.update(|s| match command {
            "connectionState" => {
                if args != "Connecting" {
                    s.retry_at = None;
                }
                if args == "Connected" || args == "Listening" {
                    s.refusal = None;
                }
                s.connection = Some(args);
                s.connection_inferred = false;
            }
            "connectedClients" => {
                s.clients = args
                    .split(',')
                    .filter(|c| !c.is_empty())
                    .map(str::to_owned)
                    .collect();
            }
            "secureSocket" => s.tls = Some(args),
            "unrecognisedClient" => s.unrecognised_client = Some(args),
            "connectionRefused" => s.refusal = Some(args),
            "retryIn" => s.retry_at = args.parse::<u64>().ok().map(|secs| now_ms() + secs * 1000),
            "peerFingerprint" => {
                s.peer_fingerprint = Some(args);
                if let Some(p) = prompt {
                    s.fingerprint_prompt = p;
                }
            }
            "missingKeyboardLayouts" => s.missing_layouts = Some(args),
            "clipboard" => {
                clipboard::push(&mut s.clipboard, &args, now_ms());
            }
            _ => {}
        });
    }

    // ---- log ---------------------------------------------------------------------------

    /// Follows the daemon log, which is where the core writes when it runs as a service.
    pub async fn tail_daemon_log(self: Arc<Self>) {
        let path = match ipc::daemon_request(&self.version_id, &["logPath".into()]).await {
            Ok(replies) => replies
                .first()
                .and_then(|r| r.strip_prefix("logPath="))
                .map(PathBuf::from)
                .unwrap_or_else(default_daemon_log),
            Err(_) => default_daemon_log(),
        };

        let mut offset = None::<u64>;
        let mut partial = String::new();
        loop {
            if let Ok(mut file) = std::fs::File::open(&path) {
                let len = file.metadata().map(|m| m.len()).unwrap_or(0);
                let start = match offset {
                    None => len.saturating_sub(LOG_HISTORY_BYTES),
                    Some(o) if o > len => 0, // rotated or truncated
                    Some(o) => o,
                };
                if len > start && file.seek(SeekFrom::Start(start)).is_ok() {
                    let mut buf = Vec::with_capacity((len - start) as usize);
                    if file.take(len - start).read_to_end(&mut buf).is_ok() {
                        partial.push_str(&String::from_utf8_lossy(&buf));
                        let mut lines: Vec<String> = partial
                            .split('\n')
                            .map(|l| l.trim_end_matches('\r').to_owned())
                            .collect();
                        partial = lines.pop().unwrap_or_default();
                        if offset.is_none() && start > 0 && !lines.is_empty() {
                            lines.remove(0); // first line is probably cut in half
                        }
                        self.push_log(lines.into_iter().filter(|l| !l.is_empty()).collect());
                    }
                }
                offset = Some(len);
            }
            tokio::time::sleep(POLL).await;
        }
    }

    fn pipe_output(self: &Arc<Self>, stream: impl AsyncRead + Unpin + Send + 'static) {
        let hub = self.clone();
        tauri::async_runtime::spawn(async move {
            let mut lines = BufReader::new(stream).lines();
            while let Ok(Some(line)) = lines.next_line().await {
                hub.push_log(vec![line]);
            }
        });
    }

    // ---- start / stop --------------------------------------------------------------------

    pub async fn start(self: &Arc<Self>) -> Result<(), String> {
        let ov = settings::read_overview()
            .map_err(|e| format!("No se pudo leer la configuración: {e}"))?;
        let mode = match ov.core_mode {
            CoreMode::Client => "client",
            CoreMode::Server => "server",
            CoreMode::None => {
                return Err(
                    "Primero elegí si esta computadora comparte su teclado o usa el de otra."
                        .into(),
                );
            }
        };
        if ov.core_mode == CoreMode::Client && ov.remote_host.trim().is_empty() {
            return Err("Falta la dirección de la computadora que comparte el teclado.".into());
        }
        if ov.core_mode == CoreMode::Server {
            layout::persist()?;
            if !server_config_file().is_file() {
                return Err(
                    "Todavía no hay un diseño de pantallas. Guardalo una vez desde el editor."
                        .into(),
                );
            }
        }

        self.update(|s| {
            s.process = ProcessStatus::Starting;
            s.connection = Some("Connecting".into());
            s.connection_inferred = false;
            s.last_error = None;
            s.refusal = None;
            s.peer_fingerprint = None;
            s.fingerprint_prompt = None;
            s.unrecognised_client = None;
            s.missing_layouts = None;
        });

        let result = match ov.process_mode {
            ProcessMode::Service => ipc::daemon_request(
                &self.version_id,
                &[format!("configFile={}", ov.settings_file), "start".into()],
            )
            .await
            .map(|_| ())
            .map_err(|e| format!("El servicio de Deskflow no respondió: {e}")),
            ProcessMode::Desktop => self.spawn_desktop(mode).await,
        };

        if let Err(e) = &result {
            let e = e.clone();
            self.update(|s| {
                s.process = ProcessStatus::Stopped;
                s.connection = None;
                s.last_error = Some(e);
            });
        }
        result
    }

    async fn spawn_desktop(self: &Arc<Self>, mode: &str) -> Result<(), String> {
        let bin = paths::core_bin().ok_or("No encontré deskflow-core.")?;
        let mut cmd = Command::new(bin);
        cmd.arg(mode)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .kill_on_drop(true);
        #[cfg(windows)]
        cmd.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
        let mut child = cmd
            .spawn()
            .map_err(|e| format!("No se pudo iniciar deskflow-core: {e}"))?;
        if let Some(out) = child.stdout.take() {
            self.pipe_output(out);
        }
        if let Some(err) = child.stderr.take() {
            self.pipe_output(err);
        }
        *self.child.lock().await = Some(child);

        let hub = self.clone();
        tauri::async_runtime::spawn(async move {
            loop {
                tokio::time::sleep(Duration::from_millis(300)).await;
                let mut guard = hub.child.lock().await;
                let Some(child) = guard.as_mut() else { return };
                if let Ok(Some(status)) = child.try_wait() {
                    *guard = None;
                    drop(guard);
                    hub.update(|s| {
                        s.process = ProcessStatus::Stopped;
                        s.connection = None;
                        if !status.success() {
                            s.last_error = Some(format!(
                                "deskflow-core terminó con código {:?}",
                                status.code()
                            ));
                        }
                    });
                    return;
                }
            }
        });
        Ok(())
    }

    pub async fn stop(self: &Arc<Self>) -> Result<(), String> {
        let ov = settings::read_overview().map_err(|e| e.to_string())?;
        let running = self.snapshot().ipc != IpcStatus::Disconnected;
        self.update(|s| {
            s.process = if running {
                ProcessStatus::Stopping
            } else {
                ProcessStatus::Stopped
            }
        });

        match ov.process_mode {
            ProcessMode::Service => {
                ipc::daemon_request(&self.version_id, &["stop".into()])
                    .await
                    .map_err(|e| format!("El servicio de Deskflow no respondió: {e}"))?;
            }
            ProcessMode::Desktop => {
                if let Some(w) = self.core_writer.lock().await.as_mut() {
                    let _ = w.write_all(b"stop\n").await;
                    let _ = w.flush().await;
                }
                // same fallback as the Qt GUI: terminate if it does not stop gracefully
                let hub = self.clone();
                tauri::async_runtime::spawn(async move {
                    tokio::time::sleep(Duration::from_millis(1500)).await;
                    if let Some(child) = hub.child.lock().await.as_mut() {
                        let _ = child.start_kill();
                    }
                });
            }
        }
        Ok(())
    }

    pub async fn restart(self: &Arc<Self>) -> Result<(), String> {
        if self.snapshot().process != ProcessStatus::Stopped {
            self.stop().await?;
            for _ in 0..20 {
                tokio::time::sleep(Duration::from_millis(250)).await;
                if self.snapshot().ipc == IpcStatus::Disconnected
                    && self.child.lock().await.is_none()
                {
                    break;
                }
            }
        }
        self.start().await
    }
}

fn default_daemon_log() -> PathBuf {
    paths::settings_dir().join("deskflow-daemon.log")
}

fn server_config_file() -> PathBuf {
    match settings::IniFile::load(&paths::settings_file()) {
        Ok(ini) => layout::server_config_file(&ini),
        Err(_) => paths::settings_dir().join("deskflow-server.conf"),
    }
}

/// Best guess of the connection state from the log, for a core that was already running
/// when we attached (it only announces state changes, and only to the clients it has then).
fn infer_connection(lines: &[String]) -> Option<&'static str> {
    let since_start = lines
        .iter()
        .rposition(|l| l.contains("starting core"))
        .map_or(lines, |i| &lines[i..]);
    since_start.iter().rev().find_map(|l| {
        let l = l.as_str();
        if l.contains("stopped client") || l.contains("stopped server") {
            Some("Disconnected")
        } else if l.contains("failed to connect to server")
            || l.contains("server is dead")
            || l.contains("started client")
        {
            Some("Connecting")
        } else if l.contains("connected to secure socket")
            || l.contains("entering computer")
            || l.contains("leaving computer")
            || l.contains("accepted client connection")
        {
            Some("Connected")
        } else if l.contains("started server") {
            Some("Listening")
        } else {
            None
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_version_output() {
        let out = "deskflow-core.exe v1.27.0.5 (3aec96d5), protocol v1.8
Copyright (C) 2024-2026 Deskflow Devs
";
        assert_eq!(parse_version_id(out).as_deref(), Some("1.27.0.5+3aec96d5"));
        assert_eq!(
            parse_version_id("deskflow-core: 1.26.0 (abc1234)").as_deref(),
            Some("1.26.0+abc1234")
        );
        assert_eq!(
            parse_version_id("Copyright (C) 2024-2026 Deskflow Devs"),
            None
        );
    }

    #[test]
    fn infers_state_from_latest_relevant_line() {
        let log = |xs: &[&str]| xs.iter().map(|s| s.to_string()).collect::<Vec<_>>();
        assert_eq!(
            infer_connection(&log(&["INFO: starting core", "INFO: started client"])),
            Some("Connecting")
        );
        assert_eq!(
            infer_connection(&log(&[
                "INFO: started client",
                "INFO: connected to secure socket",
                "INFO: clipboard"
            ])),
            Some("Connected")
        );
        assert_eq!(
            infer_connection(&log(&[
                "INFO: entering computer",
                "INFO: starting core",
                "INFO: started client"
            ])),
            Some("Connecting")
        );
        assert_eq!(infer_connection(&log(&["nothing useful"])), None);
    }
}
