// Copies the Deskflow core, daemon and the DLLs they need into src-tauri/core-bin,
// which the Windows bundle installs next to the app (tauri.windows.conf.json).
//
// DESKFLOW_BUILD_DIR: the CMake build's bin folder (default: <repo>/build/bin).
// VCToolsRedistDir:   set by vcvars; otherwise the newest Build Tools redist is used.

import { copyFileSync, existsSync, mkdirSync, readdirSync, rmSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

if (process.platform !== "win32") {
  console.log("stage-core: only the Windows bundle carries the core");
  process.exit(0);
}

const app = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const buildBin = process.env.DESKFLOW_BUILD_DIR ?? resolve(app, "../../../build/bin");
const target = join(app, "src-tauri", "core-bin");

function redistDir() {
  const fromEnv = process.env.VCToolsRedistDir;
  const roots = fromEnv
    ? [fromEnv]
    : ["BuildTools", "Community", "Professional", "Enterprise"].flatMap((edition) => {
        const base = `C:/Program Files${edition === "BuildTools" ? " (x86)" : ""}/Microsoft Visual Studio/2022/${edition}/VC/Redist/MSVC`;
        if (!existsSync(base)) return [];
        return readdirSync(base)
          .filter((v) => /^\d+\.\d+\.\d+$/.test(v))
          .sort((a, b) => b.localeCompare(a, undefined, { numeric: true }))
          .map((v) => join(base, v));
      });
  for (const root of roots) {
    const crt = join(root, "x64", "Microsoft.VC143.CRT");
    if (existsSync(join(crt, "msvcp140.dll"))) return crt;
  }
  throw new Error("stage-core: Visual C++ redistributable DLLs not found (set VCToolsRedistDir)");
}

const files = [
  [buildBin, "deskflow-core.exe"],
  [buildBin, "deskflow-daemon.exe"],
  [buildBin, "libssl-3-x64.dll"],
  [buildBin, "libcrypto-3-x64.dll"],
  // the core is built against the dynamic CRT; a clean PC may not have it
  ...["msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"].map((f) => [redistDir(), f]),
];

rmSync(target, { recursive: true, force: true });
mkdirSync(target, { recursive: true });
for (const [dir, name] of files) {
  const from = join(dir, name);
  if (!existsSync(from)) {
    throw new Error(`stage-core: missing ${from}. Build the core first (cmake --build build).`);
  }
  copyFileSync(from, join(target, name));
  console.log(`stage-core: ${name}`);
}
