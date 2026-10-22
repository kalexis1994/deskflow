# Cross-computer clipboard: analysis

Written while planning the Qt removal (`remove-qt.md`); to be fixed afterwards.
Line numbers are from commit fef8ad027.

## How it works today

- Windows: `MSWindowsComputer` listens with `AddClipboardFormatListener`; on
  `WM_CLIPBOARDUPDATE` it compares `GetClipboardSequenceNumber()` and checks a private
  "Deskflow Ownership" format to tell its own writes from other apps'
  (`MSWindowsComputer.cpp:935-948, 1347-1362`).
- A local copy becomes a *grab* (`CCLP id,seq`). Every other computer immediately
  empties its clipboard (`Server.cpp:1164-1214`, `Computer.cpp:212-215`).
- The data (`DCLP`, 512 KB chunks) is only sent when the owner's cursor leaves it, and
  only pushed to the *active* computer; the rest get it when the cursor enters them
  (`Server.cpp:463-504`, `Client.cpp:231-250, 376-410`).
- Change detection is marshalled-data equality; time stamps are always 0 on Windows.

## Problems, by impact

1. **Second copy is never detected** (`MSWindowsComputer.cpp:1351-1357`): a grab is only
   raised when ownership goes Deskflow → other app. Copy A then copy B in other apps, or
   any copy before Deskflow ever wrote the clipboard, produces no event; it is only
   picked up on the next screen leave, and only if the owner did not change.
2. **Open failures are ignored on read and write** (`MSWindowsComputer.cpp:421-426`,
   `PrimaryClient.cpp:119-124`, `Client.cpp:254`): with clipboard-history and other
   listeners contending, `OpenClipboard` gives up after 5×5 ms; reads then marshal an
   *empty* clipboard that is sent as if real, writes fail silently, and
   `ServerProxy.cpp:554` still logs "clipboard was updated".
3. **A grab wipes every other clipboard before the data exists** (`Server.cpp:1199-1208`):
   if the data then never arrives (oversize, open failure, owner disconnects,
   unsupported format such as copied files), everyone is left with nothing.
4. **Oversize drops everything** (`Server.cpp:1457`, `Client.cpp:396`, default 3 MB):
   the limit applies to text + HTML + bitmap together; a screenshot or an Office copy
   (which renders a DIB) loses the text too, instead of falling back to text only.
5. **Copy on an inactive client is discarded** (`Server.cpp:1179-1181, 1445-1447`,
   `Client.cpp:404`): rejected as mis-sequenced and never resent.
6. **Crossing grabs are not resolved** (no acknowledgement): a server grab crossing the
   active client's own grab empties the client's copy while the server records the
   client as owner.
7. **Stale client data can overwrite a newer server copy** (`Server.cpp:1179, 1190,
   1451`: the owner check is an `assert`, compiled out in release).
8. **Owner is not reset when a client disconnects** (`Server.cpp:1289-1301`).
9. **Chunks go through the OS message queue all at once** (`StreamChunker.cpp:24-50`):
   a full queue drops a chunk silently, which disconnects the client
   (`ServerProxy.cpp:555`) or makes the server drain its input stream
   (`ClientProxy1_0.cpp:124-133`).
10. **Clipboard and Selection both map to the one Windows clipboard**: every transfer
    happens twice, and with an X11 peer the PRIMARY selection overwrites Ctrl+C content.
11. **Reads block the main thread** while the source app renders delayed formats
    (`MSWindowsClipboard.cpp:188`), freezing cursor switching.
12. Smaller ones: HTML fragment offsets not bounds-checked (`std::out_of_range`,
    `MSWindowsClipboardHTMLConverter.cpp:68-75`); `GlobalUnlock` skipped
    (`MSWindowsClipboardAnyTextConverter.cpp:46-50`); `new`/`free()` mismatch for
    `ClipboardInfo` (`ClientProxy1_0.cpp:434`, `ClientProxy1_6.cpp:76`); unchecked
    `readf` results (`ServerProxy.cpp:505, 565`); `>=` vs `>` limit checks.

## Status

Fixed (these work with an unmodified Deskflow on the other side; the server-side ones
take effect where this build is the server):

- 1: every change made by another program is a new grab on Windows, and a change
  whose notification never came is caught by the clipboard sequence number when the
  cursor leaves. What is on the clipboard when Deskflow starts isn't shared until it
  changes.
- 2: a clipboard that can't be opened is never read as empty or sent; reads and writes
  are retried for 3 s (every 250 ms) and the log says what happened.
- 3: a grab no longer empties anyone. The server keeps the previous data but doesn't
  hand it out while the owner's new data is on its way, never sends an empty
  clipboard, and clients ignore an empty one from the server (the old server sends
  one right after a grab and when it starts).
- 4: over the size limit the bitmap is dropped first, then the HTML, so the text still
  goes; only when the text alone is too big is nothing sent.
- 7: an update from a computer that isn't the owner is ignored in release builds too.
- 8: a clipboard owned by a computer that disconnects goes back to the server.
- Loops (deskflow/deskflow#10139): content Deskflow wrote coming back (clipboard
  history, a clipboard manager, the other system's line endings) is recognized and not
  sent back; "unchanged" also ignores line endings.
- Clipboards with nothing Deskflow can share (copied files) are not sent, so they no
  longer empty the other computers.

Still open: 5, 6, 9, 10, 11, 12.

## Fix direction

- Detect every change by clipboard sequence number, not by ownership transitions.
- Retry `OpenClipboard` with backoff off the hot path, and never send or apply an
  empty clipboard that came from a failed open; log failures.
- Do not empty other computers on grab; replace their content when data arrives.
- Per-format size limit with a text-only fallback.
- Reset ownership on disconnect; check the owner in release builds.
- Pace chunk sending instead of queueing all chunks at once.
- Only send the Selection where it means something (X11/Wayland peers).
