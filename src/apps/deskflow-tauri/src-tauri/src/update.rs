//! New versions, from the releases of the fork on GitHub (see docs/dev/updates.md).
//!
//! The app only looks for them on its own. Installing cuts Deskflow off for a moment,
//! and on a computer with no keyboard of its own that means losing control of it, so
//! nothing is installed until the person says so: `install_update` is only reached
//! from the button.
//!
//! Every release is a complete installer and the feed names only the latest one, so
//! any version goes straight to the newest without passing through the ones between.

use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter, Manager, State, Url};
use tauri_plugin_updater::{Update, UpdaterExt};

use crate::engine::Hub;
use crate::ipc;
use crate::settings::{self, ProcessMode};
use crate::tray;

/// Lets the app settle first: this also runs right after logging in.
const FIRST_CHECK: Duration = Duration::from_secs(30);
const CHECK_EVERY: Duration = Duration::from_secs(6 * 60 * 60);
/// Someone on this channel is waiting for a build to try.
const CHECK_ALPHA_EVERY: Duration = Duration::from_secs(10 * 60);
const WATCH_TICK: Duration = Duration::from_secs(60);
/// Reading and checking the installer takes the service well under a second.
const SERVICE_ANSWER: Duration = Duration::from_secs(30);

/// Always the newest alpha; the stable feed is in `tauri.conf.json`.
const ALPHA_FEED: &str =
    "https://github.com/kalexis1994/deskflow/releases/download/alpha-channel/latest.json";

/// Which releases this computer is offered. Asks the same way on both.
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum Channel {
    #[default]
    Stable,
    Alpha,
}

impl Channel {
    fn parse(text: &str) -> Self {
        match text.trim() {
            "alpha" => Self::Alpha,
            _ => Self::Stable,
        }
    }

    fn name(self) -> &'static str {
        match self {
            Self::Stable => "stable",
            Self::Alpha => "alpha",
        }
    }

    fn check_every(self) -> Duration {
        match self {
            Self::Stable => CHECK_EVERY,
            Self::Alpha => CHECK_ALPHA_EVERY,
        }
    }
}

/// A file of its own: the service restarts the core when the settings file changes,
/// and choosing a channel must not cut anyone off.
fn channel_file(app: &AppHandle) -> Option<PathBuf> {
    let dir = app.path().app_config_dir().ok()?;
    Some(dir.join("update-channel"))
}

pub fn saved_channel(app: &AppHandle) -> Channel {
    channel_file(app)
        .and_then(|file| std::fs::read_to_string(file).ok())
        .map(|text| Channel::parse(&text))
        .unwrap_or_default()
}

#[derive(Clone, Debug, Default, PartialEq, Serialize)]
#[serde(tag = "kind", rename_all = "camelCase")]
pub enum Phase {
    #[default]
    Idle,
    Checking,
    UpToDate,
    /// Waiting for the person to decide. `error` says why the last attempt failed.
    Available {
        version: String,
        notes: Option<String>,
        error: Option<String>,
    },
    Downloading {
        version: String,
        percent: Option<u8>,
    },
    Installing {
        version: String,
    },
    /// A search the person asked for failed; a background one fails quietly.
    Failed {
        message: String,
    },
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct UpdateState {
    current: String,
    channel: Channel,
    #[serde(flatten)]
    phase: Phase,
    /// Answered "not now" for this version.
    dismissed: bool,
}

pub struct Updates {
    inner: Mutex<Inner>,
}

struct Inner {
    current: String,
    channel: Channel,
    phase: Phase,
    found: Option<Update>,
    /// The version the person said "not now" to: a newer one asks again.
    dismissed: Option<String>,
    /// The version that already brought the window up, so it does it only once.
    announced: Option<String>,
}

impl Inner {
    fn view(&self) -> UpdateState {
        let dismissed = match &self.phase {
            Phase::Available { version, .. } => self.dismissed.as_ref() == Some(version),
            _ => false,
        };
        UpdateState {
            current: self.current.clone(),
            channel: self.channel,
            phase: self.phase.clone(),
            dismissed,
        }
    }

    fn busy(&self) -> bool {
        matches!(
            self.phase,
            Phase::Checking | Phase::Downloading { .. } | Phase::Installing { .. }
        )
    }
}

impl Updates {
    pub fn new(current: String, channel: Channel) -> Arc<Self> {
        Arc::new(Self {
            inner: Mutex::new(Inner {
                current,
                channel,
                phase: Phase::Idle,
                found: None,
                dismissed: None,
                announced: None,
            }),
        })
    }

    fn change(&self, app: &AppHandle, f: impl FnOnce(&mut Inner)) {
        let (view, offered) = {
            let mut inner = self.inner.lock().unwrap();
            f(&mut inner);
            let offered = match &inner.phase {
                Phase::Available { version, .. } => Some(version.clone()),
                _ => None,
            };
            (inner.view(), offered)
        };
        tray::show_update(app, offered.as_deref());
        let _ = app.emit("update-state", view);
    }

    async fn check(&self, app: &AppHandle, asked: bool) {
        let channel = {
            let mut inner = self.inner.lock().unwrap();
            if inner.busy() {
                return;
            }
            if asked {
                inner.phase = Phase::Checking;
            }
            inner.channel
        };
        if asked {
            self.change(app, |_| {});
        }

        let result = search(app, channel).await;
        let mut announce = false;
        self.change(app, |inner| {
            // started installing while this was searching, or changed channel
            if inner.channel != channel
                || matches!(
                    inner.phase,
                    Phase::Downloading { .. } | Phase::Installing { .. }
                )
            {
                return;
            }
            match result {
                Ok(Some(update)) => {
                    let version = update.version.clone();
                    announce = inner.announced.as_ref() != Some(&version)
                        && inner.dismissed.as_ref() != Some(&version);
                    inner.announced = Some(version.clone());
                    inner.phase = Phase::Available {
                        version,
                        notes: update.body.clone(),
                        error: None,
                    };
                    inner.found = Some(update);
                }
                Ok(None) => {
                    inner.phase = Phase::UpToDate;
                    inner.found = None;
                }
                Err(e) => {
                    eprintln!("no update check: {e}");
                    let known = inner.found.as_ref().map(|update| Phase::Available {
                        version: update.version.clone(),
                        notes: update.body.clone(),
                        error: None,
                    });
                    inner.phase = known.unwrap_or(if asked {
                        Phase::Failed {
                            message: format!("No se pudo buscar actualizaciones: {e}"),
                        }
                    } else {
                        Phase::Idle
                    });
                }
            }
        });
        if announce {
            // the app lives hidden: the question has to reach the person
            tray::show_window(app);
        }
    }
}

async fn search(
    app: &AppHandle,
    channel: Channel,
) -> Result<Option<Update>, tauri_plugin_updater::Error> {
    let exiting = app.clone();
    let mut builder = app
        .updater_builder()
        // only the icon: refusing the administrator prompt leaves the app running,
        // and the installer stops the core itself
        .on_before_exit(move || tray::set_visible(&exiting, false));
    if channel == Channel::Alpha {
        builder = builder.endpoints(vec![alpha_feed()])?;
    }
    builder.build()?.check().await
}

fn alpha_feed() -> Url {
    Url::parse(ALPHA_FEED).expect("a fixed address")
}

/// Looks for a new version now and then, for as long as the app runs.
pub fn watch(app: AppHandle) {
    // a development build has nothing to install over
    if cfg!(debug_assertions) {
        return;
    }
    tauri::async_runtime::spawn(async move {
        tokio::time::sleep(FIRST_CHECK).await;
        let updates = app.state::<Arc<Updates>>();
        updates.check(&app, false).await;
        let mut checked = Instant::now();
        loop {
            // in steps: the channel, and so the wait, can change in the meantime
            tokio::time::sleep(WATCH_TICK).await;
            let channel = updates.inner.lock().unwrap().channel;
            if checked.elapsed() >= channel.check_every() {
                updates.check(&app, false).await;
                checked = Instant::now();
            }
        }
    });
}

/// From the tray: the question again, even after "not now".
pub fn ask_again(app: &AppHandle) {
    app.state::<Arc<Updates>>()
        .change(app, |inner| inner.dismissed = None);
    tray::show_window(app);
}

#[tauri::command]
pub fn update_state(updates: State<'_, Arc<Updates>>) -> UpdateState {
    updates.inner.lock().unwrap().view()
}

#[tauri::command]
pub async fn check_for_updates(
    app: AppHandle,
    updates: State<'_, Arc<Updates>>,
) -> Result<(), String> {
    updates.check(&app, true).await;
    Ok(())
}

/// Follows another channel from now on, and looks there right away. What the old one
/// offered stops being offered; nothing already installed changes.
#[tauri::command]
pub async fn set_update_channel(
    app: AppHandle,
    updates: State<'_, Arc<Updates>>,
    channel: Channel,
) -> Result<(), String> {
    if updates.inner.lock().unwrap().busy() {
        return Err("Esperá a que termine la actualización en curso".into());
    }
    let file = channel_file(&app).ok_or("No encontré dónde guardar el canal")?;
    if let Some(dir) = file.parent() {
        std::fs::create_dir_all(dir).map_err(|e| format!("No se pudo guardar el canal: {e}"))?;
    }
    std::fs::write(&file, channel.name())
        .map_err(|e| format!("No se pudo guardar el canal: {e}"))?;

    updates.change(&app, |inner| {
        inner.channel = channel;
        inner.found = None;
        inner.dismissed = None;
        inner.announced = None;
        inner.phase = Phase::Idle;
    });
    updates.check(&app, true).await;
    Ok(())
}

/// The answer to "not now": no more asking until the app starts again or a newer
/// version comes out. The tray keeps the way back.
#[tauri::command]
pub fn dismiss_update(app: AppHandle, updates: State<'_, Arc<Updates>>) {
    updates.change(&app, |inner| {
        if let Phase::Available { version, .. } = &inner.phase {
            inner.dismissed = Some(version.clone());
        }
    });
}

/// The person said yes. Returns only if it failed: otherwise the app exits here, for
/// the installer to replace it.
///
/// Under the service, the service installs it, so nothing else needs a click: on a
/// computer controlled through Deskflow, Windows' permission prompt would come up with
/// nobody able to answer it once the installer stops the core. Elsewhere, or if the
/// service won't, Windows asks for permission as for any installer.
#[tauri::command]
pub async fn install_update(
    app: AppHandle,
    updates: State<'_, Arc<Updates>>,
) -> Result<(), String> {
    let update = {
        let inner = updates.inner.lock().unwrap();
        if inner.busy() {
            return Ok(());
        }
        inner.found.clone()
    };
    let Some(update) = update else {
        return Err("No hay ninguna actualización para instalar".into());
    };
    let version = update.version.clone();

    updates.change(&app, |inner| {
        inner.phase = Phase::Downloading {
            version: version.clone(),
            percent: None,
        };
    });

    let mut received = 0u64;
    let mut shown = None;
    // checks the signature too
    let downloaded = update
        .download(
            |chunk, total| {
                received += chunk as u64;
                let percent = percent_of(received, total);
                if percent != shown {
                    shown = percent;
                    updates.change(&app, |inner| {
                        inner.phase = Phase::Downloading {
                            version: version.clone(),
                            percent,
                        };
                    });
                }
            },
            || {},
        )
        .await;
    let bytes = match downloaded {
        Ok(bytes) => bytes,
        Err(e) => {
            failed(&app, &updates, &update, &e.to_string());
            return Ok(());
        }
    };
    updates.change(&app, |inner| {
        inner.phase = Phase::Installing {
            version: version.clone(),
        };
    });

    let service = settings::read_overview().is_ok_and(|o| o.process_mode == ProcessMode::Service);
    if service {
        match through_service(&app, &update, &bytes).await {
            Ok(()) => {
                // the installer closes the app anyway, and opens it again when it is done
                tray::set_visible(&app, false);
                app.exit(0);
                return Ok(());
            }
            Err(e) => eprintln!("the service didn't install the update, asking Windows: {e}"),
        }
    }

    // runs the installer and exits; it returns only if it couldn't
    if let Err(e) = update.install(&bytes) {
        failed(&app, &updates, &update, &e.to_string());
    }
    Ok(())
}

fn failed(app: &AppHandle, updates: &Updates, update: &Update, reason: &str) {
    eprintln!("no update: {reason}");
    tray::set_visible(app, true);
    let version = update.version.clone();
    updates.change(app, |inner| {
        inner.dismissed = None;
        inner.phase = Phase::Available {
            version: version.clone(),
            notes: update.body.clone(),
            error: Some(format!(
                "No se pudo instalar la versión {version}, y Deskflow sigue como estaba: {reason}"
            )),
        };
    });
}

/// Leaves the installer and its signature where the service can read them, and asks it
/// to install them. The service checks both again on its own copy.
async fn through_service(app: &AppHandle, update: &Update, bytes: &[u8]) -> Result<(), String> {
    use base64::Engine;

    let signature = base64::engine::general_purpose::STANDARD
        .decode(update.signature.trim())
        .map_err(|e| format!("unreadable signature: {e}"))?;
    let dir = std::env::temp_dir().join("deskflow-update");
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let installer = dir.join(installer_name(&update.version));
    std::fs::write(&installer, bytes).map_err(|e| e.to_string())?;
    let mut signature_path = installer.clone().into_os_string();
    signature_path.push(".sig");
    std::fs::write(&signature_path, signature).map_err(|e| e.to_string())?;

    let version_id = app.state::<Arc<Hub>>().version_id.clone();
    let request = format!("installUpdate={}", installer.to_string_lossy());
    // an older service doesn't know the request and never answers it
    tokio::time::timeout(SERVICE_ANSWER, ipc::daemon_request(&version_id, &[request]))
        .await
        .map_err(|_| "the service didn't answer".to_string())?
        .map_err(|e| e.to_string())?;
    Ok(())
}

/// As the release names it, which is what its signature says it is.
fn installer_name(version: &str) -> String {
    format!("Deskflow-Tauri_{version}_x64-setup.exe")
}

fn percent_of(received: u64, total: Option<u64>) -> Option<u8> {
    let total = total.filter(|t| *t > 0)?;
    Some((received.min(total) * 100 / total) as u8)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn inner(phase: Phase, dismissed: Option<&str>) -> Inner {
        Inner {
            current: "0.1.8".into(),
            channel: Channel::Stable,
            phase,
            found: None,
            dismissed: dismissed.map(Into::into),
            announced: None,
        }
    }

    fn available(version: &str) -> Phase {
        Phase::Available {
            version: version.into(),
            notes: None,
            error: None,
        }
    }

    #[test]
    fn not_now_is_about_one_version() {
        assert!(inner(available("0.1.9"), Some("0.1.9")).view().dismissed);
        assert!(!inner(available("0.2.0"), Some("0.1.9")).view().dismissed);
        assert!(!inner(available("0.1.9"), None).view().dismissed);
    }

    #[test]
    fn nothing_runs_twice() {
        assert!(inner(Phase::Checking, None).busy());
        assert!(
            inner(
                Phase::Downloading {
                    version: "0.1.9".into(),
                    percent: None
                },
                None
            )
            .busy()
        );
        assert!(!inner(available("0.1.9"), None).busy());
        assert!(!inner(Phase::UpToDate, None).busy());
    }

    #[test]
    fn progress_needs_a_known_size() {
        assert_eq!(percent_of(50, None), None);
        assert_eq!(percent_of(50, Some(0)), None);
        assert_eq!(percent_of(50, Some(200)), Some(25));
        assert_eq!(percent_of(300, Some(200)), Some(100));
    }

    /// The service checks updates with the key in its own code (MSWindowsUpdate.h).
    #[test]
    fn the_service_checks_with_the_same_key() {
        let conf: serde_json::Value =
            serde_json::from_str(include_str!("../tauri.conf.json")).unwrap();
        let ours = conf["plugins"]["updater"]["pubkey"].as_str().unwrap();

        let header = include_str!("../../../../lib/platform/MSWindowsUpdate.h");
        let start = header.find("kPublicKey =").unwrap();
        let end = start + header[start..].find(';').unwrap();
        let theirs: String = header[start..end].split('"').skip(1).step_by(2).collect();
        assert_eq!(theirs, ours);
    }

    #[test]
    fn names_the_installer_as_its_signature_does() {
        assert_eq!(
            installer_name("0.1.13"),
            "Deskflow-Tauri_0.1.13_x64-setup.exe"
        );
    }

    #[test]
    fn stable_unless_told_otherwise() {
        assert_eq!(Channel::parse("alpha\n"), Channel::Alpha);
        assert_eq!(Channel::parse("stable"), Channel::Stable);
        assert_eq!(Channel::parse("anything else"), Channel::Stable);
        assert_eq!(Channel::parse(""), Channel::Stable);
        assert_eq!(Channel::parse(Channel::Alpha.name()), Channel::Alpha);
        assert_eq!(Channel::parse(Channel::Stable.name()), Channel::Stable);
    }

    #[test]
    fn alpha_looks_more_often_at_its_own_feed() {
        assert!(Channel::Alpha.check_every() < Channel::Stable.check_every());
        assert_eq!(alpha_feed().host_str(), Some("github.com"));
    }

    #[test]
    fn the_interface_reads_a_flat_state() {
        let state = inner(available("0.1.9"), None).view();
        let json = serde_json::to_value(state).unwrap();
        assert_eq!(json["kind"], "available");
        assert_eq!(json["version"], "0.1.9");
        assert_eq!(json["current"], "0.1.8");
        assert_eq!(json["channel"], "stable");
        assert_eq!(json["dismissed"], false);
    }
}
