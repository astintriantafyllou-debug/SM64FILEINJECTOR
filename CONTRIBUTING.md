# Contributing

1. Build and run the tests first (see [BUILDING.md](BUILDING.md)). A change is not done until `ctest` passes.
2. **Binary-format changes need evidence.** Do not add or relabel a bit mapping from memory. Cite the decompilation file and
   add an independent test (expected values written by hand, not read back from the table under test). Record it in
   [SAVE_FORMAT.md](SAVE_FORMAT.md).
3. **Every repeated Dear ImGui widget needs a unique ID.** Wrap loops in `ImGui::PushID(slot)`, `PushID(course)`, `PushID(star)`.
   `sm64-ui-tests` fails if any page or dialog submits two items with the same ID. Never label a widget `"##Star"` in a loop
   without a pushed ID.
4. Keep engine code free of ImGui/SDL. UI code must not touch bytes directly; go through `sm64::Core` so every edit is one
   undoable history entry.
5. Style: `clang-format` (see `.clang-format`). Warnings are errors in CI for GCC and Clang.
6. Do not add ROMs, Nintendo assets or fonts you cannot redistribute.
7. Be honest in labels: if a behaviour is not verified, say *experimental* or leave it out.
