# Super Mario 64 EEPROM save format

This document records the retail SM64 save layout the editor implements **and the evidence for each
claim**. Everything marked *verified* was cross-checked against the
[n64decomp/sm64](https://github.com/n64decomp/sm64) decompilation (revision `9921382a`, files
`src/game/save_file.c`, `src/game/save_file.h`, `include/course_table.h`, `levels/course_defines.h`,
`src/game/interaction.c`). The editor's tests encode the same facts independently, so a mistake in
`SM64SaveFormat.hpp` would be caught rather than silently repeated.

All multi-byte values are **big-endian**. The editor never depends on host endianness or struct packing.

## File layout (512 bytes, 4 Kbit EEPROM) — verified

| Offset  | Size | Block                |
|---------|------|----------------------|
| `0x000` | 56   | Slot A primary       |
| `0x038` | 56   | Slot A backup        |
| `0x070` | 56   | Slot B primary       |
| `0x0A8` | 56   | Slot B backup        |
| `0x0E0` | 56   | Slot C primary       |
| `0x118` | 56   | Slot C backup        |
| `0x150` | 56   | Slot D primary       |
| `0x188` | 56   | Slot D backup        |
| `0x1C0` | 32   | Menu data primary    |
| `0x1E0` | 32   | Menu data backup     |

`8 × 56 + 2 × 32 = 512`. The editor asserts this at compile time.

## Save slot (56 bytes, `struct SaveFile`) — verified

| Offset | Size | Field                                   |
|--------|------|-----------------------------------------|
| 0      | 1    | `capLevel` (where the cap was lost)     |
| 1      | 1    | `capArea`                               |
| 2      | 6    | `capPos` (3 × s16)                      |
| 8      | 4    | `flags` (see below)                     |
| 12     | 25   | `courseStars[25]`                       |
| 37     | 15   | `courseCoinScores[15]`                  |
| 52     | 2    | signature magic `0x4441` (`44 41`, "DA")|
| 54     | 2    | checksum                                |

The signature is **not** at bytes 0–1; it sits immediately before the final two checksum bytes.

### Checksum — verified

`calc_checksum()` is the 16-bit sum of every byte of the block **except the last two** (the stored
checksum itself). The signature bytes *are* included. It is a plain byte sum, not a sum of 16-bit words,
and it is stored big-endian. There is no additional modifier. (The N64 ROM header CRC1/CRC2 is a different
thing and has nothing to do with the save.)

Check vector: an empty slot's checksum is `0x0085` (just `0x44 + 0x41`).

### Flags word — verified (`SAVE_FLAG_*`)

| Bit | Meaning                          | Bit | Meaning                              |
|-----|----------------------------------|-----|--------------------------------------|
| 0   | File exists                      | 11  | `UNLOCKED_WF_DOOR`                   |
| 1   | Have Wing Cap (red switch)       | 12  | `UNLOCKED_CCM_DOOR`                  |
| 2   | Have Metal Cap (green switch)    | 13  | `UNLOCKED_JRB_DOOR`                  |
| 3   | Have Vanish Cap (blue switch)    | 14  | `UNLOCKED_BITDW_DOOR`                |
| 4   | Have key 1 (basement key)        | 15  | `UNLOCKED_BITFS_DOOR`                |
| 5   | Have key 2 (upstairs key)        | 16  | Cap on ground                        |
| 6   | Unlocked basement door           | 17  | Cap on Klepto                        |
| 7   | Unlocked upstairs door           | 18  | Cap on Ukiki                         |
| 8   | Dire, Dire Docks moved back      | 19  | Cap on Mr. Blizzard                  |
| 9   | Moat drained                     | 20  | Unlocked 50-star door                |
| 10  | `UNLOCKED_PSS_DOOR`              | 24–30 | Castle secret stars (7-bit field)  |

Using a key sets the matching `UNLOCKED_*` flag and clears the `HAVE_KEY_*` flag (`interaction.c`), which
is why the castle-access preset clears the held-key bits.

### Stars — verified

`courseStars[i]` holds one byte per course; **bits 0–6 are star flags** (mask `0x7F`), so a main course
has up to seven stars (six missions plus the 100-coin star). Indices follow `levels/course_defines.h`
(course number minus one):

| Index | Course                      | Index | Course                       |
|-------|-----------------------------|-------|------------------------------|
| 0–14  | The 15 main courses (BOB … RR, in menu order) | 19 | Cavern of the Metal Cap |
| 15    | Bowser in the Dark World    | 20    | Tower of the Wing Cap        |
| 16    | Bowser in the Fire Sea      | 21    | Vanish Cap Under the Moat    |
| 17    | Bowser in the Sky           | 22    | Wing Mario Over the Rainbow  |
| 18    | Princess's Secret Slide (2 stars: bits 0, 1) | 23 | The Secret Aquarium |
|       |                             | 24    | Cake ending entry (unused)   |

The castle stars (three Toad stars, two MIPS stars) are bits 24–28 of the flags word.

**Total stars is derived, never stored**: the game sums the 7-bit popcount of all 25 `courseStars` bytes
plus the castle-star bits. A completed file is exactly `15 × 7 + 15 = 120`. The editor reports the same
number the game would show, and flags any star bit that no retail star corresponds to.

### Cannon flag quirk — verified

Bit 7 of a course byte is the *cannon open* flag. The game indexes it with the **1-based course number**
(`courseStars[gCurrCourseNum]`), so the cannon flag for course *N* lives in the byte that follows course *N*.
Star edits in this editor never touch bit 7.

### Lives — not stored

The retail game keeps Mario's lives in runtime state only; there is no lives field in the EEPROM. The editor
therefore has no "Lives" control.

## Menu block (32 bytes, `struct MainMenuSaveData`) — verified

| Offset | Size | Field                                          |
|--------|------|------------------------------------------------|
| 0      | 16   | `coinScoreAges[4]` (u32 each)                  |
| 16     | 2    | `soundMode`                                    |
| 18     | 10   | padding (EU builds keep a `language` u16 at 18)|
| 28     | 2    | signature magic **`0x4849` ("HI")**            |
| 30     | 2    | checksum (same algorithm)                      |

The menu block's magic is `HI`, **not** `DA`. (Version 1.0 of this editor used `DA` for the menu blocks, which
made authentic files look invalid and rewrote the menu signature on every save. See `MIGRATION.md`.)

A freshly wiped menu block (`wipe_main_menu_data`) has `coinScoreAges = {0x3FFFFFFF, 0x2AAAAAAA, 0x15555555, 0}`
and `soundMode = 0`. A freshly erased slot (`save_file_erase`) is all zeros with a valid signature.

## How the game treats damaged blocks — verified

On boot, for each pair the game verifies both copies and: if both valid, uses the primary in memory; if only one
is valid, restores the other from it; if neither is valid, erases the slot (or wipes the menu data). The editor's
repair mirrors this and adds one rule the game does not need: if both copies are valid but **different**, the
editor never picks one for you.

## What is *not* covered / not verified

* Region differences. The layout above is identical for US/JP/EU/Shindou builds apart from the EU `language`
  field noted above; the editor does not interpret it.
* Per-star names. The 90 mission names are not shipped, so tooltips say "star N" rather than naming missions.
* The meaning of the lost-cap position bytes beyond "x/y/z"; they are displayed to Expert users but not edited.
* Hacks, ports and decompilation forks may use different layouts. The editor assumes retail.
* GameShark addresses. The RDRAM address of the save buffer differs between regions and revisions and is not
  verified for any of them; the editor asks you for it.
* Export profile names for specific flashcarts: no device-specific byte transformation is documented, so those
  profiles write the canonical bytes and are labelled *experimental*.
