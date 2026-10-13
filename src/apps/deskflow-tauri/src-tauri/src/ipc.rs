//! Client side of the Deskflow IPC protocol (see `src/lib/deskflow/ipc/IpcServer.cpp`).
//!
//! Both the core and the daemon listen on a `QLocalServer`: a named pipe on
//! Windows, a Unix socket elsewhere. Messages are UTF-8 lines of the form
//! `command` or `command=args`; a client must first send `hello=<version>+<sha>`.

use std::io;
use std::time::Duration;

use tokio::io::{AsyncBufReadExt, AsyncRead, AsyncWrite, AsyncWriteExt, BufReader, Lines, ReadHalf, WriteHalf};

use crate::paths;

pub trait Stream: AsyncRead + AsyncWrite + Unpin + Send {}
impl<T: AsyncRead + AsyncWrite + Unpin + Send> Stream for T {}

pub type Reader = Lines<BufReader<ReadHalf<Box<dyn Stream>>>>;
pub type Writer = WriteHalf<Box<dyn Stream>>;

const REPLY_TIMEOUT: Duration = Duration::from_secs(3);

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Handshake {
    Ok,
    /// The server still accepts us and broadcasts to us, but it is a different build.
    VersionMismatch(String),
}

pub struct Connection {
    pub reader: Reader,
    pub writer: Writer,
}

async fn open(name: &str) -> io::Result<Box<dyn Stream>> {
    let endpoint = paths::ipc_endpoint(name);
    #[cfg(windows)]
    {
        let pipe = tokio::net::windows::named_pipe::ClientOptions::new().open(&endpoint)?;
        Ok(Box::new(pipe))
    }
    #[cfg(unix)]
    {
        let sock = tokio::net::UnixStream::connect(&endpoint).await?;
        Ok(Box::new(sock))
    }
}

/// Splits `command=args` the way the Qt GUI does: everything after the first `=` is the argument.
pub fn parse(line: &str) -> (&str, &str) {
    line.split_once('=').unwrap_or((line, ""))
}

impl Connection {
    pub async fn connect(name: &str, version_id: &str) -> io::Result<(Self, Handshake)> {
        let (r, w) = tokio::io::split(open(name).await?);
        let mut conn = Connection { reader: BufReader::new(r).lines(), writer: w };
        conn.send(&format!("hello={version_id}")).await?;
        loop {
            let line = conn.next_line_timeout().await?;
            match parse(&line) {
                ("hello", _) => return Ok((conn, Handshake::Ok)),
                ("versionMismatch", theirs) => return Ok((conn, Handshake::VersionMismatch(theirs.to_owned()))),
                ("error", detail) => {
                    return Err(io::Error::new(io::ErrorKind::ConnectionRefused, format!("ipc rejected: {detail}")));
                }
                _ => continue,
            }
        }
    }

    pub async fn send(&mut self, message: &str) -> io::Result<()> {
        self.writer.write_all(format!("{message}\n").as_bytes()).await?;
        self.writer.flush().await
    }

    pub async fn next_line_timeout(&mut self) -> io::Result<String> {
        match tokio::time::timeout(REPLY_TIMEOUT, self.reader.next_line()).await {
            Ok(Ok(Some(line))) => Ok(line),
            Ok(Ok(None)) => Err(io::ErrorKind::UnexpectedEof.into()),
            Ok(Err(e)) => Err(e),
            Err(_) => Err(io::ErrorKind::TimedOut.into()),
        }
    }

    /// Sends a command and waits for its reply (`ok`, `error` or `<command>=...`),
    /// skipping any broadcast lines the server interleaves.
    pub async fn request(&mut self, message: &str) -> io::Result<String> {
        self.send(message).await?;
        let (command, _) = parse(message);
        loop {
            let line = self.next_line_timeout().await?;
            let (reply, _) = parse(&line);
            if reply == "ok" || reply == "error" || reply == command {
                return Ok(line);
            }
        }
    }
}

/// Runs a short sequence of commands against the daemon and returns the replies.
pub async fn daemon_request(version_id: &str, messages: &[String]) -> io::Result<Vec<String>> {
    let (mut conn, _) = Connection::connect(paths::DAEMON_IPC, version_id).await?;
    let mut replies = Vec::with_capacity(messages.len());
    for m in messages {
        let reply = conn.request(m).await?;
        if parse(&reply).0 == "error" {
            return Err(io::Error::other(format!("daemon rejected '{m}': {reply}")));
        }
        replies.push(reply);
    }
    Ok(replies)
}
