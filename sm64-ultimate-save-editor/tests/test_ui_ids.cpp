// Headless Dear ImGui tests.
//
// Dear ImGui is built with IMGUI_ENABLE_TEST_ENGINE for this target, which calls
// ImGuiTestEngineHook_ItemAdd() for EVERY item submitted (even ones clipped off
// screen). Recording those IDs lets the tests prove that no page, in any mode or
// window size, submits two items with the same ID - the "7 visible items with
// conflicting ID" bug from v1.0. ImGui's own hover-based detector is used as an
// independent cross-check.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "SM64App.hpp"
#include "SM64Presets.hpp"
#include "SM64Theme.hpp"
#include "test_framework.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <map>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Test-engine hooks
// ---------------------------------------------------------------------------
namespace {
std::map<ImGuiID, int> gIdCounts;
std::map<ImGuiID, std::string> gLabels;
bool gCollect = false;
}  // namespace

void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect&, const ImGuiLastItemData* item) {
    if (!gCollect || item == nullptr || id == 0) return;
    if ((item->ItemFlags & ImGuiItemFlags_AllowDuplicateId) != 0) return;
    ++gIdCounts[id];
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
    if (gCollect && label != nullptr) gLabels.emplace(id, label);
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

namespace {

using namespace sm64ui;

class NullPlatform final : public Platform {
public:
    std::optional<std::filesystem::path> openFileDialog(const std::string&) override { return std::nullopt; }
    std::optional<std::filesystem::path> saveFileDialog(const std::string&, const std::filesystem::path&,
                                                        const std::vector<std::string>&) override {
        return std::nullopt;
    }
};

struct Harness {
    ImGuiContext* ctx{nullptr};
    explicit Harness(bool dark = true, float scale = 1.0F) {
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.DisplaySize = ImVec2(1280.0F, 720.0F);
        io.DeltaTime = 1.0F / 60.0F;
        configureFonts(scale);
        unsigned char* pixels = nullptr;
        int w = 0;
        int h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        applyTheme(dark, 0, scale);
        ctx->TestEngineHookItems = true;
    }
    ~Harness() { ImGui::DestroyContext(ctx); }
    Harness(const Harness&) = delete;
    Harness& operator=(const Harness&) = delete;

    void frame(App& app, float width, float height) const {
        ImGui::GetIO().DisplaySize = ImVec2(width, height);
        ImGui::NewFrame();
        app.render();
        ImGui::Render();
    }
};

// Counts widgets that share an ID during the last frame.
//
// Two refinements, both established by experiment (see the controls below):
//  * InputTextMultiline legitimately submits its own ID twice (its inner child window and
//    its text item share one ID), so an ID that belongs to an active child window and was
//    seen at most twice is the multiline pattern, not a conflict.
//  * Two BeginChild() calls with the same name only submit ONE item but reuse one window
//    (BeginCount == 2), so duplicate child names are detected through BeginCount.
struct Audit {
    std::size_t items{0};
    std::size_t duplicates{0};
    std::string firstDuplicate;
};

template <typename Fn>
Audit auditFrame(const Harness& h, Fn&& body) {
    (void)h;
    gIdCounts.clear();
    gLabels.clear();
    gCollect = true;
    body(h);
    gCollect = false;

    std::set<ImGuiID> childIds;
    Audit a;
    for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
        if ((window->Flags & ImGuiWindowFlags_ChildWindow) == 0 || !window->Active) continue;
        childIds.insert(window->ChildId);
        if (window->BeginCount > 1) {
            a.duplicates += static_cast<std::size_t>(window->BeginCount - 1);
            if (a.firstDuplicate.empty()) a.firstDuplicate = std::string("child window '") + window->Name + "' opened twice";
        }
    }

    a.items = gIdCounts.size();
    for (const auto& [id, count] : gIdCounts) {
        if (count <= 1) continue;
        if (count == 2 && childIds.count(id) != 0U) continue;  // InputTextMultiline pattern
        a.duplicates += static_cast<std::size_t>(count - 1);
        if (a.firstDuplicate.empty()) {
            const auto it = gLabels.find(id);
            a.firstDuplicate = it != gLabels.end() ? it->second : "(unlabelled)";
        }
    }
    return a;
}

sm64::Core makeBusyCore() {
    sm64::Core core;
    std::string error;
    sm64::PresetRequest request;
    request.id = sm64::PresetId::Complete120Stars;
    request.slot = 0;
    (void)sm64::applyPreset(core, request, error);
    request.id = sm64::PresetId::RandomChallenge;
    request.slot = 1;
    request.seed = 7;
    (void)sm64::applyPreset(core, request, error);
    core.setMirrored(2, 5, 0xAB);                       // slot C: both copies invalid (raw edit)
    core.set(sm64::B[3] + 20, 0x55);                    // slot D: backup invalid -> recoverable
    core.set(sm64::MenuPrimaryOffset + 2, 0x11);        // menu primary invalid
    return core;
}

struct Scenario {
    const char* name;
    Page page;
    bool expert;
    float width;
    float height;
};

}  // namespace

// ===========================================================================
// Negative control: the detector must catch the v1.0 bug and accept the fix.
// ===========================================================================
TEST("ID audit catches the historical bug: repeated '##Star' checkboxes") {
    Harness h;
    const Audit bad = auditFrame(h, [](const Harness&) {
        ImGui::NewFrame();
        ImGui::Begin("control");
        bool value = false;
        for (int star = 0; star < 7; ++star) {
            ImGui::Checkbox("##Star", &value);
        }
        ImGui::End();
        ImGui::Render();
    });
    CHECK_EQ(bad.duplicates, std::size_t{6});  // 7 widgets, one ID
    CHECK(bad.firstDuplicate.find("Star") != std::string::npos || bad.firstDuplicate == "(unlabelled)");
}

TEST("ID audit accepts the fix: nested PushID(slot/course/star)") {
    Harness h;
    const Audit good = auditFrame(h, [](const Harness&) {
        ImGui::NewFrame();
        ImGui::Begin("control");
        bool value = false;
        for (int slot = 0; slot < 4; ++slot) {
            ImGui::PushID(slot);
            for (int course = 0; course < 15; ++course) {
                ImGui::PushID(course);
                for (int star = 0; star < 7; ++star) {
                    ImGui::PushID(star);
                    ImGui::Checkbox("##Collected", &value);
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::End();
        ImGui::Render();
    });
    CHECK_EQ(good.duplicates, std::size_t{0});
    CHECK(good.items >= 420);  // 4 x 15 x 7 distinct widgets were actually audited
}

TEST("ID audit catches two child windows with the same name") {
    Harness h;
    const Audit bad = auditFrame(h, [](const Harness&) {
        ImGui::NewFrame();
        ImGui::Begin("control");
        for (int i = 0; i < 2; ++i) {
            ImGui::BeginChild("##card", ImVec2(50.0F, 20.0F));  // missing PushID(i): same window twice
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::Render();
    });
    CHECK(bad.duplicates >= 1U);

    const Audit good = auditFrame(h, [](const Harness&) {
        ImGui::NewFrame();
        ImGui::Begin("control");
        for (int i = 0; i < 2; ++i) {
            ImGui::PushID(i);
            ImGui::BeginChild("##card", ImVec2(50.0F, 20.0F));
            ImGui::EndChild();
            ImGui::PopID();
        }
        ImGui::End();
        ImGui::Render();
    });
    CHECK_EQ(good.duplicates, std::size_t{0});
}

TEST("ID audit does not flag InputTextMultiline's internal ID reuse") {
    Harness h;
    static char buffer[64] = "text";
    const Audit a = auditFrame(h, [](const Harness&) {
        ImGui::NewFrame();
        ImGui::Begin("control");
        ImGui::InputTextMultiline("##box", buffer, sizeof(buffer), ImVec2(200.0F, 60.0F), ImGuiInputTextFlags_ReadOnly);
        ImGui::InputText("##plain", buffer, sizeof(buffer));
        ImGui::End();
        ImGui::Render();
    });
    CHECK_EQ(a.duplicates, std::size_t{0});

    // ...but two multiline boxes with the same label ARE a real conflict.
    const Audit twice = auditFrame(h, [](const Harness&) {
        ImGui::NewFrame();
        ImGui::Begin("control");
        ImGui::InputTextMultiline("##box", buffer, sizeof(buffer), ImVec2(200.0F, 60.0F), ImGuiInputTextFlags_ReadOnly);
        ImGui::InputTextMultiline("##box", buffer, sizeof(buffer), ImVec2(200.0F, 60.0F), ImGuiInputTextFlags_ReadOnly);
        ImGui::End();
        ImGui::Render();
    });
    CHECK(twice.duplicates >= 1U);
}

// ===========================================================================
// Real application
// ===========================================================================
TEST("no duplicate widget IDs on any page, in either mode, at several window sizes") {
    const float sizes[][2] = {{800.0F, 600.0F}, {1024.0F, 768.0F}, {1280.0F, 720.0F}, {1920.0F, 1080.0F}, {2560.0F, 1440.0F}};
    for (const bool dark : {true, false}) {
        Harness h(dark);
        for (const bool expert : {false, true}) {
            for (std::size_t p = 0; p < kPageCount; ++p) {
                const auto page = static_cast<Page>(p);
                if (!expert && (page == Page::RawData || page == Page::GameShark)) continue;
                for (const auto& size : sizes) {
                    sm64::Core core = makeBusyCore();
                    NullPlatform platform;
                    sm64::Settings settings;
                    settings.tutorialSeen = true;
                    settings.expert = expert;
                    App app(core, platform, settings, {});
                    app.setPage(page);
                    for (int slot = 0; slot < 4; ++slot) {
                        app.setSlot(slot);
                        h.frame(app, size[0], size[1]);  // warm-up so layout settles
                        const Audit a = auditFrame(h, [&](const Harness& hh) { hh.frame(app, size[0], size[1]); });
                        if (a.duplicates != 0U) {
                            std::fprintf(stderr, "    duplicate IDs on page %zu (%s, %s, %.0fx%.0f, slot %d): %zu, e.g. '%s'\n", p,
                                         expert ? "expert" : "beginner", dark ? "dark" : "light", static_cast<double>(size[0]),
                                         static_cast<double>(size[1]), slot, a.duplicates, a.firstDuplicate.c_str());
                        }
                        CHECK_EQ(a.duplicates, std::size_t{0});
                    }
                }
            }
        }
    }
}

TEST("the audit is not vacuous: key pages really submit many widgets") {
    Harness h;
    sm64::Core core = makeBusyCore();
    NullPlatform platform;
    sm64::Settings settings;
    settings.tutorialSeen = true;
    settings.expert = true;
    App app(core, platform, settings, {});

    app.setPage(Page::Slots);
    h.frame(app, 1280.0F, 720.0F);
    const Audit slots = auditFrame(h, [&](const Harness& hh) { hh.frame(app, 1280.0F, 720.0F); });
    CHECK(slots.items >= 15U * 10U);  // 15 courses x (coins + 7 stars + All + Clear)

    app.setPage(Page::RawData);
    h.frame(app, 1280.0F, 720.0F);
    const Audit raw = auditFrame(h, [&](const Harness& hh) { hh.frame(app, 1280.0F, 720.0F); });
    CHECK(raw.items >= 100U);  // the clipper submits a window of the 512 bytes
}

TEST("no duplicate IDs inside any dialog") {
    const Dialog dialogs[] = {Dialog::Unsaved, Dialog::NewSave, Dialog::Preset, Dialog::Overwrite, Dialog::Notice,
                              Dialog::Welcome, Dialog::Shortcuts, Dialog::History, Dialog::Palette, Dialog::RepairPreview,
                              Dialog::Confirm};
    for (const Dialog dialog : dialogs) {
        for (const auto& size : {std::pair<float, float>{800.0F, 600.0F}, std::pair<float, float>{1920.0F, 1080.0F}}) {
            Harness h;
            sm64::Core core = makeBusyCore();
            core.setFlag(0, sm64::save_flag::HaveWingCap, false);  // give History / Unsaved something to show
            NullPlatform platform;
            sm64::Settings settings;
            settings.tutorialSeen = true;
            App app(core, platform, settings, {});
            if (dialog == Dialog::Preset) {
                // Open via the public path used by menus: any slot preset.
                app.setSlot(1);
            }
            app.openDialog(dialog);
            for (int i = 0; i < 3; ++i) h.frame(app, size.first, size.second);
            const Audit a = auditFrame(h, [&](const Harness& hh) { hh.frame(app, size.first, size.second); });
            if (a.duplicates != 0U) {
                std::fprintf(stderr, "    duplicate IDs in dialog %d: %zu, e.g. '%s'\n", static_cast<int>(dialog), a.duplicates,
                             a.firstDuplicate.c_str());
            }
            CHECK_EQ(a.duplicates, std::size_t{0});
        }
    }
}

TEST("every dialog stays inside the usable display (no off-screen popups)") {
    const Dialog dialogs[] = {Dialog::Unsaved, Dialog::NewSave, Dialog::Notice, Dialog::Welcome,
                              Dialog::Shortcuts, Dialog::History, Dialog::Palette, Dialog::RepairPreview};
    for (const Dialog dialog : dialogs) {
        Harness h;
        sm64::Core core = makeBusyCore();
        core.setFlag(0, sm64::save_flag::HaveWingCap, false);
        NullPlatform platform;
        sm64::Settings settings;
        settings.tutorialSeen = true;
        App app(core, platform, settings, {});
        app.openDialog(dialog);
        for (int i = 0; i < 4; ++i) h.frame(app, 800.0F, 600.0F);
        bool sawPopup = false;
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
            if ((window->Flags & ImGuiWindowFlags_Popup) == 0 || !window->WasActive) continue;
            sawPopup = true;
            CHECK(window->Pos.x >= -1.0F);
            CHECK(window->Pos.y >= -1.0F);
            CHECK(window->Pos.x + window->Size.x <= 800.0F + 1.0F);
            CHECK(window->Pos.y + window->Size.y <= 600.0F + 1.0F);
        }
        CHECK(sawPopup);
    }
}

TEST("UI scales 1.0-2.0 and both themes render every page without ImGui errors") {
    for (const float scale : {1.0F, 1.25F, 1.5F, 1.75F, 2.0F}) {
        for (const bool dark : {true, false}) {
            Harness h(dark, scale);
            sm64::Core core = makeBusyCore();
            NullPlatform platform;
            sm64::Settings settings;
            settings.tutorialSeen = true;
            settings.expert = true;
            App app(core, platform, settings, {});
            for (std::size_t p = 0; p < kPageCount; ++p) {
                app.setPage(static_cast<Page>(p));
                for (int i = 0; i < 3; ++i) h.frame(app, 800.0F, 600.0F);
                const Audit a = auditFrame(h, [&](const Harness& hh) { hh.frame(app, 800.0F, 600.0F); });
                CHECK_EQ(a.duplicates, std::size_t{0});
            }
        }
    }
}

// ===========================================================================
// Cross-check with ImGui's own hover-based conflict detector.
// ===========================================================================
TEST("ImGui's built-in conflict detector never fires while sweeping the slot and dashboard pages") {
    Harness h;
    ImGui::GetIO().ConfigDebugHighlightIdConflicts = true;
    for (const Page page : {Page::Slots, Page::Dashboard, Page::SecretStars, Page::Castle}) {
        sm64::Core core = makeBusyCore();
        NullPlatform platform;
        sm64::Settings settings;
        settings.tutorialSeen = true;
        App app(core, platform, settings, {});
        app.setPage(page);
        const float w = 1100.0F;
        const float hgt = 1500.0F;  // tall display so the 15-course table is fully visible
        h.frame(app, w, hgt);
        bool conflict = false;
        for (float y = 0.0F; y < hgt && !conflict; y += 14.0F) {
            for (float x = 0.0F; x < w; x += 14.0F) {
                ImGui::GetIO().AddMousePosEvent(x, y);
                ImGui::GetIO().DisplaySize = ImVec2(w, hgt);
                ImGui::NewFrame();
                if (ImGui::GetCurrentContext()->DebugDrawIdConflicts != 0) conflict = true;
                app.render();
                ImGui::Render();
                if (conflict) break;
            }
        }
        CHECK(!conflict);
    }
}
