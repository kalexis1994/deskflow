import type { CoreMode } from "../types";
import { t } from "../i18n";
import { isAsking, isWorking, progressLine, whatHappens, type UpdateState } from "./describe";
import "./UpdateCard.css";

interface UpdateCardProps {
  state: UpdateState;
  mode: CoreMode;
  service: boolean;
  onInstall: () => void;
  onDismiss: () => void;
}

/** Asks before installing: nothing happens until someone presses the button. */
export function UpdateCard({ state, mode, service, onInstall, onDismiss }: UpdateCardProps) {
  if (isWorking(state)) {
    return (
      <section className="update" role="status">
        <h3>{progressLine(state)}</h3>
        {state.kind === "downloading" && <progress max={100} value={state.percent ?? undefined} />}
      </section>
    );
  }
  if (state.kind !== "available" || !isAsking(state)) return null;

  return (
    <section className="update" aria-label={t("update.card.label")}>
      <h3>
        {t("update.card.title", { version: state.version })}
        <span className="muted small"> {t("update.card.current", { current: state.current })}</span>
      </h3>
      {state.notes && <p className="update__notes">{state.notes}</p>}
      <p>{whatHappens(mode, service)}</p>
      {state.error && (
        <div className="alert alert--danger" role="alert">
          <span>{state.error}</span>
        </div>
      )}
      <div className="update__actions">
        <button className="btn btn--primary" onClick={onInstall}>
          {t("update.install")}
        </button>
        <button className="btn btn--secondary" onClick={onDismiss}>
          {t("update.notNow")}
        </button>
      </div>
    </section>
  );
}
