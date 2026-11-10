import { openUrl } from "@tauri-apps/plugin-opener";
import { useState } from "react";
import { LANGUAGES, t, type LanguageChoice } from "../i18n";
import { chooseLanguage, languageChoice, systemLanguage } from "../i18n/choice";
import type { Overview } from "../types";
import { canChangeChannel, CHANNELS, channelLabel, channelTitle, searchLine, type UpdateChannel, type UpdateState } from "../update/describe";
import "./AboutCard.css";

const REPOSITORY = "https://github.com/kalexis1994/deskflow";
const LINKS = [
  { key: "about.source", url: REPOSITORY },
  { key: "about.releases", url: `${REPOSITORY}/releases` },
  { key: "about.upstream", url: "https://github.com/deskflow/deskflow" },
] as const;

function open(url: string) {
  openUrl(url).catch(() => window.open(url, "_blank", "noopener"));
}

interface AboutCardProps {
  ov: Overview;
  coreVersion: string | null;
  update: UpdateState | null;
  onCheck: () => void;
  onChannel: (channel: UpdateChannel) => void;
}

/** What this is, which version, where it gets new ones and in which language it speaks. */
export function AboutCard({ ov, coreVersion, update, onCheck, onChannel }: AboutCardProps) {
  return (
    <>
      <section className="card about">
        <div className="about__head">
          <h3>Deskflow</h3>
          <p className="muted">{t("about.tagline")}</p>
        </div>
        <dl className="about__facts">
          {update && (
            <div>
              <dt>{t("about.version", { version: update.current })}</dt>
              {coreVersion && <dd className="muted small">{t("about.core", { version: coreVersion })}</dd>}
            </div>
          )}
          <div>
            <dt>{ov.processMode === "service" ? t("about.runsAsService") : t("about.runsInApp")}</dt>
          </div>
          <div>
            <dt>{t("about.settingsFile")}</dt>
            <dd className="about__path" title={ov.settingsFile}>
              <code>{ov.settingsFile}</code>
            </dd>
          </div>
        </dl>
      </section>

      {update && <UpdatesSection state={update} onCheck={onCheck} onChannel={onChannel} />}

      <LanguageSection />

      <section className="card about">
        <h3>{t("about.links")}</h3>
        <ul className="about__links">
          {LINKS.map((l) => (
            <li key={l.url}>
              <button className="about__link" onClick={() => open(l.url)}>
                {t(l.key)}
              </button>
            </li>
          ))}
        </ul>
        <p className="muted small">{t("about.license")}</p>
      </section>
    </>
  );
}

function UpdatesSection({ state, onCheck, onChannel }: { state: UpdateState; onCheck: () => void; onChannel: (c: UpdateChannel) => void }) {
  const line = searchLine(state);
  return (
    <section className="card about">
      <h3>{t("about.updates")}</h3>
      <div className="about__row">
        <label className="about__field">
          <span className="muted small">{t("about.channel")}</span>
          <select value={state.channel} disabled={!canChangeChannel(state)} onChange={(e) => onChannel(e.target.value as UpdateChannel)}>
            {CHANNELS.map((c) => (
              <option key={c} value={c}>
                {channelLabel(c)}
              </option>
            ))}
          </select>
        </label>
        <button className="btn btn--secondary" onClick={onCheck} disabled={state.kind === "checking"}>
          {t("about.check")}
        </button>
        {line && <span className="muted">{line}</span>}
      </div>
      <p className="muted small">{channelTitle(state.channel)}</p>
      {state.kind === "failed" && (
        <div className="alert alert--danger" role="alert">
          <span>{t("about.checkFailed", { reason: state.message })}</span>
        </div>
      )}
    </section>
  );
}

function LanguageSection() {
  const [choice, setChoice] = useState<LanguageChoice>(languageChoice);
  const system = LANGUAGES.find((l) => l.code === systemLanguage())?.name ?? "English";
  return (
    <section className="card about">
      <h3>{t("about.language")}</h3>
      <label className="about__field">
        <select
          value={choice}
          aria-label={t("about.language")}
          onChange={(e) => {
            const next = e.target.value as LanguageChoice;
            setChoice(next);
            chooseLanguage(next);
          }}
        >
          <option value="auto">{t("about.languageAuto", { name: system })}</option>
          {LANGUAGES.map((l) => (
            <option key={l.code} value={l.code} lang={l.code}>
              {l.name}
            </option>
          ))}
        </select>
      </label>
      <p className="muted small">{t("about.languageHint")}</p>
    </section>
  );
}
