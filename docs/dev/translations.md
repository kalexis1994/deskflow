# Translations

The Tauri app speaks Spanish, English, French, German and Portuguese. The person
chooses in **Acerca de**; **Automático** follows Windows, or English when Windows uses
none of them. The choice is kept in `language` in the app's config folder.

## Where the texts are

`src/apps/deskflow-tauri/src/i18n/locales/<language>/<part>.json`, one file per part of
the app (`app`, `layout`, `hotkeys`, `clipboard`, `options`, `security`, `update`,
`about`, `common`). Keys are flat, with dots: `"status.connected.title"`.

`native.json` holds what the Rust side shows: the tray menu and the errors it sends to
the window. Rust reads the same files (`src-tauri/src/i18n.rs`), so each text lives in
one place.

## Rules

- **Spanish is the reference.** Its keys are the ones the code may use: TypeScript
  rejects `t("…")` with a key Spanish lacks, and a Rust test checks every
  `i18n::t("…")`/`i18n::tf("…")` in the sources.
- **Every language has the same keys and the same `{placeholders}`.** Tests on both
  sides fail otherwise, so a new text needs all five languages before it builds green.
- **Counts** use `tn()` and keys ending in `_one` and `_other`.
- **Text with elements inside**, such as a name in bold, uses `tNode()` with
  placeholders, so each language puts them where its grammar wants.
- **Translate while rendering.** Nothing calls `t()` at module level or keeps a
  translated text in state: a language change re-renders everything, and only text
  read during that render follows it.
- Key names that change with the keyboard's language (Supr/Delete/Entf, Ctrl/Strg,
  Shift/Maj/Umschalt) are texts too; universal ones (Alt, Esc, F1) stay in code.

## Adding a language

Add its code to `LANGUAGES` in `src/i18n/index.ts` and in `src-tauri/src/i18n.rs`
(with its `native.json` in `NATIVE`), then copy the `es` folder and translate it.
