# certview — Unified Certificate, Key, and Trust Store Plugin

A fast, self-contained Lister (WLX) plugin for [Double Commander](https://doublecmd.sourceforge.io/) that inspects, analyzes, and visualizes certificates, revocation lists, cryptographic containers, keys, and trust stores.

Ships as **two independent native builds** — one for Double Commander's **Qt6** build (`certview_qt6.wlx`) using `wlxbase_wlqt`, and one for its **GTK3** build (`certview_gtk3.wlx`). Both share the identical `certview_core` engine with an embedded, zero-dependency cryptographic parsing core.

---

## Features

- **Zero External Runtime Dependencies:** Everything is self-contained. No external CLI calls to `openssl`, `gpg`, or `certtool`. Cryptographic and ASN.1 parsing is powered by statically embedded, vendored MbedTLS and dedicated native parsers.
- **Extensive Format & Identity Support:**
  - **X.509 Certificates:** `.cer`, `.crt`, `.der` (binary ASN.1), `.pem` (ASCII armored), and multi-certificate system trust bundles (e.g. `/etc/ssl/certs/ca-certificates.crt`).
  - **Certificate Requests & Revocation:** `.csr`, `.req` (PKCS#10 Certificate Signing Requests), `.crl` (X.509 Certificate Revocation Lists).
  - **PKCS#7 / CMS Bundles:** `.p7b`, `.p7c`, `.spc` certificate chains and degenerate PKCS#7 signed data containers.
  - **PKCS#12 Containers:** `.p12`, `.pfx` archives. Header inspection displays safe-bag encryption algorithms, MAC iterations, and salt metadata; provides inline password decryption to inspect contained certificates.
  - **GnuPG / OpenPGP Formats:**
    - `.asc` (Armored OpenPGP public and private keys).
    - `.gpg`, `.pgp` (Binary OpenPGP keyrings and packet streams).
    - `trustdb.gpg` (GnuPG Web-of-Trust database v2/v3 record and trust policy inspection).
  - **Private Keys (`.key`):**
    - Standard PKCS#1 and PKCS#8 private keys (PEM and DER) with algorithm and bit length detection.
    - GnuPG S-expression private keys (`~/.gnupg/private-keys-v1.d/*.key`), supporting passphrase-protected keys and smartcard/hardware token shadowed keys (`Token: D276...`).
  - **GnuPG Keybox (`.kbx`):** Modern GnuPG key database format (`pubring.kbx`), parsing OpenPGP key blobs, embedded X.509 DER certificates, fingerprints, and user identities.
  - **SSH Public Keys (`.pub`):** RFC 4716 and OpenSSH public keys (`ssh-rsa`, `ssh-ed25519`, `ecdsa-sha2-*`, `sk-*`), calculating SHA-256 and MD5 fingerprints.
- **Multi-Certificate & Multi-Key Browser:**
  - Instant `[1/N]` combobox selector in the top toolbar to browse all certificates or keys inside trust bundles, keyrings, or keybox databases.
- **Visual Status Badges:**
  - Color-coded validity indicator: `✓ VALID` (green), `EXPIRED` (red), `NOT YET VALID` (orange), `ENCRYPTED CONTAINER` (blue), or `INFO` (cyan).
  - Remaining days calculation badge.
- **Structured Tabs:**
  - **General:** Subject, Issuer, Validity period (Not Before / Not After), Serial Number, Key Algorithm, Key Bits, Signature Algorithm, SHA-256 and SHA-1 Fingerprints.
  - **Extensions & SANs:** Subject Alternative Names (DNS, IP, email, URI), Key Usage flags, Extended Key Usage (EKU) OIDs.
  - **Details / Raw:** Comprehensive human-readable certificate dump and raw PEM/key bytes.
- **Clipboard & Export Actions:**
  - **Copy Summary**: Formats all properties across the selected entity into clean key-value text.
  - **Copy Raw / Key**: Copies the raw PEM block or key data directly to the clipboard.
  - Full keyboard copy support on all tables and text views.

---

## Architecture

```
certview_core (static library, UI-agnostic)
    ├── CertModel.h          → Entity structures (CertEntry, CertDocument, Pkcs12Metadata)
    ├── CertParser.h / .cpp  → Format dispatch, MbedTLS ASN.1/X.509/PK, OpenPGP, KBX, TrustDB, SSH
    └── vendored_mbedtls     → Embedded crypto & ASN.1 core
certview_qt6.wlx (shared library)
    ├── src/qt6/plugin_qt6.cpp     → WLX C interface (ListLoad, ListLoadNext, ListCloseWindow, etc.)
    ├── src/qt6/CertViewWidget.cpp → Qt6 UI tabs, tables, status banner, password unlock
    └── wlxbase_wlqt               → Wayland-safe crash logging and base toolkit utilities
certview_gtk3.wlx (shared library)
    └── src/gtk3/plugin_gtk3.cpp   → WLX C interface + GTK3 GtkNotebook & GtkGrid viewer
```

---

## Building

### Prerequisites

- CMake ≥ 3.20
- C++17 compiler (`g++` or `clang++`)
- Double Commander base library dependencies (`wlxbase_wlqt` / `wlxbase_gtk`)
- For Qt6 build: Qt6 (`Core`, `Gui`, `Widgets`)
- For GTK3 build: GTK3 (`gtk+-3.0`)

### Build Steps

```bash
cd wlx/certview
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

Generated outputs in `build/`:
- `certview_qt6.wlx` (for Qt6 Double Commander)
- `certview_gtk3.wlx` (for GTK3 Double Commander)

---

## Installation in Double Commander

1. In Double Commander, open **Configuration** → **Options** → **Plugins** → **WLX plugins**.
2. Click **Add** and select `certview_qt6.wlx` (or `certview_gtk3.wlx`).
3. Set the detect string to:
   ```
   EXT="CER" | EXT="CRT" | EXT="DER" | EXT="PEM" | EXT="P12" | EXT="PFX" | EXT="CSR" | EXT="REQ" | EXT="CRL" | EXT="P7B" | EXT="P7C" | EXT="SPC" | EXT="KBX" | EXT="ASC" | EXT="GPG" | EXT="PGP" | EXT="PUB" | EXT="KEY"
   ```
4. Press **F3** (or **Ctrl+Q** for Quick View) on any certificate, key, or trust database to inspect it.
