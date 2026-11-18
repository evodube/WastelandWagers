// fo4roulette.dll, retired: the mod's been renamed Wasteland Wagers (WastelandWagers.dll). This stands in for the old
// plugin, so installing the new version over the old one leaves nothing of the old running: F4SE asks it if it'll
// load, and it says no (F4SE logs it as not loaded and lets it go). Nothing to delete by hand.
#include <cstdint>

namespace
{
	struct PluginInfo
	{
		std::uint32_t infoVersion;
		const char*   name;
		std::uint32_t version;
	};
}

extern "C" __declspec(dllexport) bool F4SEPlugin_Query(const void*, PluginInfo* a_info)
{
	if (a_info) {
		a_info->infoVersion = 1;
		a_info->name = "fo4roulette (retired: now WastelandWagers)";
		a_info->version = 1;
	}
	return false;
}

extern "C" __declspec(dllexport) bool F4SEPlugin_Load(const void*)
{
	return false;
}
