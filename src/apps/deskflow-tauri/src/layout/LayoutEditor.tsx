import {
  useCallback,
  useEffect,
  useLayoutEffect,
  useMemo,
  useRef,
  useState,
  type PointerEvent as ReactPointerEvent,
} from "react";
import { invoke } from "@tauri-apps/api/core";
import * as g from "./geometry";
import { useLayoutHistory, type EditScreen } from "./useLayoutHistory";
import type { CoreState } from "../types";
import { t, tn, type Key } from "../i18n";
import { tNode } from "../i18n/react";
import { AdvancedPanel, KeyboardPanel } from "./OptionPanels";
import { advancedCustomized, defaultOptions, keyboardCustomized, optionsSummary, type ComputerOptions } from "./options";
import "./LayoutEditor.css";

interface LayoutView {
  screens: (g.Screen & { options?: ComputerOptions })[];
  serverName: string;
  foreignConfig: string | null;
  imported: boolean;
}

interface Props {
  core: CoreState;
  address: string | null;
  port: number;
  /** Called after a save, since renaming this computer changes the app info. */
  onSaved: () => void;
}

interface View {
  scale: number;
  ox: number;
  oy: number;
}

type Drag =
  | { kind: "move"; id: string; pointer: g.Rect; start: g.Rect; rect: g.Rect; guides: g.Guide[]; moved: boolean }
  | { kind: "resize"; id: string; handle: g.Handle; pointer: g.Rect; start: g.Rect; rect: g.Rect; guides: g.Guide[] }
  | { kind: "pan"; clientX: number; clientY: number; start: View };

/** `text` is worked out on render, so a message on screen follows the language. */
type Message = { tone: "ok" | "warn" | "danger"; text: () => string };
/** A refused name; also a function, for the same reason. */
type NameError = () => string;

const HANDLES: g.Handle[] = ["nw", "n", "ne", "e", "se", "s", "sw", "w"];
const SNAP_PX = 14;
const SIDE_TEXTS: Record<g.Side, Key> = {
  right: "layout.screen.link.right",
  left: "layout.screen.link.left",
  up: "layout.screen.link.up",
  down: "layout.screen.link.down",
};
const ASPECTS: [string, number][] = [
  ["16:9", 16 / 9],
  ["16:10", 16 / 10],
  ["21:9", 21 / 9],
  ["4:3", 4 / 3],
  ["9:16", 9 / 16],
];

let idSeq = 0;
const newId = () => `s${++idSeq}`;
const rectOf = (s: g.Rect): g.Rect => ({ x: s.x, y: s.y, w: s.w, h: s.h });
const toEdit = (screens: LayoutView["screens"]): EditScreen[] =>
  screens.map((s) => ({ ...s, id: newId(), orig: s.name, options: s.options ?? defaultOptions() }));
const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));

export function LayoutEditor({ core, address, port, onSaved }: Props) {
  const hist = useLayoutHistory();
  const { screens } = hist;
  const [meta, setMeta] = useState<Omit<LayoutView, "screens"> | null>(null);
  const [selected, setSelected] = useState<string | null>(null);
  const [drag, setDrag] = useState<Drag | null>(null);
  const [view, setView] = useState<View>({ scale: 1, ox: 0, oy: 0 });
  const [size, setSize] = useState({ w: 0, h: 0 });
  const [needsFit, setNeedsFit] = useState(true);
  const [saving, setSaving] = useState(false);
  const [message, setMessage] = useState<Message | null>(null);
  const [renameFocus, setRenameFocus] = useState(0);
  const canvasRef = useRef<HTMLDivElement>(null);

  const load = useCallback(async () => {
    try {
      const v = await invoke<LayoutView>("get_layout");
      hist.reset(toEdit(v.screens));
      setMeta({ serverName: v.serverName, foreignConfig: v.foreignConfig, imported: v.imported });
      setNeedsFit(true);
    } catch (e) {
      setMessage({ tone: "danger", text: () => String(e) });
    }
  }, [hist.reset]);

  useEffect(() => {
    load();
  }, [load]);

  useLayoutEffect(() => {
    const el = canvasRef.current;
    if (!el) return;
    const ro = new ResizeObserver(([entry]) => {
      setSize({ w: entry.contentRect.width, h: entry.contentRect.height });
      setNeedsFit(true);
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const fit = useCallback(
    (list: g.Rect[]) => {
      if (!size.w || !list.length) return;
      const b = g.bounds(list);
      const pad = 56;
      const scale = clamp(Math.min((size.w - 2 * pad) / b.w, (size.h - 2 * pad) / b.h), 0.15, 2.5);
      setView({ scale, ox: size.w / 2 - (b.x + b.w / 2) * scale, oy: size.h / 2 - (b.y + b.h / 2) * scale });
    },
    [size],
  );

  useEffect(() => {
    if (needsFit && size.w && screens.length && !drag) {
      fit(screens);
      setNeedsFit(false);
    }
  }, [needsFit, size, screens, fit, drag]);

  // wheel zoom around the cursor; registered natively because React's wheel listener is passive
  useEffect(() => {
    const el = canvasRef.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      const r = el.getBoundingClientRect();
      const cx = e.clientX - r.left;
      const cy = e.clientY - r.top;
      setView((v) => {
        const scale = clamp(v.scale * Math.exp(-e.deltaY * 0.0015), 0.15, 4);
        const k = scale / v.scale;
        return { scale, ox: cx - (cx - v.ox) * k, oy: cy - (cy - v.oy) * k };
      });
    };
    el.addEventListener("wheel", onWheel, { passive: false });
    return () => el.removeEventListener("wheel", onWheel);
  }, []);

  const serverName = meta?.serverName ?? "";
  const shown = useMemo(
    () => (drag && drag.kind !== "pan" ? screens.map((s) => (s.id === drag.id ? { ...s, ...drag.rect } : s)) : screens),
    [screens, drag],
  );
  const contacts = useMemo(() => g.contacts(shown), [shown]);
  const lost = useMemo(() => new Set(serverName ? g.unreachable(shown, serverName) : []), [shown, serverName]);
  const dragInvalid = useMemo(() => {
    if (!drag || drag.kind === "pan") return false;
    return g.overlapsAny(drag.rect, screens.filter((s) => s.id !== drag.id));
  }, [drag, screens]);
  const sel = screens.find((s) => s.id === selected) ?? null;

  const flash = (m: Message) => setMessage(m);

  // ---- mutations ------------------------------------------------------------------

  const replace = (id: string, patch: Partial<EditScreen>) => hist.commit(screens.map((s) => (s.id === id ? { ...s, ...patch } : s)));

  /** Applies a new rectangle, nudging it out of neighbours if needed. */
  const applyRect = (id: string, rect: g.Rect): boolean => {
    const others = screens.filter((s) => s.id !== id);
    const fitted = g.resolveOverlap(rect, others);
    if (!fitted) {
      flash({ tone: "warn", text: () => t("layout.editor.noRoom") });
      return false;
    }
    replace(id, fitted);
    return true;
  };

  const addScreen = (name?: string) => {
    const anchor = sel ?? screens.find((s) => s.name === serverName) ?? null;
    const size = anchor ? { w: anchor.w, h: anchor.h } : { w: g.DEFAULT_W, h: g.DEFAULT_H };
    const rect = g.placeNear(anchor, size, screens);
    const screen: EditScreen = { ...rect, name: name ?? g.nextFreeName(screens, t("layout.editor.newName")), id: newId(), orig: null, options: defaultOptions() };
    hist.commit([...screens, screen]);
    setSelected(screen.id);
    if (!name) setRenameFocus((n) => n + 1);
    setNeedsFit(true);
  };

  const removeScreen = (id: string) => {
    const s = screens.find((x) => x.id === id);
    if (!s || s.name === serverName) return;
    hist.commit(screens.filter((x) => x.id !== id));
    setSelected(null);
  };

  const rename = (id: string, name: string): NameError | null => {
    if (!g.isValidName(name)) return () => t("layout.name.invalid");
    const lower = name.toLowerCase();
    if (screens.some((s) => s.id !== id && s.name.toLowerCase() === lower)) return () => t("layout.name.taken");
    const owner = screens.find((s) => s.options.aliases.some((a) => a.toLowerCase() === lower));
    if (owner) return owner.id === id ? () => t("layout.name.ownAlias") : () => t("layout.name.aliasOf", { name: owner.name });
    const s = screens.find((x) => x.id === id);
    if (s && s.name === serverName) setMeta((m) => (m ? { ...m, serverName: name } : m));
    replace(id, { name });
    return null;
  };

  const save = async () => {
    setSaving(true);
    setMessage(null);
    try {
      const renames = screens.filter((s) => s.orig && s.orig !== s.name).map((s) => ({ from: s.orig!, to: s.name }));
      const selName = sel?.name;
      const v = await invoke<LayoutView>("save_layout", {
        screens: screens.map(({ name, x, y, w, h, options }) => ({ name, x, y, w, h, options })),
        renames,
      });
      const fresh = toEdit(v.screens);
      hist.reset(fresh);
      setMeta({ serverName: v.serverName, foreignConfig: v.foreignConfig, imported: v.imported });
      setSelected(fresh.find((s) => s.name === selName)?.id ?? null);
      const running = core.process === "running";
      flash({ tone: "ok", text: () => t(running ? "layout.editor.savedRunning" : "layout.editor.savedNext") });
      onSaved();
    } catch (e) {
      flash({ tone: "danger", text: () => String(e) });
    } finally {
      setSaving(false);
    }
  };

  // ---- pointer interaction -----------------------------------------------------------

  const toWorld = (e: { clientX: number; clientY: number }) => {
    const r = canvasRef.current!.getBoundingClientRect();
    return { x: (e.clientX - r.left - view.ox) / view.scale, y: (e.clientY - r.top - view.oy) / view.scale, w: 0, h: 0 };
  };

  const capture = (e: ReactPointerEvent) => {
    canvasRef.current?.setPointerCapture(e.pointerId);
    canvasRef.current?.focus({ preventScroll: true });
  };

  const onTileDown = (e: ReactPointerEvent, s: EditScreen) => {
    if (e.button !== 0) return;
    e.stopPropagation();
    capture(e);
    setSelected(s.id);
    setMessage(null);
    setDrag({ kind: "move", id: s.id, pointer: toWorld(e), start: rectOf(s), rect: rectOf(s), guides: [], moved: false });
  };

  const onHandleDown = (e: ReactPointerEvent, s: EditScreen, handle: g.Handle) => {
    if (e.button !== 0) return;
    e.stopPropagation();
    capture(e);
    setDrag({ kind: "resize", id: s.id, handle, pointer: toWorld(e), start: rectOf(s), rect: rectOf(s), guides: [] });
  };

  const onCanvasDown = (e: ReactPointerEvent) => {
    if (e.button !== 0 && e.button !== 1) return;
    capture(e);
    if (e.button === 0) setSelected(null);
    setDrag({ kind: "pan", clientX: e.clientX, clientY: e.clientY, start: view });
  };

  const onPointerMove = (e: ReactPointerEvent) => {
    if (!drag) return;
    if (drag.kind === "pan") {
      setView({ ...drag.start, ox: drag.start.ox + e.clientX - drag.clientX, oy: drag.start.oy + e.clientY - drag.clientY });
      return;
    }
    const p = toWorld(e);
    const dx = p.x - drag.pointer.x;
    const dy = p.y - drag.pointer.y;
    const others = screens.filter((s) => s.id !== drag.id);
    // Alt turns off the magnets
    const threshold = e.altKey ? 0 : SNAP_PX / view.scale;
    if (drag.kind === "move") {
      const proposed = { ...drag.start, x: drag.start.x + dx, y: drag.start.y + dy };
      const snapped = g.snapMove(proposed, others, threshold);
      const moved = drag.moved || Math.hypot(dx, dy) * view.scale > 3;
      setDrag({ ...drag, rect: { ...drag.start, x: snapped.x, y: snapped.y }, guides: snapped.guides, moved });
    } else {
      const r = g.resize(drag.start, drag.handle, dx, dy, others, e.shiftKey, threshold);
      setDrag({ ...drag, rect: r.rect, guides: r.guides });
    }
  };

  const onPointerUp = (e: ReactPointerEvent) => {
    canvasRef.current?.releasePointerCapture(e.pointerId);
    const d = drag;
    setDrag(null);
    if (!d || d.kind === "pan") return;
    const others = screens.filter((s) => s.id !== d.id);
    if (d.kind === "move") {
      if (!d.moved) return;
      const fitted = g.resolveOverlap(d.rect, others);
      if (fitted) replace(d.id, fitted);
      else flash({ tone: "warn", text: () => t("layout.editor.overlapsHere") });
    } else if (g.overlapsAny(d.rect, others)) {
      flash({ tone: "warn", text: () => t("layout.editor.cantOverlap") });
    } else {
      replace(d.id, d.rect);
    }
  };

  const onKeyDown = (e: KeyboardEvent) => {
    const target = e.target as HTMLElement | null;
    // never steal keys while the person is typing in a field
    if (target?.closest("input, textarea, select, [contenteditable='true'], [data-keycapture]")) return;
    const mod = e.ctrlKey || e.metaKey;
    if (mod && e.key.toLowerCase() === "z") {
      e.preventDefault();
      if (e.shiftKey) hist.redo();
      else hist.undo();
      return;
    }
    if (mod && e.key.toLowerCase() === "y") {
      e.preventDefault();
      hist.redo();
      return;
    }
    if (e.key === "Escape") {
      setSelected(null);
      return;
    }
    // moving and deleting only act when the canvas (or nothing in particular) has focus
    const onCanvas = target === canvasRef.current || target === document.body;
    if (!sel || !onCanvas) return;
    if (e.key === "Delete" || e.key === "Backspace") {
      e.preventDefault();
      removeScreen(sel.id);
      return;
    }
    const step = e.shiftKey ? 10 : 1;
    const delta: Record<string, [number, number]> = { ArrowLeft: [-step, 0], ArrowRight: [step, 0], ArrowUp: [0, -step], ArrowDown: [0, step] };
    const d = delta[e.key];
    if (d) {
      e.preventDefault();
      const next = { ...rectOf(sel), x: sel.x + d[0], y: sel.y + d[1] };
      if (!g.overlapsAny(next, screens.filter((s) => s.id !== sel.id))) replace(sel.id, next);
    }
  };

  const keyHandler = useRef(onKeyDown);
  keyHandler.current = onKeyDown;
  useEffect(() => {
    const listener = (e: KeyboardEvent) => keyHandler.current(e);
    window.addEventListener("keydown", listener);
    return () => window.removeEventListener("keydown", listener);
  }, []);

  // ---- render ---------------------------------------------------------------------------

  const px = (r: g.Rect) => ({ left: view.ox + r.x * view.scale, top: view.oy + r.y * view.scale, width: r.w * view.scale, height: r.h * view.scale });
  const running = core.process === "running";
  const guides = drag && drag.kind !== "pan" ? drag.guides : [];
  const pending = core.unrecognisedClient;
  const pendingInLayout = pending ? screens.some((s) => s.name.toLowerCase() === pending.toLowerCase()) : false;

  return (
    <section className="card le">
      <div className="le__head">
        <div>
          <h3>{t("layout.editor.title")}</h3>
          <p className="muted small">{t("layout.editor.intro")}</p>
        </div>
      </div>

      {meta?.imported && (
        <div className="alert alert--info">
          <span>{t("layout.editor.imported")}</span>
        </div>
      )}
      {meta?.foreignConfig && (
        <div className="alert alert--warn">
          <span>{tNode("layout.editor.foreignConfig", { file: <code>{meta.foreignConfig}</code> })}</span>
        </div>
      )}
      {pending && !(pendingInLayout && !hist.dirty) && (
        <div className="alert alert--warn le__pending">
          <span>
            {tNode(pendingInLayout ? "layout.editor.pendingInLayout" : "layout.editor.pendingMissing", { name: <strong>{pending}</strong> })}
          </span>
          {!pendingInLayout && (
            <button className="btn btn--primary btn--small" onClick={() => addScreen(pending)}>
              {t("layout.add")}
            </button>
          )}
        </div>
      )}

      <div className="le__toolbar">
        <button className="btn btn--secondary btn--small" onClick={() => addScreen()} title={t("layout.editor.addComputerTitle")}>
          <Icon d="M8 3v10M3 8h10" /> {t("layout.editor.addComputer")}
        </button>
        <div className="le__group">
          <IconButton title={t("layout.editor.undo")} disabled={!hist.canUndo} onClick={hist.undo} d="M5 4 2 7l3 3M2 7h8a4 4 0 0 1 0 8H7" />
          <IconButton title={t("layout.editor.redo")} disabled={!hist.canRedo} onClick={hist.redo} d="m11 4 3 3-3 3M14 7H6a4 4 0 0 0 0 8h3" />
          <IconButton title={t("layout.editor.fit")} onClick={() => fit(screens)} d="M2 6V2h4M10 2h4v4M14 10v4h-4M6 14H2v-4" />
        </div>
        <div className="le__spacer" />
        {message && <span className={`le__msg le__msg--${message.tone}`}>{message.text()}</span>}
        {hist.dirty && !message && <span className="le__msg">{t("layout.editor.unsaved")}</span>}
        <button className="btn btn--secondary btn--small" disabled={!hist.dirty || saving} onClick={hist.revert}>
          {t("common.discard")}
        </button>
        <button className="btn btn--primary btn--small" disabled={!hist.dirty || saving} onClick={save}>
          {saving ? t("layout.editor.saving") : running ? t("layout.editor.saveApply") : t("common.save")}
        </button>
      </div>

      <div className="le__body">
        <div
          ref={canvasRef}
          className={`le__canvas ${drag?.kind === "pan" ? "le__canvas--panning" : ""}`}
          tabIndex={0}
          onPointerDown={onCanvasDown}
          onPointerMove={onPointerMove}
          onPointerUp={onPointerUp}
          onPointerCancel={onPointerUp}
          aria-label={t("layout.editor.canvas")}
        >
          {shown.map((s) => {
            const isServer = s.name === serverName;
            const isSel = s.id === selected;
            const active = drag && drag.kind !== "pan" && drag.id === s.id;
            const connected = isServer ? running : core.clients.includes(s.name);
            const cls = [
              "le__screen",
              isServer && "le__screen--server",
              isSel && "le__screen--selected",
              active && "le__screen--active",
              active && dragInvalid && "le__screen--invalid",
              lost.has(s.name) && "le__screen--lost",
            ]
              .filter(Boolean)
              .join(" ");
            return (
              <div key={s.id} className={cls} style={px(s)} onPointerDown={(e) => onTileDown(e, s)}>
                <div className="le__screen-inner">
                  <span className="le__name">{s.name}</span>
                  <span className="le__sub">
                    {running && <span className={`dot ${connected ? "dot--ok" : ""}`} />}
                    {isServer ? t("layout.editor.thisComputer") : running ? t(connected ? "layout.editor.connected" : "layout.editor.notConnected") : ""}
                  </span>
                  {(isSel || active) && <span className="le__aspect">{g.aspectLabel(s.w, s.h)}</span>}
                  {optionsSummary(s.options).length > 0 && (
                    <span className="le__badge" title={optionsSummary(s.options).join(" · ")}>
                      <svg viewBox="0 0 16 16" aria-hidden="true">
                        <path d="M2 4h7M12 4h2M2 12h2M7 12h7M9 2v4M4 10v4" />
                      </svg>
                    </span>
                  )}
                </div>
                {isSel &&
                  HANDLES.map((h) => (
                    <span key={h} className={`le__handle le__handle--${h}`} onPointerDown={(e) => onHandleDown(e, s, h)} />
                  ))}
              </div>
            );
          })}

          <svg className="le__overlay" width={size.w} height={size.h} aria-hidden="true">
            {contacts.map((c, i) => (
              <line
                key={i}
                className="le__contact"
                x1={view.ox + c.x1 * view.scale}
                y1={view.oy + c.y1 * view.scale}
                x2={view.ox + c.x2 * view.scale}
                y2={view.oy + c.y2 * view.scale}
              />
            ))}

            {guides.map((gd, i) =>
              gd.orientation === "v" ? (
                <line key={i} className="le__guide" x1={view.ox + gd.at * view.scale} y1={0} x2={view.ox + gd.at * view.scale} y2={size.h} />
              ) : (
                <line key={i} className="le__guide" x1={0} y1={view.oy + gd.at * view.scale} x2={size.w} y2={view.oy + gd.at * view.scale} />
              ),
            )}
          </svg>

        </div>

        <aside className="le__inspector">
          {sel ? (
            <Inspector
              key={sel.id}
              screen={sel}
              isServer={sel.name === serverName}
              screens={screens}
              lost={lost.has(sel.name)}
              renameFocus={renameFocus}
              onRename={(name) => rename(sel.id, name)}
              onRect={(r) => applyRect(sel.id, r)}
              onOptions={(options) => replace(sel.id, { options })}
              onRemove={() => removeScreen(sel.id)}
            />
          ) : (
            <Summary count={screens.length} lost={[...lost]} address={address} port={port} />
          )}
        </aside>
      </div>
      <p className="le__hint">{t("layout.editor.hint")}</p>
    </section>
  );
}

function Summary({ count, lost, address, port }: { count: number; lost: string[]; address: string | null; port: number }) {
  return (
    <div className="le__panel">
      <h4>{tn("layout.summary.computers", count)}</h4>
      {lost.length > 0 && <p className="le__warn">{tn("layout.summary.unreachable", lost.length, { names: lost.join(", ") })}</p>}
      <ul className="le__tips">
        <li>{t("layout.summary.tipClick")}</li>
        <li>{t("layout.summary.tipLines")}</li>
        <li>{t("layout.summary.tipSize")}</li>
      </ul>
      {address && <p className="muted small">{tNode("layout.summary.address", { address: <code>{address}</code>, port })}</p>}
    </div>
  );
}

interface InspectorProps {
  screen: EditScreen;
  isServer: boolean;
  screens: EditScreen[];
  lost: boolean;
  renameFocus: number;
  onRename: (name: string) => NameError | null;
  onRect: (r: g.Rect) => boolean;
  onOptions: (o: ComputerOptions) => void;
  onRemove: () => void;
}

type Tab = "screen" | "keyboard" | "advanced";

function Inspector(props: InspectorProps) {
  const { screen, screens, onOptions } = props;
  const [tab, setTab] = useState<Tab>("screen");
  const tabs: { id: Tab; label: string; dot: boolean }[] = [
    { id: "screen", label: t("layout.inspector.tab.screen"), dot: false },
    { id: "keyboard", label: t("layout.inspector.tab.keyboard"), dot: keyboardCustomized(screen.options) },
    { id: "advanced", label: t("layout.inspector.tab.advanced"), dot: advancedCustomized(screen.options) },
  ];
  return (
    <div className="le__panel">
      <div className="le__tabs" role="tablist">
        {tabs.map((it) => (
          <button key={it.id} role="tab" aria-selected={tab === it.id} className={`le__tab ${tab === it.id ? "le__tab--on" : ""}`} onClick={() => setTab(it.id)}>
            {it.label}
            {it.dot && <span className="le__tab-dot" aria-label={t("layout.inspector.customized")} />}
          </button>
        ))}
      </div>
      {tab === "screen" && <ScreenPanel {...props} />}
      {tab === "keyboard" && <KeyboardPanel options={screen.options} onChange={onOptions} />}
      {tab === "advanced" && (
        <AdvancedPanel
          options={screen.options}
          onChange={onOptions}
          name={screen.name}
          taken={screens.map((s) => ({ name: s.name, aliases: s.options.aliases }))}
        />
      )}
    </div>
  );
}

function ScreenPanel({ screen, isServer, screens, lost, renameFocus, onRename, onRect, onRemove }: InspectorProps) {
  const [name, setName] = useState(screen.name);
  const [error, setError] = useState<NameError | null>(null);
  const nameRef = useRef<HTMLInputElement>(null);
  useEffect(() => setName(screen.name), [screen.name]);
  useEffect(() => {
    if (renameFocus) {
      nameRef.current?.focus();
      nameRef.current?.select();
    }
  }, [renameFocus]);

  const commitName = () => {
    const n = name.trim();
    if (n === screen.name) return setError(null);
    const err = onRename(n);
    setError(err);
    if (err) setName(screen.name);
  };

  const links = g.neighbours(screens, screen.name);
  const ratio = screen.w / screen.h;

  return (
    <>
      <label className="field">
        <span className="field__label">{t(isServer ? "layout.screen.nameServer" : "layout.screen.nameClient")}</span>
        <input
          ref={nameRef}
          value={name}
          spellCheck={false}
          onChange={(e) => setName(e.target.value)}
          onBlur={commitName}
          onKeyDown={(e) => {
            if (e.key === "Enter") (e.target as HTMLInputElement).blur();
            if (e.key === "Escape") {
              setName(screen.name);
              setError(null);
            }
          }}
        />
      </label>
      {error && <p className="le__error">{error()}</p>}
      {!isServer && !error && <p className="muted small">{t("layout.screen.nameHint")}</p>}

      <div className="le__row">
        <NumberField label={t("layout.screen.width")} value={screen.w} onCommit={(w) => onRect({ ...screen, w })} />
        <NumberField label={t("layout.screen.height")} value={screen.h} onCommit={(h) => onRect({ ...screen, h })} />
      </div>

      <div className="le__chips" role="group" aria-label={t("layout.screen.aspect")}>
        {ASPECTS.map(([label, r]) => (
          <button
            key={label}
            className={`le__chip ${Math.abs(r - ratio) / r < 0.02 ? "le__chip--on" : ""}`}
            onClick={() => onRect({ ...screen, h: Math.round(screen.w / r) })}
          >
            {label}
          </button>
        ))}
        <button className="le__chip" title={t("layout.screen.rotateTitle")} onClick={() => onRect({ ...screen, w: screen.h, h: screen.w })}>
          ⟲ {t("layout.screen.rotate")}
        </button>
      </div>

      <div className="le__links">
        <span className="field__label">{t("layout.screen.linksTitle")}</span>
        {links.length ? (
          <ul>
            {links.map((l, i) => (
              <li key={i}>{tNode(SIDE_TEXTS[l.side], { name: <strong>{l.name}</strong> })}</li>
            ))}
          </ul>
        ) : (
          <p className="le__warn">{t("layout.screen.noLinks")}</p>
        )}
        {lost && links.length > 0 && <p className="le__warn">{t("layout.screen.unreachable")}</p>}
      </div>

      {!isServer && (
        <button className="btn btn--danger btn--small" onClick={onRemove}>
          {t("layout.screen.remove")}
        </button>
      )}
    </>
  );
}

function NumberField({ label, value, onCommit }: { label: string; value: number; onCommit: (v: number) => void }) {
  const [text, setText] = useState(String(Math.round(value)));
  useEffect(() => setText(String(Math.round(value))), [value]);
  const commit = () => {
    const v = Math.round(Number(text));
    if (Number.isFinite(v) && v >= g.MIN_SIZE && v !== Math.round(value)) onCommit(v);
    else setText(String(Math.round(value)));
  };
  return (
    <label className="field">
      <span className="field__label">{label}</span>
      <input
        type="number"
        min={g.MIN_SIZE}
        value={text}
        onChange={(e) => setText(e.target.value)}
        onBlur={commit}
        onKeyDown={(e) => e.key === "Enter" && (e.target as HTMLInputElement).blur()}
      />
    </label>
  );
}

function Icon({ d }: { d: string }) {
  return (
    <svg viewBox="0 0 16 16" className="le__icon" aria-hidden="true">
      <path d={d} />
    </svg>
  );
}

function IconButton({ d, title, disabled, onClick }: { d: string; title: string; disabled?: boolean; onClick: () => void }) {
  return (
    <button className="le__iconbtn" title={title} aria-label={title} disabled={disabled} onClick={onClick}>
      <Icon d={d} />
    </button>
  );
}
