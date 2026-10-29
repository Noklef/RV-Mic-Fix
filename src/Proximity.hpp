#pragma once
#include "Common.hpp"
#include "Ue4ssAbi.hpp"
namespace rv {
std::vector<uintptr_t> rosterSignature(RC::Unreal::UEngine* engine);
std::wstring repairProximity(RC::Unreal::UEngine* engine, bool diagnoseOnly);
}
