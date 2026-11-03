# Copying files between computers

Copy files or folders in Explorer on one computer, paste them on another. Text,
HTML and images already travel as clipboard formats; files are different: they can
be large, many, and must arrive complete and unaltered.

## Decisions

**Copied when the cursor arrives, pasted from a local copy.** Like the other
formats today, the data goes to a computer when the cursor enters it. The receiving
core writes the files into a per-user cache and puts them on the system clipboard as
ordinary files (`CF_HDROP` on Windows). Pasting is then a local copy that every
program understands: Explorer, file dialogs, browsers' upload, mail attachments.

The alternative, handing the files over only when pasted (virtual files with
`CFSTR_FILEDESCRIPTOR`/`CFSTR_FILECONTENTS`, as Remote Desktop does), avoids moving
files nobody pastes, but only some programs accept virtual files, and Explorer
waits on the network inside the paste. It may come later for very large copies.

**Its own messages, offered by the server.** A classic core disconnects on unknown
messages and misreads unknown clipboard formats. So:

- a new server offers the feature in the options it sends (`DSOP`, option `FCLP`
  with the size limit); classic clients ignore unknown options;
- a new client that sees the offer answers with its own limit (`CFCL`); only then
  are file messages exchanged in either direction;
- the file list never goes into the marshalled clipboard: text/HTML/image sharing
  keeps working with every peer, files only between new ones.

**The server is the hub, as for the other formats.** A copy on a client goes to the
server (into its cache) when the cursor leaves the client, and from there to
whichever computer the cursor enters. A copy on the server is read from disk when
needed.

## Robustness

- **Integrity:** every file carries its size and SHA-256; the receiver verifies both
  before anything reaches the clipboard. A transfer is all or nothing: files are
  written under a staging folder and moved into place only when all are verified.
- **A file changing while it is read** (size or modification time differ after the
  read) aborts that transfer with a clear reason.
- **Paths from the network are untrusted:** relative only, `/`-separated, no `..`,
  no drive letters, no `:` (NTFS streams), no Windows device names (`CON`, `NUL`,
  `COM1`…), no control characters, no trailing dots or spaces, length limits. A list
  that breaks any rule is refused as a whole.
- **Limits** (configurable): total size (default 500 MB), number of files (10 000),
  depth. Free disk space is checked before accepting. Symbolic links and junctions
  are not followed.
- **Input stays responsive:** files travel in 64 KiB chunks paced through the event
  queue with a bounded amount in flight, so key and mouse messages are never stuck
  behind a large file (the text clipboard queues every chunk at once today).
- **Superseded or broken transfers stop:** a newer copy anywhere cancels the older
  transfer; a disconnect discards partial files; nothing half-written ever reaches
  the clipboard.
- **Runs as the user:** when the core runs as SYSTEM (service mode), files are read
  and written impersonating the logged-on user, so the user's network drives work
  and the cache belongs to the user. At the login screen there is no user and no
  file transfer.
- **Cache:** `%LOCALAPPDATA%\Deskflow\Clipboard\<id>\` of the logged-on user; the
  last two transfers are kept (one may still be being pasted), older ones and any
  leftover staging folders are deleted.
- **Echoes:** putting received files on the clipboard is Deskflow's own write
  (ownership format), not a new copy.
- **Paste copies, never moves:** received files go on the clipboard with
  "Preferred DropEffect" set to copy. A cut on the source computer is copied too:
  the original stays where it is.
- **Pasting late:** only the last two copies are kept in the cache; pasting from
  an older one finds its files gone.

## Messages

Not tied to a protocol version: a new server adds the option `FCLP` to the
options it sends; a new client that can share files answers `CFCL`. Only then
are the others exchanged. Every one names its copy with an id (1 to 64 lowercase
hex digits); messages for a copy that is no longer current are ignored.

| Code | Direction | Content |
|------|-----------|---------|
| option `FCLP` | server → client | max MiB the server accepts |
| `CFCL` | client → server | max MiB the client accepts |
| `DFLS` | both | copy id, file list (paths, sizes, kinds, times) |
| `DFCH` | both | copy id, file index, the next 1 to 64 KiB of the file |
| `DFDN` | both | copy id, file index, SHA-256 of the file |
| `DFAK` | both | copy id: one chunk or file end was received |
| `DFCN` | both | copy id, reason: stop this copy |

**Flow control:** the receiver acknowledges every chunk and every file end; the
sender never has more than 8 unacknowledged (512 KiB), so key and mouse messages
are never stuck behind file data in the socket. The last file's acknowledgment is
sent once the copy is in place, so "sent" means the other computer has it.

## Code

- `src/lib/deskflow/files/FileList`: the list, its validation and binary form,
  walking a selection.
- `FileTransfer`: `Reader` (chunks, detects changes), `Writer` (staging,
  verification, all or nothing), `Cache`, `AsSessionUser`.
- `FileChannel`: one connection's transfers (one out, one in), flow control,
  cancellation; no sockets, so it is tested back to back.
- `FileHub`: the server's decisions: the current copy, who has it, where the
  cursor is, what to cancel.
- `ServerProxy`/`Client` (client) and `ClientProxy1_6`/`Server` (server) read and
  write the messages and connect these to the clipboard.
- The platform says whether it can share files and reads/writes them on the
  clipboard (`IPlatformComputer::canShareFiles`, `getClipboardFiles`,
  `setClipboardFiles`); until a platform does, files aren't offered from it.

Settings: `core/fileClipboard` (on by default) and `core/fileClipboardMaxSize`
(MiB, 500 by default), on each computer: each refuses copies above its own limit
and doesn't send copies above the other's.

## Work in steps

1. (done) Model and storage, no network: file list format and validation, reading a
   selection into a list, writing a transfer into the cache with verification,
   cache cleanup, user impersonation. Unit tests.
2. (done) Protocol: negotiation, messages, pacing, hub logic in the server, cancellation.
3. (done) Windows clipboard: read `CF_HDROP`, write `CF_HDROP` + preferred drop effect
   (`platform/MSWindowsClipboardFiles`). Other platforms don't offer files yet.
4. (done) GUI: progress and results in the clipboard panel; settings for the limit.
   The core reports results as clipboard activity (`clipboard=` with a `files`
   summary, `files/FileReport`) and progress as `fileProgress=`, at most 4 times a
   second.
