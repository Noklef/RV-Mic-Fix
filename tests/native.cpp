#include "../src/Overlay.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

static unsigned checks = 0;

static void check(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
    ++checks;
}

// Records draw calls with a fixed-width font, like the engine's HUD metrics.
class Recorder final : public rv::Painter
{
public:
    struct Op
    {
        bool text;
        float x, y, w, h;
        std::wstring s;
    };

    std::vector<Op> ops;

    void rect(float x, float y, float w, float h, rv::Color) override
    {
        ops.push_back({false, x, y, w, h, {}});
    }

    void text(std::wstring_view s, float x, float y, rv::Color, float scale) override
    {
        ops.push_back({true, x, y, float(s.size()) * charWidth() * scale, lineHeight() * scale,
                       std::wstring(s)});
    }

    float lineHeight() override
    {
        return 24;
    }

    float charWidth() override
    {
        return 11;
    }

    bool hasText(std::wstring_view s) const
    {
        for (auto& op : ops)
        {
            if (op.text && op.s.find(s) != std::wstring::npos)
            {
                return true;
            }
        }
        return false;
    }
};

static const rv::Hit& hit(const rv::Overlay& overlay, std::wstring_view id)
{
    for (auto& h : overlay.hits())
    {
        if (h.id == id)
        {
            return h;
        }
    }
    throw std::runtime_error("Overlay control is missing");
}

static rv::Pointer centre(const rv::Hit& h, bool down)
{
    return {h.x + h.w / 2, h.y + h.h / 2, down, true};
}

static void overlayChecks(std::vector<rv::Device> devices)
{
    rv::Prefs prefs;
    prefs.microphone = devices.size() > 1 ? devices[1].name : L"";
    prefs.setPercent(L"Sam", 60);
    prefs.toggleMute(L"Jo");
    rv::VoiceStatus voice;
    voice.inSession = true;
    voice.host = true;
    voice.talkers = 3;
    voice.synths = 3;
    voice.globalVoices = 1;
    voice.players = {
        {L"Alex", 4.25, true, true, true, true, L"talker BP_VOIPTalker_C_1, attach Mesh"},
        {L"Sam", 37.0, true, true, true, false, L"talker none"},
        {L"Jo", -1, false, false, false, false, L""},
    };
    rv::OverlayModel model;
    model.devices = &devices;
    model.activeKnown = devices.size() > 1;
    model.activeDevice = devices.size() > 1 ? devices[1].id : GUID{};
    model.micStatus = L"Click a microphone to switch to it. The choice is saved.";
    model.voice = &voice;
    model.prefs = &prefs;
    model.details = true;
    model.status = L"Fix now: re-registered 2 talker(s), restarted 1 voice stream(s).";

    rv::Overlay overlay;
    Recorder r;
    auto actions = overlay.frame(r, 1920, 1080, {}, model);
    check(actions.empty(), "An idle frame produced actions");
    check(!r.ops.empty() && !r.ops[0].text && r.ops[0].w > 600,
          "Panel background must be drawn first, beneath its contents");
    auto& panel = r.ops[0];
    for (auto& op : r.ops)
    {
        check(op.x >= panel.x - 0.5f && op.y >= panel.y - 0.5f &&
                  op.x + op.w <= panel.x + panel.w + 0.5f &&
                  op.y + op.h <= panel.y + panel.h + 0.5f,
              "An overlay element is drawn outside the panel");
    }
    check(r.hasText(L"GLOBAL") && r.hasText(L"POSITIONAL") && r.hasText(L"NO PAWN"),
          "Voice state chips are missing");
    check(r.hasText(L"60%") && r.hasText(L"muted") && r.hasText(L"you are the host"),
          "Volume or session state is missing");
    check(r.hasText(L"BP_VOIPTalker_C_1"), "Details are missing while enabled");
    for (auto id : {L"close", L"refresh", L"auto", L"repair", L"log", L"details", L"volume:Alex",
                    L"mute:Sam", L"device:0"})
    {
        hit(overlay, id);
    }

    // A press fires once; holding the button does not repeat it.
    auto& mute = hit(overlay, L"mute:Sam");
    actions = overlay.frame(r, 1920, 1080, centre(mute, true), model);
    check(actions.size() == 1 && actions[0].kind == rv::UiKind::ToggleMute &&
              actions[0].player == L"Sam",
          "Mute click did not target its player");
    check(overlay.frame(r, 1920, 1080, centre(hit(overlay, L"mute:Sam"), true), model).empty(),
          "A held button repeated its action");
    overlay.frame(r, 1920, 1080, {}, model);

    if (devices.size() > 1)
    {
        auto& last = hit(
            overlay,
            L"device:" + std::to_wstring(std::min(devices.size(), rv::Overlay::MaxDevices) - 1));
        actions = overlay.frame(r, 1920, 1080, centre(last, true), model);
        check(actions.size() == 1 && actions[0].kind == rv::UiKind::SelectDevice &&
                  IsEqualGUID(actions[0].device.id,
                              devices[std::min(devices.size(), rv::Overlay::MaxDevices) - 1].id),
              "Device row selected the wrong endpoint");
        overlay.frame(r, 1920, 1080, {}, model);
    }

    // Slider: drag from the left end to the right end, through the middle.
    auto slider = hit(overlay, L"volume:Alex");
    float left = slider.x + slider.w * 0.02f;
    float y = slider.y + slider.h / 2;
    actions = overlay.frame(r, 1920, 1080, {left, y, true, true}, model);
    check(actions.size() == 1 && actions[0].kind == rv::UiKind::SetVolume && actions[0].value == 0,
          "Slider press at the left end must set 0%");
    actions = overlay.frame(r, 1920, 1080, {slider.x + slider.w, y + 200, true, true}, model);
    check(actions.size() == 1 && actions[0].value == rv::Prefs::MaxPercent,
          "Slider drag must continue outside its row and clamp at 200%");
    check(overlay.frame(r, 1920, 1080, {slider.x + slider.w, y + 200, true, true}, model).empty(),
          "An unchanged slider value was emitted again");
    // Actions apply on the next tick, so the saved value can lag; compare to
    // the last emitted value, not the saved one. Near 100% snaps to 100%.
    actions = overlay.frame(r, 1920, 1080, {slider.x + slider.w * 0.52f, y, true, true}, model);
    check(actions.size() == 1 && actions[0].value == 100,
          "Dragging back to 100% before the save applied was lost");
    overlay.frame(r, 1920, 1080, {}, model);
    actions = overlay.frame(r, 1920, 1080, {slider.x + slider.w * 0.52f, y, true, true}, model);
    check(actions.empty(), "Pressing on the current value must not emit a change");
    overlay.frame(r, 1920, 1080, {slider.x + slider.w * 0.52f, y, false, true}, model);
    check(overlay.frame(r, 1920, 1080, {slider.x + slider.w, y, false, true}, model).empty(),
          "Moving without a press must not change volume");

    auto close = hit(overlay, L"close");
    actions = overlay.frame(r, 1920, 1080, centre(close, true), model);
    check(actions.size() == 1 && actions[0].kind == rv::UiKind::Close, "Close button failed");

    // Small windows: the panel stays on screen.
    Recorder narrow;
    rv::Overlay compact;
    compact.frame(narrow, 1280, 720, {}, model);
    check(narrow.ops[0].x >= 0 && narrow.ops[0].x + narrow.ops[0].w <= 1280,
          "Panel overflows a 1280x720 canvas horizontally");
}

static void prefsChecks()
{
    check(rv::fit(L"abcdef", 6) == L"abcdef" && rv::fit(L"abcdef", 5) == L"ab..." &&
              rv::fit(L"abcdef", 2) == L"ab",
          "Text fitting changed");
    rv::Prefs prefs;
    std::wstring tricky = L"Zoë \U0001F690  the driver";
    check(!prefs.managed(tricky) && prefs.gain(tricky) == 1.0f,
          "Unknown players must be left at the game's volume");
    prefs.setPercent(tricky, 250);
    prefs.setPercent(L"quiet one", -5);
    prefs.toggleMute(L"muted person");
    prefs.setPercent(L"muted person", 80);
    prefs.microphone = L"Microphone (USB Audio Device)";
    prefs.autoFix = false;
    check(prefs.player(tricky).percent == 200 && prefs.player(L"quiet one").percent == 0,
          "Volume must clamp to 0-200%");
    check(prefs.gain(L"muted person") == 0.0f && prefs.gain(tricky) == 2.0f,
          "Mute must silence without losing the saved percent");

    rv::Prefs parsed;
    parsed.parse(prefs.serialize() + L"player 50 0\r\nnonsense line\nplayer x 0 bad\nautofix\n");
    check(parsed.serialize() == prefs.serialize(), "Settings text did not round trip");
    check(!parsed.managed(L"bad") && !parsed.managed(L""), "Malformed lines must be ignored");

    auto path = std::filesystem::path(L"prefs-fixture.ini");
    check(prefs.save(path), "Settings save failed");
    check(prefs.save(path), "Settings save must replace an existing file");
    rv::Prefs loaded;
    check(loaded.load(path) && loaded.serialize() == prefs.serialize() &&
              loaded.player(tricky).percent == 200 && !loaded.autoFix &&
              loaded.microphone == prefs.microphone,
          "Settings did not survive a UTF-8 file round trip");
    std::filesystem::remove(path);
    check(!loaded.load(path), "A missing settings file must report not loaded");
}

static void reapplyChecks()
{
    rv::Reapply policy;
    check(!policy.due(true, 0), "A matching capture must not be re-applied");
    check(policy.due(false, 0), "A mismatched capture must be re-applied");
    policy.result({false, true, L"busy"}, 0);
    check(!policy.due(false, 4999) && policy.due(false, 5000),
          "Re-apply must wait between attempts");
    policy.result({false, true, L"busy"}, 5000);
    policy.result({false, false, L"x"}, 10000);
    policy.result({false, false, L"x"}, 15000);
    check(policy.failures == 2, "Busy refusals must not count as failures");
    policy.result({false, false, L"x"}, 20000);
    check(!policy.due(false, 99999), "Re-apply must stop after three real failures");
    check(!policy.due(true, 99999) && policy.due(false, 99999),
          "Seeing the saved device again must reset the failure count");
}

int main()
{
    try
    {
        check(!rv::CaptureControl{}.apply({}).ok,
              "Microphone operation bypassed the executable gate");
        auto info = rv::CaptureControl{}.current();
        check(!info.ok && info.count == 0, "Capture query bypassed the executable gate");
        {
            std::ofstream file("hash-fixture.bin", std::ios::binary);
            file << "abc";
        }
        check(rv::sha256(L"hash-fixture.bin") ==
                  L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA-256 compatibility gate produced an incorrect digest");
        std::filesystem::remove(L"hash-fixture.bin");

        auto devices = rv::enumerateDevices();
        auto count = devices.size();
        devices.insert(devices.begin(), rv::Device{GUID{}, L"Windows default"});
        if (count == 0)
        {
            devices.push_back({{1}, L"Fixture microphone A"});
            devices.push_back({{2},
                               L"Fixture microphone B with a very long endpoint name that "
                               L"must be shortened"});
        }
        overlayChecks(devices);
        prefsChecks();
        reapplyChecks();
        std::cout << "PASS: " << checks << " native checks; " << count
                  << " real capture devices enumerated.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
