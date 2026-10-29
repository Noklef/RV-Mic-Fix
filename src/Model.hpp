#pragma once
#include "Devices.hpp"

namespace rv
{
// Plain values produced by the voice pass for display; no Unreal pointers.
struct PlayerInfo
{
    std::wstring name;
    double distance = -1; // metres, negative when unknown
    bool pawn = false;
    bool talker = false;
    bool speaking = false;
    bool positional = false; // playing with attenuation, attached to this player
    std::wstring detail;
};

struct VoiceStatus
{
    bool inSession = false;
    bool host = false;
    std::vector<PlayerInfo> players; // remote players only
    unsigned talkers = 0;
    unsigned staleTalkers = 0;
    unsigned synths = 0;
    unsigned globalVoices = 0; // playing without attenuation
    unsigned registered = 0;
    unsigned restarted = 0;
    double scanMs = 0;
};
} // namespace rv
