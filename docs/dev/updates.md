# Updates

The Tauri app looks for new versions in the GitHub releases of
[kalexis1994/deskflow](https://github.com/kalexis1994/deskflow).

## How it behaves

- **It asks.** The app searches 30 seconds after starting and every 6 hours. When it
  finds a version it brings up the window once with **Actualizar** and **Ahora no**.
  Nothing is installed without pressing the button. "Ahora no" lasts until the app
  restarts or a newer version comes out, and the tray menu keeps the way back.
- **Two channels.** Each computer chooses in the footer of the window. **Estable** gets
  the releases that were tried. **Alpha** gets test builds and looks every 10 minutes,
  so a new one shows up by itself. Both ask the same way. The choice is kept in
  `update-channel` in the app's config folder and not in the settings file, because
  the service restarts the core when that file changes.
- **Any version goes straight to the latest.** Every release holds the complete
  installer and `latest.json` names only the newest one, so nobody has to install the
  versions in between.

Under the service, the service installs the update once the person says yes, so
nothing else asks: Windows' permission prompt would come up on a computer controlled
through Deskflow just as the installer stops the core, and nobody could answer it. Any
program can reach the service's pipe, so it trusts nothing in the request. It copies
the installer into a new folder that only SYSTEM and administrators can write, checks
that copy's signature with the key built into it (`MSWindowsUpdate.h`, which a test
keeps equal to `tauri.conf.json`), takes only a version newer than the installed one,
and runs it in the person's session as it runs the core, with a progress bar and the
app opening again at the end. Without the service, or if it refuses, the app runs the
installer itself and Windows asks for permission.

Installing stops the service, so Deskflow is cut off for a few seconds. That is why
it can't be silent: on a computer with no keyboard of its own, that is the only way
to control it. The installer shows no dialogs during an update, because nobody could
close them while the core is down. If an update leaves the service stopped, reach the
computer another way (remote desktop) and run the installer again.

## What every release must keep true

The installer can run over any earlier version, or over the same one, any number of
times. So:

- Installer hooks and `windows/deskflow-service.ps1` describe the end state: they
  must work whether or not the service, the rule or the value is already there.
- Never rely on a version having been installed. A change to saved settings is applied
  when the app or core reads them, by looking at what the file holds, not at which
  version it came from.
- Never publish a delta or a release that only works from the previous one.

## Publishing

```bash
pnpm tauri build
node scripts/release.mjs --alpha --notes "what to try"
node scripts/release.mjs --promote
node scripts/release.mjs --notes "what changed"
```

The script runs from `src/apps/deskflow-tauri`, publishes the version in
`src-tauri/tauri.conf.json`, and takes the notes from `--notes "..."`. `--dry-run`
prepares everything in `dist-release/` without uploading it.

`--alpha` publishes that version as a pre-release, which the stable channel can't see:
it reads `releases/latest`, and GitHub leaves pre-releases out of it. The `alpha-channel`
release holds a copy of the newest alpha's `latest.json` at a fixed address.

`--promote` turns the alpha of the current version into the stable release, with the
same installer and signature, so what reaches everyone is exactly what was tried.
`node scripts/release.mjs` on its own publishes directly to stable.

Alphas use the same version numbers as stable releases: every build gets the next one,
without a suffix. An app only moves to a higher number, so going back from Alpha to
Estable leaves it where it is until a higher stable version comes out.

## The signing key

Updates are signed, and an installed app refuses anything else. The public key is in
`tauri.conf.json`; the private one is `~/.tauri/deskflow-updater.key`, outside the
repository, and it must never be committed.

**Back it up.** Without it no new version can be signed, and every installed app would
have to be reinstalled by hand with a build that carries a new public key. Anyone who
gets it can sign an update that installs as administrator, so keep the copy somewhere
private. It has no password.

This signature only concerns the updater. Windows SmartScreen can still warn about an
installer downloaded by hand, because that takes a code signing certificate.
