# Game mode

A first-person game doesn't care where the cursor is. It hides it, confines it to a
small part of the screen and reads the mouse's own motion, recentering the cursor
every frame. Deskflow normally moves a client's cursor to absolute positions, so the
camera barely turns, or jumps. What such a game needs is relative motion.

## Automatic

The server offers it to each client with the `MCAP` option (on unless
`server/autoGameMode` is false, **Modo juego automático** in the app). A client that
understands it checks four times a second, while the cursor is on it, whether a
program hides the cursor and confines it (`isMouseCaptured` in
`MSWindowsMouseCapture.h`; either alone is common outside games). On a change it tells
the server with `CMCP`. Classic peers never see either: a classic client ignores the
option, and a classic server never offers it.

While the client says so, the server:

- keeps the cursor on that client, whatever the lock settings,
- sends relative motion (`DMRM`) instead of positions,
- tells the app, which shows it under the status.

It stops when the game lets go, when the cursor leaves that client, or when the person
presses Scroll Lock or the lock hotkey, which lets go until the game takes the mouse
again. Then the client's cursor is put back at the center of its screen.

By hand, the old way still works: **Movimientos relativos** on, and the cursor locked
to the screen.

## The mouse's own motion

The motion the server's hook sees has Windows' pointer acceleration applied, which
makes aiming feel wrong. So the primary also registers the mouse for raw input and,
while the cursor is on a client, sends what the mouse measured
(`PrimaryComputerRawMotionOnSecondary`). In relative motion the server sends that
instead of the hook's.

Whether Windows delivers raw input for events the hook swallows isn't something the
server relies on: each hook event normally comes with a raw one, and after a few
without, the server goes back to the accelerated motion.

## Limits

- Games whose anti-cheat ignores injected input (Valorant, for one) don't see it.
- Only Windows clients detect games, and only a Windows server sends raw motion.
