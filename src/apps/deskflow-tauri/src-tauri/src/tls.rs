//! TLS fingerprints: this computer's own, and the databases of trusted peers.
//!
//! Same files and formats as the core and the Qt GUI: `src/lib/net/Fingerprint.cpp`
//! (`v2:sha256:<hex>` lines, older `AA:BB:...` SHA-1 lines) and
//! `src/lib/net/SecureSocket.cpp`, which checks `trusted-servers` when connecting as a
//! client and `trusted-clients` when serving.

use std::path::PathBuf;

use base64::Engine;
use serde::Serialize;
use sha2::{Digest, Sha256};

use crate::i18n;
use crate::paths;
use crate::settings::{CoreMode, IniFile};

pub fn tls_dir() -> PathBuf {
    paths::settings_dir().join("tls")
}

/// Database the core checks in the given mode.
pub fn trusted_db(mode: CoreMode) -> PathBuf {
    tls_dir().join(if mode == CoreMode::Server {
        "trusted-clients"
    } else {
        "trusted-servers"
    })
}

/// `security/certificate`, or the default `<settings>/tls/deskflow.pem`.
pub fn certificate_path(ini: &IniFile) -> PathBuf {
    ini.get("security", "certificate")
        .filter(|p| !p.is_empty())
        .map(PathBuf::from)
        .unwrap_or_else(|| tls_dir().join("deskflow.pem"))
}

/// SHA-256 of the first certificate in a PEM file, as `QSslCertificate::digest` computes it.
pub fn pem_fingerprint(pem: &str) -> Option<String> {
    let begin = "-----BEGIN CERTIFICATE-----";
    let start = pem.find(begin)? + begin.len();
    let end = start + pem[start..].find("-----END CERTIFICATE-----")?;
    let b64: String = pem[start..end]
        .chars()
        .filter(|c| !c.is_whitespace())
        .collect();
    let der = base64::engine::general_purpose::STANDARD.decode(b64).ok()?;
    Some(hex(&Sha256::digest(der)))
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

/// Normalises what the core sends (`4577EBBF...`, possibly with colons) to lower-case hex.
pub fn normalize(fingerprint: &str) -> Option<String> {
    let h: String = fingerprint
        .chars()
        .filter(|c| *c != ':')
        .collect::<String>()
        .to_lowercase();
    (h.len() == 64 && h.chars().all(|c| c.is_ascii_hexdigit())).then_some(h)
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Trusted {
    /// "sha256" or "sha1".
    pub kind: String,
    pub hex: String,
}

/// Parses a database, keeping only valid entries (like `FingerprintDatabase::read`).
pub fn parse_db(text: &str) -> Vec<Trusted> {
    text.lines()
        .filter_map(|line| {
            let line = line.trim();
            if let Some(rest) = line.strip_prefix("v2:") {
                let (kind, h) = rest.split_once(':')?;
                let kind = kind.to_lowercase();
                let h = h.to_lowercase();
                let len = match kind.as_str() {
                    "sha256" => 64,
                    "sha1" => 40,
                    _ => return None,
                };
                (h.len() == len && h.chars().all(|c| c.is_ascii_hexdigit()))
                    .then_some(Trusted { kind, hex: h })
            } else {
                // v1: SHA-1 written as 20 colon-separated bytes
                let h: String = line.split(':').collect::<String>().to_lowercase();
                (line.len() == 59
                    && line.matches(':').count() == 19
                    && h.chars().all(|c| c.is_ascii_hexdigit()))
                .then_some(Trusted {
                    kind: "sha1".into(),
                    hex: h,
                })
            }
        })
        .collect()
}

fn format_db(entries: &[Trusted]) -> String {
    entries
        .iter()
        .map(|e| format!("v2:{}:{}\n", e.kind, e.hex))
        .collect()
}

pub fn read_db(mode: CoreMode) -> Vec<Trusted> {
    std::fs::read_to_string(trusted_db(mode))
        .map(|t| parse_db(&t))
        .unwrap_or_default()
}

pub fn is_trusted(mode: CoreMode, fingerprint: &str) -> bool {
    normalize(fingerprint).is_some_and(|h| {
        read_db(mode)
            .iter()
            .any(|t| t.kind == "sha256" && t.hex == h)
    })
}

fn write_db(mode: CoreMode, entries: &[Trusted]) -> Result<(), String> {
    let path = trusted_db(mode);
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    std::fs::write(&path, format_db(entries)).map_err(|e| {
        i18n::tf(
            "error.tls.write",
            &[("path", &path.display()), ("reason", &e)],
        )
    })
}

pub fn trust(mode: CoreMode, fingerprint: &str) -> Result<(), String> {
    let hex = normalize(fingerprint).ok_or_else(|| i18n::t("error.tls.invalidFingerprint"))?;
    let mut entries = read_db(mode);
    if !entries.iter().any(|t| t.kind == "sha256" && t.hex == hex) {
        entries.push(Trusted {
            kind: "sha256".into(),
            hex,
        });
        write_db(mode, &entries)?;
    }
    Ok(())
}

pub fn forget(mode: CoreMode, kind: &str, hex: &str) -> Result<(), String> {
    let mut entries = read_db(mode);
    entries.retain(|t| !(t.kind == kind && t.hex == hex.to_lowercase()));
    write_db(mode, &entries)
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TlsInfo {
    pub enabled: bool,
    /// Whether a server checks its clients (`security/checkPeerFingerprints`).
    pub check_peers: bool,
    pub local_fingerprint: Option<String>,
    pub certificate: String,
    pub trusted_servers: Vec<Trusted>,
    pub trusted_clients: Vec<Trusted>,
}

pub fn info() -> Result<TlsInfo, String> {
    let ini = IniFile::load(&paths::settings_file()).map_err(|e| e.to_string())?;
    let cert = certificate_path(&ini);
    Ok(TlsInfo {
        enabled: ini
            .get("security", "tlsEnabled")
            .is_none_or(|v| v == "true"),
        check_peers: ini
            .get("security", "checkPeerFingerprints")
            .is_none_or(|v| v == "true"),
        local_fingerprint: std::fs::read_to_string(&cert)
            .ok()
            .and_then(|p| pem_fingerprint(&p)),
        certificate: cert.to_string_lossy().into_owned(),
        trusted_servers: read_db(CoreMode::Client),
        trusted_clients: read_db(CoreMode::Server),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    // Not a real certificate: only the PEM framing and base64 matter here.
    const PEM: &str = "-----BEGIN CERTIFICATE-----
MIIBkTCB+wIUZ5dE3n1F0dDf3l3vU5m3l0Z8fhQwDQYJKoZIhvcNAQELBQAwEjEQ
MA4GA1UEAwwHZGVza2ZsbzAeFw0yNjAxMDEwMDAwMDBaFw0zNjAxMDEwMDAwMDBa
MBIxEDAOBgNVBAMMB2Rlc2tmbG8wXDANBgkqhkiG9w0BAQEFAANLADBIAkEAuX9f
-----END CERTIFICATE-----
";

    #[test]
    fn fingerprint_is_sha256_of_the_der() {
        let der = base64::engine::general_purpose::STANDARD
            .decode(
                "MIIBkTCB+wIUZ5dE3n1F0dDf3l3vU5m3l0Z8fhQwDQYJKoZIhvcNAQELBQAwEjEQ\
                 MA4GA1UEAwwHZGVza2ZsbzAeFw0yNjAxMDEwMDAwMDBaFw0zNjAxMDEwMDAwMDBa\
                 MBIxEDAOBgNVBAMMB2Rlc2tmbG8wXDANBgkqhkiG9w0BAQEFAANLADBIAkEAuX9f",
            )
            .unwrap();
        assert_eq!(pem_fingerprint(PEM), Some(hex(&Sha256::digest(der))));
        assert_eq!(pem_fingerprint("no certificate here"), None);
    }

    /// Cross-check against OpenSSL with a real certificate:
    /// `DESKFLOW_TEST_CERT=path DESKFLOW_TEST_CERT_SHA256="$(openssl x509 -in path -noout -fingerprint -sha256)" cargo test`
    #[test]
    fn matches_openssl_for_a_real_certificate() {
        let (Ok(path), Ok(expected)) = (
            std::env::var("DESKFLOW_TEST_CERT"),
            std::env::var("DESKFLOW_TEST_CERT_SHA256"),
        ) else {
            return;
        };
        let pem = std::fs::read_to_string(path).unwrap();
        let expected = expected.rsplit('=').next().unwrap();
        assert_eq!(pem_fingerprint(&pem), normalize(expected));
    }

    /// Read-only check against the real install of the machine running the tests:
    /// `DESKFLOW_TEST_REAL_INSTALL=1 cargo test real_install -- --nocapture`
    #[test]
    fn real_install() {
        if std::env::var("DESKFLOW_TEST_REAL_INSTALL").is_err() {
            return;
        }
        let info = info().unwrap();
        println!("certificate: {}", info.certificate);
        println!("local fingerprint: {:?}", info.local_fingerprint);
        println!("trusted servers: {:?}", info.trusted_servers);
        println!("trusted clients: {:?}", info.trusted_clients);
        assert!(info.local_fingerprint.is_some());
    }

    #[test]
    fn reads_the_core_database_formats() {
        let db = "v2:sha256:4577ebbf2317c8d371fc408d2b72810c237473f774260d158bbb9eb6fe549dc1\n\
                  v2:sha1:00112233445566778899aabbccddeeff00112233\n\
                  AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD\n\
                  v2:sha256:tooshort\n\n";
        let entries = parse_db(db);
        assert_eq!(entries.len(), 3);
        assert_eq!(entries[0].kind, "sha256");
        assert_eq!(
            entries[2],
            Trusted {
                kind: "sha1".into(),
                hex: "aabbccddeeff00112233445566778899aabbccdd".into()
            }
        );
        assert_eq!(
            parse_db(&format_db(&entries)),
            entries,
            "roundtrip writes v2 lines"
        );
    }

    #[test]
    fn normalizes_what_the_core_sends() {
        let upper = "4577EBBF2317C8D371FC408D2B72810C237473F774260D158BBB9EB6FE549DC1";
        assert_eq!(normalize(upper).unwrap(), upper.to_lowercase());
        assert_eq!(normalize("45:77:EB").as_deref(), None);
    }
}
