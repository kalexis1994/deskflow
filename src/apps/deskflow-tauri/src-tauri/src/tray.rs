//! The icon in the notification area. The service runs the core whether or not this
//! app is open, so the app lives here: it starts hidden when the user logs in
//! (`--tray`, registered by the installer) and closing the window only hides it.

use std::sync::{Arc, Mutex};

use tauri::menu::{Menu, MenuItem, PredefinedMenuItem};
use tauri::tray::{MouseButton, MouseButtonState, TrayIcon, TrayIconBuilder, TrayIconEvent};
use tauri::{AppHandle, Manager, Wry};

use crate::engine::{CoreState, Hub, IpcStatus, ProcessStatus};
use crate::{i18n, update};

/// Starts without showing the window.
pub const HIDDEN_ARG: &str = "--tray";

/// Where the update item goes: below the status, above "Abrir".
const UPDATE_POSITION: usize = 2;

pub struct Tray {
    icon: TrayIcon,
    menu: Menu<Wry>,
    status: MenuItem<Wry>,
    update: MenuItem<Wry>,
    open: MenuItem<Wry>,
    quit: MenuItem<Wry>,
    shown: Mutex<Shown>,
}

#[derive(Default)]
struct Shown {
    status: String,
    waiting_for_person: bool,
    update: Option<String>,
}

/// One line for the tooltip and the menu; the same words as `describeStatus` in
/// `src/status.ts`, as far as the core's state alone tells.
fn status_line(core: &CoreState) -> String {
    match core.process {
        ProcessStatus::Starting => return i18n::t("tray.starting"),
        ProcessStatus::Stopping => return i18n::t("tray.stopping"),
        ProcessStatus::Stopped => return i18n::t("tray.off"),
        ProcessStatus::Running => {}
    }
    if core.ipc == IpcStatus::Disconnected {
        return i18n::t("tray.off");
    }
    match core.connection.as_deref() {
        Some("Connected") => match core.clients.as_slice() {
            [] => i18n::t("tray.connected"),
            [client] => i18n::tf("tray.sharingWith", &[("name", client)]),
            clients => i18n::tf("tray.sharingWithMany", &[("count", &clients.len())]),
        },
        Some("Listening") => i18n::t("tray.listening"),
        Some("Connecting") => i18n::t("tray.connecting"),
        _ => i18n::t("tray.disconnected"),
    }
}

/// A trust decision only a person can make, which a hidden window wouldn't show.
fn waiting_for_person(core: &CoreState) -> bool {
    core.fingerprint_prompt.is_some() || core.unrecognised_client.is_some()
}

pub fn create(app: &AppHandle) -> tauri::Result<()> {
    let status = MenuItem::with_id(app, "status", "Deskflow", false, None::<&str>)?;
    let update = MenuItem::with_id(app, "update", i18n::t("tray.update"), true, None::<&str>)?;
    let open = MenuItem::with_id(app, "open", i18n::t("tray.open"), true, None::<&str>)?;
    let quit = MenuItem::with_id(app, "quit", i18n::t("tray.quit"), true, None::<&str>)?;
    let menu = Menu::with_items(
        app,
        &[
            &status,
            &PredefinedMenuItem::separator(app)?,
            &open,
            &PredefinedMenuItem::separator(app)?,
            &quit,
        ],
    )?;

    let mut builder = TrayIconBuilder::with_id("main")
        .tooltip("Deskflow")
        .menu(&menu)
        .show_menu_on_left_click(false)
        .on_menu_event(|app, event| match event.id().as_ref() {
            "open" => show_window(app),
            // only brings up the question: installing is a decision for its buttons
            "update" => update::ask_again(app),
            "quit" => quit_app(app),
            _ => {}
        })
        .on_tray_icon_event(|tray, event| {
            if let TrayIconEvent::Click {
                button: MouseButton::Left,
                button_state: MouseButtonState::Up,
                ..
            } = event
            {
                show_window(tray.app_handle());
            }
        });
    if let Some(icon) = app.default_window_icon() {
        builder = builder.icon(icon.clone());
    }
    let icon = builder.build(app)?;

    app.manage(Tray {
        icon,
        menu,
        status,
        update,
        open,
        quit,
        shown: Mutex::new(Shown::default()),
    });
    Ok(())
}

/// Whether the icon exists: without it a hidden window couldn't be brought back.
pub fn available(app: &AppHandle) -> bool {
    app.try_state::<Tray>().is_some()
}

pub fn show_window(app: &AppHandle) {
    if let Some(window) = app.get_webview_window("main") {
        let _ = window.show();
        let _ = window.unminimize();
        let _ = window.set_focus();
    }
}

fn quit_app(app: &AppHandle) {
    let app = app.clone();
    tauri::async_runtime::spawn(async move {
        app.state::<Arc<Hub>>().quit().await;
        set_visible(&app, false);
        app.exit(0);
    });
}

/// Hides the icon of an app about to exit: Windows would keep showing a dead one.
pub fn set_visible(app: &AppHandle, visible: bool) {
    if let Some(tray) = app.try_state::<Tray>() {
        let _ = tray.icon.set_visible(visible);
    }
}

/// Offers the version waiting for an answer, or stops offering it.
pub fn show_update(app: &AppHandle, version: Option<&str>) {
    let Some(tray) = app.try_state::<Tray>() else {
        return;
    };
    let mut shown = tray.shown.lock().unwrap();
    if shown.update.as_deref() == version {
        return;
    }
    match version {
        Some(version) => {
            let _ = tray
                .update
                .set_text(i18n::tf("tray.updateTo", &[("version", &version)]));
            if shown.update.is_none() {
                let _ = tray.menu.insert(&tray.update, UPDATE_POSITION);
            }
        }
        None => {
            let _ = tray.menu.remove(&tray.update);
        }
    }
    shown.update = version.map(Into::into);
    set_tooltip(&tray, &shown);
}

/// The state, and a new version when there is one: the tray icon is where an update
/// shows up, as the window doesn't open for it
fn set_tooltip(tray: &Tray, shown: &Shown) {
    let mut text = format!("Deskflow: {}", shown.status);
    if let Some(version) = &shown.update {
        text.push('\n');
        text.push_str(&i18n::tf("tray.updateAvailable", &[("version", version)]));
    }
    let _ = tray.icon.set_tooltip(Some(text.as_str()));
}

/// Says everything again in the language just chosen.
pub fn relabel(app: &AppHandle) {
    let Some(tray) = app.try_state::<Tray>() else {
        return;
    };
    let _ = tray.open.set_text(i18n::t("tray.open"));
    let _ = tray.quit.set_text(i18n::t("tray.quit"));
    let update = {
        let mut shown = tray.shown.lock().unwrap();
        shown.status.clear();
        shown.update.clone()
    };
    if let Some(version) = update {
        let _ = tray
            .update
            .set_text(i18n::tf("tray.updateTo", &[("version", &version)]));
    }
    if let Some(hub) = app.try_state::<Arc<Hub>>() {
        refresh(app, &hub.snapshot());
    }
}

/// Follows the core's state; called on every change.
pub fn refresh(app: &AppHandle, core: &CoreState) {
    let Some(tray) = app.try_state::<Tray>() else {
        return;
    };
    let status = status_line(core);
    let waiting = waiting_for_person(core);
    let (status_changed, newly_waiting) = {
        let mut shown = tray.shown.lock().unwrap();
        let status_changed = shown.status != status;
        let newly_waiting = waiting && !shown.waiting_for_person;
        shown.status.clone_from(&status);
        shown.waiting_for_person = waiting;
        (status_changed, newly_waiting)
    };
    if status_changed {
        let _ = tray.status.set_text(&status);
        set_tooltip(&tray, &tray.shown.lock().unwrap());
    }
    if newly_waiting {
        show_window(app);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::engine::FingerprintPrompt;
    use crate::settings::CoreMode;

    fn running(connection: &str, clients: &[&str]) -> CoreState {
        CoreState {
            ipc: IpcStatus::Connected,
            process: ProcessStatus::Running,
            connection: Some(connection.into()),
            clients: clients.iter().map(|c| c.to_string()).collect(),
            ..CoreState::default()
        }
    }

    #[test]
    fn says_what_deskflow_is_doing() {
        assert_eq!(status_line(&CoreState::default()), "Apagado");
        assert_eq!(
            status_line(&running("Connected", &["laptop"])),
            "Compartiendo con laptop"
        );
        assert_eq!(
            status_line(&running("Connected", &["a", "b"])),
            "Compartiendo con 2 computadoras"
        );
        assert_eq!(status_line(&running("Connected", &[])), "Conectado");
        assert_eq!(
            status_line(&running("Listening", &[])),
            "Esperando computadoras"
        );
        assert_eq!(
            status_line(&running("Connecting", &[])),
            "Buscando conexión…"
        );
        assert_eq!(status_line(&running("Disconnected", &[])), "Desconectado");

        let mut lost = running("Connected", &["laptop"]);
        lost.ipc = IpcStatus::Disconnected;
        assert_eq!(status_line(&lost), "Apagado");
    }

    #[test]
    fn trust_decisions_need_a_person() {
        let mut core = running("Connected", &[]);
        assert!(!waiting_for_person(&core));
        core.unrecognised_client = Some("laptop".into());
        assert!(waiting_for_person(&core));

        core.unrecognised_client = None;
        core.fingerprint_prompt = Some(FingerprintPrompt {
            fingerprint: "ab".into(),
            mode: CoreMode::Server,
        });
        assert!(waiting_for_person(&core));
    }
}
