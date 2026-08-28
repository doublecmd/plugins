# Double Commander Plugin Manager

Standalone Lazarus/Qt6 plugin manager for [Double Commander](https://doublecmd.sourceforge.io/).

## Features

- **Install** — extract archives, copy plugin binaries, register in `doublecmd.xml`
- **Uninstall** — remove XML nodes and plugin directories
- **Enable / Disable** — relocate nodes to `<DisabledPlugins>` (with JSON reconciliation)
- **Ordering** — reorder plugin priority in XML
- **Tweak** — WCX `<Flags>` bitmask and WLX/WDX `<DetectString>` editing
- **Updates** — URL binding, HTTP HEAD polling, backup/rollback
- **Restart** — graceful DC shutdown before config writes

Metadata (URLs, ETags, rollback state) is stored in `~/.config/doublecmd/plugman.json`.

## Build

Requirements: Lazarus 3.x / FPC 3.2+, `libQt6Pas`, OpenSSL.

```bash
cd doublecmd_plugman
lazbuild --build-mode=Release --widgetset=qt6 plugman.lpi
```

Run `./plugman`.

## Configuration

Settings are stored in `~/.config/doublecmd/plugman.ini`. Override:

- Config directory (where `doublecmd.xml` lives)
- Commander path / executable
- External editor for plugin `.ini`/`.conf` files

## JSON schema

See `default/plugman.json` for the template.

## Platform

Primary target: **Linux / Qt6 / Wayland**.
