# Monitors in the screen layout

Deskflow moves the cursor over one rectangle per computer: the box around all of its
monitors. Where the monitors don't fill that box (different heights, an offset), there
is no screen; a cursor sent there is pulled by the system to the nearest monitor, and
it jumps.

## Who knows the monitors

- The server knows its own (`IPlatformComputer::getMonitors`, `EnumDisplayMonitors` on
  Windows).
- A client tells the server when offered the `MONS` option, with `DMON`, and again
  whenever its display changes. Classic peers never see either.
- The server sends every computer's monitors to the app (`monitors`, JSON). The app
  keeps what it last heard of each computer, also after it disconnects.

## In the editor

- A screen with known monitors takes their proportion, draws them inside its
  rectangle, and keeps the proportion whichever way it is resized. Its size on the
  canvas is still the person's choice.
- When the reported monitors differ from the saved ones, the editor takes them as a
  change to save, and says so. Only then, or when the layout loads: undo can take a
  merge back.
- The monitors are saved with the layout (`monitors` of each screen, in pixels).

## Links

Two screens are linked only along the parts of their touching edges where both have a
monitor on that edge (`covered` in `src-tauri/src/layout.rs`, mirrored in
`src/layout/geometry.ts`). Monitors stacked along an edge make one stretch; a gap
leaves two links between the same pair, which the core's ranges handle.

Changing the monitors' own arrangement is left to the system's display settings:
Deskflow follows it.
