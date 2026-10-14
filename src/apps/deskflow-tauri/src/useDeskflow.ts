import { useCallback, useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import type { AppInfo, Autostart, Changes, CoreState, Overview } from "./types";

const LOG_LIMIT = 1000;

/** Connects the UI to the Rust side: initial snapshot plus live events. */
export function useDeskflow() {
  const [info, setInfo] = useState<AppInfo | null>(null);
  const [core, setCore] = useState<CoreState | null>(null);
  const [log, setLog] = useState<string[]>([]);
  const [address, setAddress] = useState<string | null>(null);
  const [autostart, setAutostart] = useState<Autostart | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let disposed = false;
    const unlisten = [
      listen<CoreState>("core-state", (e) => setCore(e.payload)),
      listen<string[]>("core-log", (e) => setLog((prev) => [...prev, ...e.payload].slice(-LOG_LIMIT))),
    ];

    Promise.all([
      invoke<AppInfo>("app_info"),
      invoke<CoreState>("core_state"),
      invoke<string[]>("core_log"),
      invoke<string | null>("local_address"),
    ])
      .then(([i, c, l, a]) => {
        if (disposed) return;
        setInfo(i);
        setCore(c);
        setLog(l.slice(-LOG_LIMIT));
        setAddress(a);
      })
      .catch((e) => setError(String(e)));

    return () => {
      disposed = true;
      unlisten.forEach((p) => p.then((off) => off()));
    };
  }, []);

  const run = useCallback(async (action: () => Promise<unknown>) => {
    setBusy(true);
    setError(null);
    try {
      await action();
    } catch (e) {
      setError(String(e));
    } finally {
      setBusy(false);
    }
  }, []);

  const start = useCallback(() => run(() => invoke("start_core")), [run]);
  const stop = useCallback(() => run(() => invoke("stop_core")), [run]);
  const save = useCallback(
    (changes: Changes) =>
      run(async () => {
        const overview = await invoke<Overview>("save_settings", { changes });
        setInfo((prev) => (prev ? { ...prev, overview } : prev));
      }),
    [run],
  );

  const refresh = useCallback(() => {
    invoke<AppInfo>("app_info")
      .then(setInfo)
      .catch((e) => setError(String(e)));
  }, []);

  // the service remembers what to start on boot only while it runs the core, so re-check on changes
  const process = core?.process;
  useEffect(() => {
    invoke<Autostart>("autostart_status").then(setAutostart, () => setAutostart(null));
  }, [process, info?.overview.processMode]);

  return { info, core, log, address, autostart, busy, error, dismissError: () => setError(null), start, stop, save, refresh };
}

/** Seconds left until `deadline` (ms since epoch), refreshed every second. */
export function useCountdown(deadline: number | null) {
  const [now, setNow] = useState(Date.now());
  useEffect(() => {
    if (deadline === null) return;
    const id = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(id);
  }, [deadline]);
  return deadline === null ? null : Math.max(0, Math.ceil((deadline - now) / 1000));
}
