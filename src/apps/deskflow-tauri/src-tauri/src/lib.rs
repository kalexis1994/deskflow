mod clipboard;
mod computer;
mod engine;
mod hotkeys;
mod ipc;
mod layout;
mod paths;
mod server_options;
mod settings;
mod tls;
mod tray;
mod update;

use std::sync::Arc;

use serde::Serialize;
use tauri::{Manager, State, WindowEvent};

use crate::engine::{CoreState, Hub};
use crate::settings::{Changes, Overview, ProcessMode};
use crate::update::Updates;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct AppInfo {
    overview: Overview,
    core_found: bool,
    core_dir: Option<String>,
}

#[tauri::command]
fn app_info() -> Result<AppInfo, String> {
    Ok(AppInfo {
        overview: settings::read_overview().map_err(|e| e.to_string())?,
        core_found: paths::core_bin().is_some(),
        core_dir: paths::core_dir().map(|d| d.to_string_lossy().into_owned()),
    })
}

/// Primary LAN address, so a server can tell its clients where to connect.
#[tauri::command]
fn local_address() -> Option<String> {
    let socket = std::net::UdpSocket::bind("0.0.0.0:0").ok()?;
    // connecting a UDP socket only picks a route; nothing is sent
    socket.connect("192.0.2.1:9").ok()?;
    socket.local_addr().ok().map(|a| a.ip().to_string())
}

#[tauri::command]
fn core_state(hub: State<'_, Arc<Hub>>) -> CoreState {
    hub.snapshot()
}

#[tauri::command]
fn core_log(hub: State<'_, Arc<Hub>>) -> Vec<String> {
    hub.log_lines()
}

#[tauri::command]
async fn start_core(hub: State<'_, Arc<Hub>>) -> Result<(), String> {
    hub.inner().start().await
}

#[tauri::command]
async fn stop_core(hub: State<'_, Arc<Hub>>) -> Result<(), String> {
    hub.inner().stop().await
}

/// Saves the changes and, if the core is running, restarts it so they take effect.
#[tauri::command]
async fn save_settings(hub: State<'_, Arc<Hub>>, changes: Changes) -> Result<Overview, String> {
    settings::apply_changes(&changes).map_err(|e| format!("No se pudo guardar: {e}"))?;
    if hub.snapshot().process != engine::ProcessStatus::Stopped {
        hub.inner().restart().await?;
    }
    settings::read_overview().map_err(|e| e.to_string())
}

#[tauri::command]
fn get_layout() -> Result<layout::LayoutView, String> {
    layout::load()
}

/// Saves the layout and, if this computer is serving, restarts the core to apply it.
#[tauri::command]
async fn save_layout(
    hub: State<'_, Arc<Hub>>,
    screens: Vec<layout::Screen>,
    renames: Vec<layout::Rename>,
) -> Result<layout::LayoutView, String> {
    let view = layout::save(screens, &renames)?;
    let serving =
        settings::read_overview().is_ok_and(|o| o.core_mode == settings::CoreMode::Server);
    if serving && hub.snapshot().process != engine::ProcessStatus::Stopped {
        hub.inner().restart().await?;
    }
    Ok(view)
}

#[tauri::command]
fn clipboard_prefs() -> Result<clipboard::ClipboardPrefs, String> {
    clipboard::prefs()
}

/// Turns the text preview of clipboard activity on or off; the core picks it up on
/// its next report, without a restart.
#[tauri::command]
fn set_clipboard_preview(enabled: bool) -> Result<clipboard::ClipboardPrefs, String> {
    clipboard::set_preview(enabled)
}

/// Turns copying files on or off and sets its limit on this computer; applies from
/// the next start of the core.
#[tauri::command]
fn set_file_sharing(enabled: bool, limit_mb: u32) -> Result<clipboard::ClipboardPrefs, String> {
    clipboard::set_files(enabled, limit_mb)
}

#[tauri::command]
fn get_server_options() -> Result<server_options::ServerOptions, String> {
    server_options::load()
}

/// Saves the global server options and restarts the core if it is serving.
#[tauri::command]
async fn save_server_options(
    hub: State<'_, Arc<Hub>>,
    options: server_options::ServerOptions,
) -> Result<server_options::ServerOptions, String> {
    let saved = server_options::save(&options)?;
    let serving =
        settings::read_overview().is_ok_and(|o| o.core_mode == settings::CoreMode::Server);
    if serving && hub.snapshot().process != engine::ProcessStatus::Stopped {
        hub.inner().restart().await?;
    }
    Ok(saved)
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct HotkeysView {
    hotkeys: Vec<hotkeys::Hotkey>,
    /// Computers of the layout, for actions that target one.
    computers: Vec<String>,
    /// This computer, which presets must not send keys to.
    server: String,
}

fn hotkeys_view() -> Result<HotkeysView, String> {
    let ini = settings::IniFile::load(&paths::settings_file()).map_err(|e| e.to_string())?;
    let layout = layout::load()?;
    Ok(HotkeysView {
        hotkeys: hotkeys::read(&ini),
        computers: layout.screens.into_iter().map(|s| s.name).collect(),
        server: layout.server_name,
    })
}

#[tauri::command]
fn get_hotkeys() -> Result<HotkeysView, String> {
    hotkeys_view()
}

/// Saves the hotkeys, regenerates the server config and restarts a running server.
#[tauri::command]
async fn save_hotkeys(
    hub: State<'_, Arc<Hub>>,
    hotkeys: Vec<hotkeys::Hotkey>,
) -> Result<HotkeysView, String> {
    let known: std::collections::HashSet<String> = layout::load()?
        .screens
        .into_iter()
        .flat_map(|s| {
            std::iter::once(s.name).chain(s.options.map(|o| o.aliases).unwrap_or_default())
        })
        .collect();
    hotkeys::validate(&hotkeys, &known)?;
    let mut ini = settings::IniFile::load(&paths::settings_file()).map_err(|e| e.to_string())?;
    hotkeys::write(&mut ini, &hotkeys);
    ini.save()
        .map_err(|e| format!("No se pudieron guardar los atajos: {e}"))?;
    layout::regenerate()?;
    let serving =
        settings::read_overview().is_ok_and(|o| o.core_mode == settings::CoreMode::Server);
    if serving && hub.snapshot().process != engine::ProcessStatus::Stopped {
        hub.inner().restart().await?;
    }
    hotkeys_view()
}

#[tauri::command]
fn tls_info() -> Result<tls::TlsInfo, String> {
    tls::info()
}

/// Trusts the fingerprint the core just reported. The core keeps retrying, so the
/// next attempt connects without a restart; a client that was stopped is started.
#[tauri::command]
async fn trust_fingerprint(
    hub: State<'_, Arc<Hub>>,
    fingerprint: String,
) -> Result<tls::TlsInfo, String> {
    let mode = settings::read_overview()
        .map_err(|e| e.to_string())?
        .core_mode;
    tls::trust(mode, &fingerprint)?;
    hub.clear_fingerprint_prompt();
    if mode == settings::CoreMode::Client
        && hub.snapshot().process == engine::ProcessStatus::Stopped
    {
        hub.inner().start().await?;
    }
    tls::info()
}

/// Rejects it for this session. A client stops, instead of retrying an untrusted server.
#[tauri::command]
async fn reject_fingerprint(hub: State<'_, Arc<Hub>>, fingerprint: String) -> Result<(), String> {
    hub.dismiss_fingerprint(&fingerprint);
    let mode = settings::read_overview()
        .map_err(|e| e.to_string())?
        .core_mode;
    if mode == settings::CoreMode::Client
        && hub.snapshot().process != engine::ProcessStatus::Stopped
    {
        hub.inner().stop().await?;
    }
    Ok(())
}

/// Removes a computer from a trusted list ("servers" or "clients").
#[tauri::command]
fn forget_fingerprint(list: String, kind: String, hex: String) -> Result<tls::TlsInfo, String> {
    let mode = if list == "clients" {
        settings::CoreMode::Server
    } else {
        settings::CoreMode::Client
    };
    tls::forget(mode, &kind, &hex)?;
    tls::info()
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct Autostart {
    /// Only the Windows service can start the core before anyone logs in.
    supported: bool,
    /// The service will start the core on boot, at the login screen.
    enabled: bool,
}

/// Whether the service remembers a config to start on boot (`daemon/configFile`,
/// set when the core is started through it and cleared when it is stopped).
#[tauri::command]
fn autostart_status() -> Autostart {
    let service =
        settings::read_overview().is_ok_and(|o| o.process_mode == settings::ProcessMode::Service);
    let enabled = paths::daemon_settings_file()
        .and_then(|f| settings::IniFile::load(&f).ok())
        .and_then(|ini| ini.get("daemon", "configFile"))
        .is_some_and(|f| !f.trim().is_empty());
    Autostart {
        supported: service,
        enabled: service && enabled,
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        // opening the app again shows the one in the tray instead of adding a second icon
        .plugin(tauri_plugin_single_instance::init(|app, args, _| {
            if !args.iter().any(|a| a == tray::HIDDEN_ARG) {
                tray::show_window(app);
            }
        }))
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_updater::Builder::new().build())
        .on_window_event(|window, event| {
            if let WindowEvent::CloseRequested { api, .. } = event
                && tray::available(window.app_handle())
            {
                api.prevent_close();
                let _ = window.hide();
            }
        })
        .setup(|app| {
            let hub = Hub::new(app.handle().clone(), engine::detect_version_id());
            app.manage(hub.clone());
            app.manage(Updates::new(
                app.package_info().version.to_string(),
                update::saved_channel(app.handle()),
            ));
            let tray_created = tray::create(app.handle());
            if let Err(e) = &tray_created {
                eprintln!("no tray icon: {e}");
            }
            if tray_created.is_err() || !std::env::args().any(|a| a == tray::HIDDEN_ARG) {
                tray::show_window(app.handle());
            }
            tauri::async_runtime::spawn(hub.clone().monitor_core());
            update::watch(app.handle().clone());
            let service =
                settings::read_overview().is_ok_and(|o| o.process_mode == ProcessMode::Service);
            if service {
                tauri::async_runtime::spawn(hub.tail_daemon_log());
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            app_info,
            local_address,
            core_state,
            core_log,
            start_core,
            stop_core,
            save_settings,
            get_layout,
            save_layout,
            get_server_options,
            save_server_options,
            get_hotkeys,
            save_hotkeys,
            tls_info,
            trust_fingerprint,
            reject_fingerprint,
            forget_fingerprint,
            autostart_status,
            clipboard_prefs,
            set_clipboard_preview,
            set_file_sharing,
            update::update_state,
            update::check_for_updates,
            update::set_update_channel,
            update::dismiss_update,
            update::install_update
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
