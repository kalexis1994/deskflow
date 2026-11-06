import { useCallback, useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import type { UpdateChannel, UpdateState } from "./describe";

export function useUpdate() {
  const [state, setState] = useState<UpdateState | null>(null);

  useEffect(() => {
    let disposed = false;
    const unlisten = listen<UpdateState>("update-state", (e) => setState(e.payload));
    invoke<UpdateState>("update_state").then((s) => {
      if (!disposed) setState(s);
    });
    return () => {
      disposed = true;
      unlisten.then((off) => off());
    };
  }, []);

  const check = useCallback(() => invoke("check_for_updates"), []);
  const install = useCallback(() => invoke("install_update"), []);
  const dismiss = useCallback(() => invoke("dismiss_update"), []);
  const setChannel = useCallback((channel: UpdateChannel) => invoke("set_update_channel", { channel }), []);

  return { state, check, install, dismiss, setChannel };
}
