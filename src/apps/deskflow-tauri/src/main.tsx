import React from "react";
import ReactDOM from "react-dom/client";
import App from "./App";

// outside Tauri (plain browser during development) use a simulated backend
if (import.meta.env.DEV && !("__TAURI_INTERNALS__" in window)) {
  await import("./devMock");
}

ReactDOM.createRoot(document.getElementById("root") as HTMLElement).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
