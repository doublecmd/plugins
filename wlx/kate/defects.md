# rich_editor_qt WLX Plugin — Known Defects & Focus Architecture Notes

## Part 1 — Known Defects (Unfixed)

These are descriptions of the observed failures as a user or integrator would experience them.
They are not an analysis of cause, nor a list of proposed fixes.

---

### Defect 1 — Content Modification Triggers a Reload

**What happens:**  
When the user modifies the file contents in the editor (types, deletes, or pastes text), the editor
discards those changes and reloads the file from disk — as if an external process had changed it.
The user's edits disappear and the file returns to its on-disk state. This happens immediately
after any modification keystroke, making the editor effectively non-writable in practice despite
appearing to be in read-write mode.

**What it looks like:**  
The user enables write mode, types a character, and before they can do anything else the text
reverts. No dialog is shown; the modification simply vanishes.

---

### Defect 2 — Focus Hijacking (Cannot Click Away to File Panel)

**What happens:**  
Once the user has clicked inside the editor panel and it has received keyboard focus, they cannot
return focus to Double Commander's file list by clicking on it. The editor retains keyboard and
mouse focus. The user is trapped inside the editor panel and must use a workaround — pressing
`Escape`, `Alt+Tab`, clicking the menubar and pressing `Escape`, or closing the viewer — to
regain access to DC's navigation.

**What it looks like:**  
The user clicks a file in the left/right panel. The editor opens. The user reads the file, then
tries to click a different file in DC's panel to navigate. Nothing happens — the file selection
does not change and the editor remains active.

---

### Defect 3 — Non-Printing Characters Not Displayed

**What happens:**  
The "Show Hidden Characters" menu toggle does not reliably show all categories of non-printing
characters. In some states it shows nothing at all; in others it shows only space characters
(rendered as a middle dot `·`). Tab characters, carriage returns (`CR`), and line feeds (`LF`)
are not visually distinguished regardless of the toggle state.

**What it looks like:**  
The user enables "Show Hidden Characters." Lines that contain only spaces may show dots, but a
file with tabs shows no tab markers. A Windows-format file (CRLF line endings) shows no `CR`
symbols. The toggle itself may appear checked or unchecked inconsistently across invocations.

---

### Defect 4 — External File Changes Not Detected / Dialog Not Shown

**What happens:**  
When another process modifies the file that is currently open in the editor (for example, a build
system regenerates it, or the user saves it from another application), the editor does not notify
the user. In Kate, this situation shows a blue notification bar at the top of the editor offering
"Reload" or "Ignore" options. In this plugin, no such bar appears; the editor either silently
reloads (Defect 1) or continues showing the stale on-disk content with no indication that the
file has changed.

**What it looks like:**  
The user opens a file. An external process writes a new version of that file to disk. The editor
continues showing the old content with no visual indicator that the on-disk version now differs
from what is displayed. The user has no way to know the file has changed unless they manually
use "Reload from Disk."

---

## Part 2 — Focus Architecture: Comparison Across Plugins

### The Core Problem

Double Commander (DC) is a Lazarus/Free Pascal application. Its WLX viewer panel is a native
window into which plugin widgets are embedded by win32/Wayland XID/WID reparenting. The plugin's
Qt widget tree shares the same process and the same top-level Wayland surface as DC. Qt's focus
system and the Wayland compositor each maintain their own independent ideas of which widget/surface
"has focus." Keeping these in sync — and keeping focus away from the plugin and with DC's file
panel — is the central challenge every plugin in this repository must solve.

---

### Approach 1 — Active/Inactive State with Application-Level Key Forwarding (mpv_wayland)

**Plugin:** `mpv_wayland` (`MpvWidget`, branch `feature/mpv-wayland`)

**Method:**  
The plugin does not attempt to hold Qt focus for the video canvas at all. Instead it uses a
combination of an `m_isActive` boolean flag and an **application-level event filter** installed
on the top-level window to simulate interaction without ever granting the GL widget compositor
focus:

1. **No setFocus, no ClickFocus:** `mousePressEvent` on the video surface sets `m_isActive =
   true` but explicitly does **not** call `setFocus()`. The comment in the code reads: *"We do
   NOT call setFocus() — that would create a Wayland subsurface focus lock."*

2. **Application-level keyboard forwarding:** The event filter is installed on the **top-level
   window** (not just the widget's children). When `m_isActive` is true, all `QEvent::KeyPress`
   events arriving at the top-level window are intercepted, translated to mpv key names, and
   forwarded to the mpv process via `mpv_command_string()`. The Qt event is consumed so DC never
   sees it. The GL widget never needs focus for this to work.

3. **Outside-click detector:** When `m_isActive` is true, any `QEvent::MouseButtonPress` whose
   global position falls outside the widget's bounds immediately sets `m_isActive = false` and
   lets the click through to DC. This is the deactivation mechanism — it requires no focus
   transfer because focus was never moved.

4. **`ChildAdded` guard:** The event filter also handles `QEvent::ChildAdded`. Whenever the GL
   surface or mpv creates a new child widget dynamically, `Qt::NoFocus` and the event filter are
   immediately applied to it.

5. **Save/restore pattern:** `m_savedFocusWidget` captures `QApplication::focusWidget()` before
   loading a file. `restoreFocusToDC()` is called via `QTimer::singleShot(100, ...)` after the
   file starts loading, returning focus to the saved DC widget without any focus ever being
   transferred to the plugin.

**Strengths:**  
- Completely avoids the Wayland subsurface focus problem by design — `setFocus()` is never
  called on the GL widget, so the compositor never creates an independent focus surface.
- Supports interactive keyboard/mouse input to mpv without holding Qt focus.
- Handles dynamically created children via `ChildAdded`.
- The outside-click detection is explicit and geometry-based, not dependent on `FocusOut` events
  that Wayland doesn't reliably deliver for embedded surfaces.

**Weaknesses:**  
- Key forwarding is manual: every key mpv understands must be mapped via `mapQtKeyToMpvKey()`. A
  character not in the mapping is silently dropped. For a video player this is acceptable (the key
  space is small); for a text editor it is not — the editor must receive the raw key events
  directly into a text canvas.
- The `m_isActive` flag is a soft state: if DC or another window grabs focus at the OS level
  (e.g. a notification, a background process), `m_isActive` remains true and subsequent key
  events will still be forwarded to mpv until the user clicks outside, even though the user
  is no longer interacting with the plugin.

---

### Approach 2 — Layered NoFocus with Save/Restore and Event Filter (wlx-log-viewer)

**Plugin:** `wlx-log-viewer` (`LogViewerWidget`)

**Method:**  
A three-layer strategy:

1. **Container-level:** `Qt::NoFocus` on the container widget itself. No `WA_NativeWindow`,
   no `WA_ShowWithoutActivating` — attributes that would promote the widget to a Wayland
   subsurface with independent compositor-level focus are explicitly avoided.

2. **Child-level:** A recursive walk of the entire child tree at construction time sets
   `Qt::NoFocus` on every non-input widget. An `eventFilter` is installed on every child.
   The event filter intercepts `QEvent::FocusIn`: if the widget receiving focus is not one of
   the explicitly allowed input widgets (e.g. a search bar), focus is immediately redirected
   back to DC via `m_savedFocusWidget->setFocus()`.

3. **Load-time save/restore:** Before loading a new file, the currently focused DC widget is
   saved into `m_savedFocusWidget`. After loading completes, that saved widget's `setFocus()`
   is called to restore DC's focus state. This handles the case where `openUrl()` or an
   equivalent loading operation internally triggers a `setFocus()` on one of KTE's children.

**Strengths:**  
- Handles dynamically created children (the event filter catches focus on widgets that didn't
  exist at construction time by re-running the child walk whenever the filter sees a new widget).
- Handles programmatic focus (intercepted at the `FocusIn` event level, not just at the policy
  level).
- Distinguishes between "allowed" and "denied" focus: the search bar is explicitly permitted
  to receive focus when activated by the user, but immediately deactivated when `Escape` is
  pressed.

**Weaknesses:**  
- Completely prevents editing — the NoFocus policy on all children makes typing in the document
  impossible. Appropriate for a log viewer (read-only by design), but not for an editor.
- Still subject to Wayland subsurface issues if any child acquires `WA_NativeWindow` after
  construction (e.g. lazy initialization inside a third-party component).

---

### Approach 3 — ClickFocus + WA_NativeWindow Strip + EventFilter (rich_editor_qt)

**Plugin:** `rich_editor_qt` (`EditorWidget`)

**Method:**  
A hybrid approach designed to permit intentional user interaction (typing, editing) while still
attempting to prevent the editor from permanently capturing focus:

1. **Container policy:** `Qt::ClickFocus` on the container — the editor acquires focus when
   clicked, which is necessary for typing.

2. **WA_NativeWindow strip:** `Qt::WA_NativeWindow` is explicitly removed from the KTextEditor
   view and its focus proxy immediately after the view is created. The intent is to prevent KTE
   from being promoted to a separate Wayland `wl_surface` (subsurface), which would give it
   independent compositor-level focus management outside Qt's control.

3. **EventFilter on all children:** An event filter is installed on all existing children and
   on the view's focus proxy. The filter intercepts `QEvent::ShortcutOverride` (to prevent DC
   from losing keyboard input to KTE's shortcut handling) and `QEvent::KeyPress` for `Escape`
   (to return focus to DC's parent widget on demand).

4. **No save/restore:** Unlike the logviewer, there is no mechanism to save DC's focus widget
   before a file loads and restore it afterwards.

**Why it still fails (focus):**  
The fundamental issue is that KTextEditor's internal implementation (`KateView`/`KateViewInternal`)
creates native windowing resources and manages focus at a level that `WA_NativeWindow` stripping
alone cannot undo if the attribute is re-applied internally during the view's own initialization
sequence. Additionally, `Qt::ClickFocus` on the container means the first click into the editor
legitimately transfers focus — and there is no mechanism to transfer it *back* when the user
clicks outside. The logviewer's approach works because it aggressively blocks focus acquisition
entirely; this approach permits it for editing purposes but has no complementary mechanism to
release it when the user is done.

**Key architectural tension:**  
The logviewer could apply `Qt::NoFocus` universally because it is read-only — no user input is
ever expected on the document canvas. The editor *requires* that the document canvas receive
keyboard input for editing. This means any focus-blocking strategy aggressive enough to prevent
hijacking will also prevent typing. No approach in the current codebase resolves this tension.
A viable solution would require a mechanism that:
- Allows keyboard input to reach KTE's text canvas while the user is actively typing,
- Detects when the user's intent has shifted (e.g. a mouse click on a DC widget outside the
  plugin boundary), and
- Transfers compositor-level focus back to DC at that point — requiring either a Wayland-level
  hook, a shared-memory IPC channel to DC, or cooperation from DC itself via the WLX API.

