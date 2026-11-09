# Migration notes: v1.0 → v2.0

**Your save files are compatible.** The 512-byte canonical format is unchanged and every file v1.0 wrote still loads.

## A bug in v1.0 you should know about

v1.0 treated the two **menu blocks** (`0x1C0`, `0x1E0`) as if they carried the slot signature `0x4441` ("DA"). The game uses
`0x4849` ("HI") for them. Consequences in v1.0:

* a genuine game-written file was reported as invalid (the menu blocks "failed" the signature check), and
* every **Save** rewrote the menu signature to "DA". The game then fails to verify both menu copies on boot and runs
  `wipe_main_menu_data()`, resetting the sound mode and the coin-score age data (stars and slots are not affected).

v2.0 uses `0x4849`. A file that v1.0 saved will show the menu pair as **both copies invalid** in v2.0 (signature "DA"). Nothing is
guessed or auto-repaired. To fix it: switch to Expert mode, open *Validation & Repair*, tick the acknowledgement under
"Menu block", and use *Rebuild checksum* on the primary and then the backup. That rewrites the correct `HI` signature and
checksum; the menu bytes themselves (sound mode, coin-score ages) were never altered by v1.0. If the game already booted a
v1.0-saved file it will have reset that menu data on its own. A test (`migration: a menu block that v1.0 signed with 'DA' ...`)
covers this path, and `regression: saving never rewrites the menu signature` guards the original bug.

## Behaviour changes

| v1.0 | v2.0 | Why |
|---|---|---|
| `Core::save` silently rewrote every signature and checksum | writes the bytes exactly as they are; structured edits keep signatures valid | hidden rewriting hid raw-edit damage and caused the menu bug |
| Replacing the destination could delete it before the rename | temp file + fsync + size check + `.bak` + rename; original untouched on failure | data safety |
| `Core::exportFile` modified the loaded data | export never changes the open data | surprising side effect |
| GameShark compared against the buffer *after the last save* | compares against the image *as loaded* | the running game still has the old bytes |
| Constructor produced an all-zero "valid" file | produces the same bytes the game writes for a fresh cartridge (menu defaults included) | accurate new files |
| Total stars counted only `courseStars[]` | counts what the game counts, including the castle (Toad/MIPS) star bits | v1.0 under-counted up to 5 stars |
| `Core::set` threw `std::out_of_range` | returns `false` | no exceptions for UI input |
| `SM64HexEditorUI.*` | replaced by `SM64App.*`, `SM64Pages*.cpp`, `SM64Theme.*` | new navigation/layout |

## API compatibility

`Core::load / save / exportFile / bytes / byte / set / setMirrored / validate / repair / normalize / gameShark` keep their names
and argument lists. The old `sm64::P`, `B`, `Size`, `Block`, `Courses` constants remain. `Profile` gained `BIN`; existing
values are unchanged.
