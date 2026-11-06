import type { CoreMode } from "../types";
import { canChangeChannel, CHANNELS, isAsking, isWorking, progressLine, searchLine, whatHappens, type UpdateChannel, type UpdateState } from "./describe";
import "./UpdateCard.css";

interface UpdateCardProps {
  state: UpdateState;
  mode: CoreMode;
  onInstall: () => void;
  onDismiss: () => void;
}

/** Asks before installing: nothing happens until someone presses the button. */
export function UpdateCard({ state, mode, onInstall, onDismiss }: UpdateCardProps) {
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
    <section className="update" aria-label="Actualización disponible">
      <h3>
        Hay una versión nueva de Deskflow: {state.version}
        <span className="muted small"> (tenés la {state.current})</span>
      </h3>
      {state.notes && <p className="update__notes">{state.notes}</p>}
      <p>{whatHappens(mode)}</p>
      {state.error && (
        <div className="alert alert--danger" role="alert">
          <span>{state.error}</span>
        </div>
      )}
      <div className="update__actions">
        <button className="btn btn--primary" onClick={onInstall}>
          Actualizar
        </button>
        <button className="btn btn--secondary" onClick={onDismiss}>
          Ahora no
        </button>
      </div>
    </section>
  );
}

interface UpdateFooterProps {
  state: UpdateState;
  onCheck: () => void;
  onChannel: (channel: UpdateChannel) => void;
}

/** The version, and looking for a newer one without waiting for the next search. */
export function UpdateFooter({ state, onCheck, onChannel }: UpdateFooterProps) {
  const failed = state.kind === "failed";
  return (
    <span className="update-footer" title={failed ? state.message : undefined}>
      Versión {state.current}
      {" · "}
      <select
        className="update-footer__channel"
        aria-label="Canal de actualizaciones"
        title={CHANNELS.find((c) => c.value === state.channel)?.title}
        value={state.channel}
        disabled={!canChangeChannel(state)}
        onChange={(e) => onChannel(e.target.value as UpdateChannel)}
      >
        {CHANNELS.map((c) => (
          <option key={c.value} value={c.value}>
            {c.label}
          </option>
        ))}
      </select>
      {" · "}
      {failed && "No se pudo buscar · "}
      {searchLine(state) ?? (
        <button className="update-footer__check" onClick={onCheck}>
          Buscar actualizaciones
        </button>
      )}
    </span>
  );
}
