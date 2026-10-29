#include "../src/Overlay.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
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

static std::vector<const rv::Item*> items(const std::vector<rv::Row>& rows)
{
    std::vector<const rv::Item*> all;
    for (auto& row : rows)
    {
        for (auto& item : row)
        {
            all.push_back(&item);
        }
    }
    return all;
}

static bool hasText(const std::vector<rv::Row>& rows, std::wstring_view s)
{
    for (auto item : items(rows))
    {
        if (item->text.find(s) != std::wstring::npos)
        {
            return true;
        }
    }
    return false;
}

static const rv::Item& control(const std::vector<rv::Row>& rows, rv::ItemKind kind,
                               rv::UiKind action, std::wstring_view player = {})
{
    for (auto item : items(rows))
    {
        if (item->kind == kind && item->action.kind == action && item->action.player == player)
        {
            return *item;
        }
    }
    throw std::runtime_error("Overlay control is missing");
}

static void overlayChecks(std::vector<rv::Device> devices)
{
    using rv::ItemKind;
    using rv::UiKind;
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
        {L"Alex", L"Alex", 4.25, true, true, true, true, L"talker BP_VOIPTalker_C_1, attach Mesh"},
        {L"Sam", L"Sam", 37.0, true, true, true, false, L"talker none"},
        {L"Jo", L"Jo", -1, false, false, false, false, L""},
    };
    rv::OverlayModel model;
    model.devices = &devices;
    model.activeKnown = devices.size() > 1;
    model.activeDevice = devices.size() > 1 ? devices[1].id : GUID{};
    model.micStatus = L"Click a microphone to switch to it. The choice is saved.";
    model.voice = &voice;
    model.prefs = &prefs;
    model.details = true;

    auto rows = rv::describe(model);
    check(hasText(rows, L"GLOBAL") && hasText(rows, L"POSITIONAL") && hasText(rows, L"NO PAWN"),
          "Voice state chips are missing");
    check(hasText(rows, L"60%") && hasText(rows, L"muted") && hasText(rows, L"you are the host"),
          "Volume or session state is missing");
    check(hasText(rows, L"BP_VOIPTalker_C_1"), "Details are missing while enabled");
    for (auto kind : {UiKind::Close, UiKind::RefreshDevices, UiKind::ToggleAuto, UiKind::RepairNow,
                      UiKind::Diagnose, UiKind::ToggleDetails})
    {
        control(rows, ItemKind::Button, kind);
    }
    check(control(rows, ItemKind::Slider, UiKind::SetVolume, L"Sam").value == 60 &&
              control(rows, ItemKind::Slider, UiKind::SetVolume, L"Alex").value == 100,
          "Sliders must start at each player's saved volume");
    check(control(rows, ItemKind::Button, UiKind::ToggleMute, L"Jo").text == L"Unmute" &&
              control(rows, ItemKind::Button, UiKind::ToggleMute, L"Sam").text == L"Mute",
          "Mute buttons must target their own player");
    size_t deviceButtons = 0;
    for (auto item : items(rows))
    {
        if (item->action.kind == UiKind::SelectDevice)
        {
            check(IsEqualGUID(item->action.device.id, devices[deviceButtons].id),
                  "Device button selects the wrong endpoint");
            check(deviceButtons != 1 || item->tone == rv::Tone::Selected,
                  "The device in use must be highlighted");
            ++deviceButtons;
        }
        check(item->kind != ItemKind::Button || !item->text.empty(), "A button has no label");
    }
    check(deviceButtons == std::min(devices.size(), rv::MaxDevices),
          "Device list must show up to the limit");

    // Text, tone and value changes update widgets in place; structure changes rebuild.
    auto before = rv::shape(rows);
    voice.players[0].distance = 80;
    voice.players[1].positional = true;
    prefs.setPercent(L"Sam", 150);
    prefs.toggleMute(L"Sam");
    prefs.autoFix = false;
    model.status = L"Fix now: re-registered 2 talker(s), restarted 1 voice stream(s).";
    check(rv::shape(rv::describe(model)) == before,
          "A text or value change must not rebuild the widgets");
    model.details = false;
    check(rv::shape(rv::describe(model)) != before, "Toggling details must rebuild");
    model.details = true;
    voice.players.push_back({L"Late joiner", L"Late joiner", 3, true, true, false, false, L""});
    check(rv::shape(rv::describe(model)) != before, "A new player must rebuild");

    auto notice = rv::describeNotice(L"microphone restored");
    check(notice.size() == 1 && notice[0].size() == 1 &&
              notice[0][0].text.find(L"restored") != std::wstring::npos,
          "Notice must be one line of text");

    check(rv::snapVolume(0) == 0 && rv::snapVolume(200) == 200 && rv::snapVolume(250) == 200 &&
              rv::snapVolume(-5) == 0 && rv::snapVolume(47.4) == 45,
          "Slider must clamp to 0-200% in 5% steps");
    check(rv::snapVolume(94) == 95 && rv::snapVolume(102.4) == 100 && rv::snapVolume(108) == 110,
          "Slider must round to the nearest 5% step");
    check(rv::snapVolume(std::numeric_limits<double>::quiet_NaN()) == 100,
          "An invalid slider value must not reach the saved volume");
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

    // Steam ID keys: a name-only line moves to the ID on first sight and follows renames.
    rv::Prefs steam;
    steam.parse(L"player 85 1 NutLord\nplayer 40 0 steam:123456\nsteam 70 0 12x45 Bad\n"
                L"steam 70 0 123 Short\n");
    check(steam.managed(L"NutLord") && !steam.managed(L"steam:123456") &&
              !steam.managed(L"steam:12x45") && !steam.managed(L"steam:123"),
          "Malformed Steam ID lines must be ignored");
    check(steam.identify(L"", L"Guest") == L"Guest" && !steam.changed,
          "A player without an ID must be keyed by name");
    auto key = steam.identify(L"76561198107107274", L"NutLord");
    check(key == L"steam:76561198107107274" && steam.changed && !steam.managed(L"NutLord") &&
              steam.player(key).percent == 85 && steam.player(key).muted,
          "A name-keyed player must move to their Steam ID");
    check(steam.serialize().find(L"steam 85 1 76561198107107274 NutLord\n") != std::wstring::npos,
          "Steam ID line has the wrong format");
    steam.changed = false;
    steam.identify(L"76561198107107274", L"Nut Lord");
    check(steam.changed && steam.label(key) == L"Nut Lord" && steam.player(key).percent == 85,
          "A renamed player must keep their settings");
    auto fresh = steam.identify(L"76561198000000001", L"New Player");
    steam.setPercent(fresh, 120);
    check(steam.label(fresh) == L"New Player" &&
              steam.serialize().find(L"steam 120 0 76561198000000001 New Player\n") !=
                  std::wstring::npos,
          "A new Steam player must be saved with their name");
    check(steam.identify(L"", L"steam:76561198000000001").empty(),
          "A name that looks like a Steam key must get no settings key");
    auto unnamed = steam.identify(L"76561198000000002", L"");
    steam.setPercent(unnamed, 50);
    check(steam.serialize().find(L"steam 50 0 76561198000000002 \n") != std::wstring::npos,
          "An unknown name must not be saved as the Steam key");
    rv::Prefs reread;
    reread.parse(steam.serialize());
    check(reread.serialize() == steam.serialize(), "Steam ID lines did not round trip");
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
