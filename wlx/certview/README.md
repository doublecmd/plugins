# certview

A fast, self-contained Lister (WLX) plugin for [Double Commander](https://doublecmd.sourceforge.io/) to inspect and visualize X.509 certificates and PKCS#12 bundles.

Available in both **Qt6** (`certview_qt6.wlx`) and **GTK3** (`certview_gtk3.wlx`) variants.

---

## Features

- **No external runtime CLI or package dependencies:** Vendored MbedTLS statically embedded into the plugin binaries.
- **Formats supported:**
  - `.cer`, `.crt`, `.der` (binary ASN.1)
  - `.pem` (ASCII armor)
  - `.p12`, `.pfx` (PKCS#12 archive metadata and password unlock)
- **GUI Views:**
  - **General:** Subject, Issuer, Validity period (Not Before / Not After), Remaining days badge (Valid / Expired / Not Yet Valid), Serial Number, Key Algorithm, Key Bits, Signature Algorithm, SHA-256 and SHA-1 Fingerprints.
  - **Extensions & SANs:** Subject Alternative Names (DNS, IP, email, URI), Key Usage, Extended Key Usage (EKU).
  - **Details / PEM:** Formatted text representation and raw PEM block.
- **Easy Clipboard Access:**
  - **"Copy Summary"** button to copy all certificate fields in key-value text format.
  - **"Copy PEM / Raw"** button to copy the PEM block directly for command-line or config use.
  - All table cells and text areas are also directly selectable for standard copy.

---

## Architecture

- `src/core/`: UI-agnostic C++ certificate model (`CertModel.h`) and parser (`CertParser.h`, `CertParser.cpp`) using statically embedded MbedTLS.
- `src/qt6/`: Qt6 Lister widget (`CertViewWidget`) integrated with `wlxbase_wlqt`.
- `src/gtk3/`: GTK3 Lister widget integrated with GtkNotebook and GtkGrid.
- `3rdparty/mbedtls/`: Vendored MbedTLS C sources compiled with `-fPIC`.

---

## Build

Requires `cmake`, `ninja` or `make`, and `g++` / `clang++`.

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

Build targets generated:
- `certview_qt6.wlx` (for Qt6 Double Commander builds)
- `certview_gtk3.wlx` (for GTK3 Double Commander builds)

---

## Installation in Double Commander

1. In Double Commander, open **Configuration** -> **Options** -> **Plugins** -> **WLX plugins**.
2. Click **Add** and choose `certview_qt6.wlx` (or `certview_gtk3.wlx` if running the GTK build).
3. The plugin will register with the detect string:
   ```
   EXT="CER" | EXT="CRT" | EXT="DER" | EXT="PEM" | EXT="P12" | EXT="PFX"
   ```
4. Press **F3** or **Ctrl+Q** on any certificate to view it.
