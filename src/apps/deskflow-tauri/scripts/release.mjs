// Publishes the installer of this version as a GitHub release, which is where
// installed apps look for updates. See docs/dev/updates.md.
//
//   node scripts/release.mjs [--dry-run] [--alpha] [--notes "text"]
//   node scripts/release.mjs --promote    this version's alpha becomes the stable one

import { execFileSync } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const REPO = "kalexis1994/deskflow";
const REMOTE = "fork";
// a pre-release whose only asset is the newest alpha's latest.json
const ALPHA_TAG = "alpha-channel";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const args = process.argv.slice(2);
const dryRun = args.includes("--dry-run");
const alpha = args.includes("--alpha");
const promote = args.includes("--promote");
const notesAt = args.indexOf("--notes");
const notes = notesAt >= 0 ? args[notesAt + 1] : "";

function fail(message) {
  console.error(`release: ${message}`);
  process.exit(1);
}

function run(command, commandArgs, options = {}) {
  return execFileSync(command, commandArgs, { cwd: root, encoding: "utf8", shell: false, ...options }).trim();
}

function shell(command) {
  // pnpm is a .cmd on Windows
  return execFileSync(command, { cwd: root, encoding: "utf8", shell: true, stdio: ["ignore", "pipe", "inherit"] });
}

const config = JSON.parse(readFileSync(join(root, "src-tauri/tauri.conf.json"), "utf8"));
const version = config.version;
const tag = `tauri-v${version}`;

if (promote) {
  // the same installer and the same signature: latest.json was in the release already
  console.log(run("gh", ["release", "edit", tag, "--repo", REPO, "--prerelease=false", "--latest"]));
  process.exit(0);
}

const key = process.env.DESKFLOW_UPDATER_KEY ?? join(homedir(), ".tauri", "deskflow-updater.key");

const built = join(root, "src-tauri/target/release/bundle/nsis", `${config.productName}_${version}_x64-setup.exe`);
if (!existsSync(built)) fail(`no installer for ${version}: run "pnpm tauri build" first (${built})`);
if (!existsSync(key)) fail(`no signing key at ${key}: see docs/dev/updates.md`);

const commit = run("git", ["rev-parse", "HEAD"]);
if (!dryRun) {
  const changes = run("git", ["status", "--porcelain", "--", ":/", ":(top,exclude)translations", ":(top,exclude).claude"]);
  if (changes !== "") fail(`commit everything first: a release must match its source\n${changes}`);
  if (!run("git", ["branch", "-r", "--contains", commit]).includes(`${REMOTE}/`)) fail(`push ${commit.slice(0, 9)} to ${REMOTE} first`);
  if (run("gh", ["release", "list", "--repo", REPO, "--json", "tagName", "--jq", ".[].tagName"]).split("\n").includes(tag)) {
    fail(`${tag} already exists: raise the version instead of replacing what apps may have installed`);
  }
}

const out = join(root, "dist-release");
rmSync(out, { recursive: true, force: true });
mkdirSync(out);

// no spaces: GitHub would rename the asset and the address in latest.json would break
const installerName = `Deskflow-Tauri_${version}_x64-setup.exe`;
const installer = join(out, installerName);
copyFileSync(built, installer);

// an empty password has to be passed as such, or the CLI waits for one
shell(`pnpm -s tauri signer sign -f "${key}" -p "" "${installer}"`);
const signature = readFileSync(`${installer}.sig`, "utf8").trim();

const url = `https://github.com/${REPO}/releases/download/${tag}/${installerName}`;
const feed = join(out, "latest.json");
writeFileSync(
  feed,
  `${JSON.stringify(
    {
      version,
      notes,
      pub_date: new Date().toISOString(),
      platforms: { "windows-x86_64": { signature, url } },
    },
    null,
    2,
  )}\n`,
);

if (dryRun) {
  console.log(`ready in ${out}, nothing uploaded`);
  process.exit(0);
}

const create = ["release", "create", tag, installer, `${installer}.sig`, feed];
create.push("--repo", REPO, "--target", commit, "--title", `Deskflow Tauri ${version}`, "--notes", notes || `Deskflow Tauri ${version}`);
create.push(alpha ? "--prerelease" : "--latest");
console.log(run("gh", create));

if (alpha) {
  const releases = run("gh", ["release", "list", "--repo", REPO, "--json", "tagName", "--jq", ".[].tagName"]).split("\n");
  if (!releases.includes(ALPHA_TAG)) {
    const about = "El canal de pruebas: latest.json apunta siempre a la última alpha.";
    run("gh", ["release", "create", ALPHA_TAG, "--repo", REPO, "--target", commit, "--prerelease", "--title", "Canal alpha", "--notes", about]);
  }
  run("gh", ["release", "upload", ALPHA_TAG, feed, "--repo", REPO, "--clobber"]);
  console.log(`the alpha channel now offers ${version}`);
}
