# Changelog

## 2.0.0

### Fixed
* Menu blocks now use the correct `0x4849` ("HI") signature (v1.0 used `0x4441` and rewrote it on every save).
* Total stars now includes the castle (Toad/MIPS) star bits, matching the game.
* Saving no longer silently rewrites checksums; writes are atomic with a `.bak` copy and read-back verification.
* No duplicate Dear ImGui IDs (nested `PushID` for slot/course/star); enforced by tests.
* Responsive layout: sidebar collapses to a combo at small sizes; works at 800×600; UI scale 75–200% on top of OS scaling.
* Dependency handling: portable-file-dialogs is header-only and no longer configured as a CMake project; versions pinned.

### Added
* Verified bit maps for every star (105 main + 15 secret = 120), all castle flags, cap/key/door flags (`SAVE_FORMAT.md`).
* Sidebar navigation, dashboard, slot cards with completion rings, search/filter course table, secret-star page, castle page.
* Undo/redo with history, dirty tracking, "unsaved changes" list, unsaved-change prompts, overwrite confirmation, recent files.
* Presets with a field-level old/new preview, each one undo step (new blank, occupied A, caps, castle access, 70 stars, 120 stars,
  clear, copy, sync, restore, repair, novelty, random challenge).
* Validation & Repair page: per-pair status, explicit choice when both copies are valid but differ, advanced checksum rebuild.
* Hex editor: virtualized table, region colours, selection, hex typing, search, jump, compare primary/backup, copy/paste,
  changed-byte highlighting, tooltips naming each field.
* Export profiles that state size/byte order/padding/extension/verified status; GameShark generator with range validation.
* Extras: command palette, welcome tour, light/dark themes + cap-colour accents, JSON and text validation reports, CRC-32 file ID,
  notes sidecar files, drag-and-drop, read-only forensic mode, portable mode (`portable.txt` next to the executable).
* Tests (engine, UI ID audit, app workflow), sanitizer and warning builds, CI for Windows/Linux/macOS, release workflow.
