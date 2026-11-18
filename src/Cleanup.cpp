#include "Cleanup.h"

namespace fr::cleanup
{
	namespace
	{
		bool g_oldWasRunning = false;

		void Remove(const std::filesystem::path& a_path)
		{
			std::error_code ec;
			if (!std::filesystem::exists(a_path, ec)) {
				return;
			}
			const auto count = std::filesystem::remove_all(a_path, ec);
			if (ec) {
				log::warn("old files: {} not removed ({}): again next time", a_path.string(), ec.message());
			} else {
				log::info("old files: {} removed ({} file{})", a_path.string(), count, count == 1 ? "" : "s");
			}
		}

		void RemovePlugin(const std::filesystem::path& a_dll)
		{
			// a .dll still loaded can't be deleted, but it can be renamed: renamed now, deleted next time
			std::error_code ec;
			auto renamed = a_dll;
			renamed += L".old";
			std::filesystem::remove(renamed, ec);
			if (!std::filesystem::exists(a_dll, ec)) {
				return;
			}
			// the old plugin itself loaded and running (the stand-in 1.0.0 shipped says no to F4SE, and has no
			// F4SEPlugin_Version)
			if (auto* old = ::GetModuleHandleW(L"fo4roulette.dll"); old && ::GetProcAddress(old, "F4SEPlugin_Version")) {
				g_oldWasRunning = true;
			}
			if (std::filesystem::remove(a_dll, ec)) {
				log::info("old files: {} removed", a_dll.string());
				return;
			}
			std::filesystem::rename(a_dll, renamed, ec);
			if (ec) {
				log::warn("old files: {} not removed or renamed ({})", a_dll.string(), ec.message());
			} else {
				log::info("old files: {} still loaded: renamed {} (removed next time)", a_dll.string(), renamed.filename().string());
			}
		}
	}

	void RemoveOldFiles(bool a_dataLoaded)
	{
		const std::filesystem::path data = L"Data";
		const auto plugins = data / L"F4SE" / L"Plugins";
		RemovePlugin(plugins / L"fo4roulette.dll");
		for (const auto* name : { L"fo4roulette.pdb", L"fo4roulette.ini", L"fo4roulette-Credits.txt", L"fo4roulette-LICENSE.txt" }) {
			Remove(plugins / name);
		}
		// your settings: carried over to WastelandWagers_user.ini first (Settings::Load), then the old file goes
		std::error_code ec;
		if (std::filesystem::exists(plugins / L"WastelandWagers_user.ini", ec)) {
			Remove(plugins / L"fo4roulette_user.ini");
		}
		for (const auto& dir : { plugins / L"fo4roulette", data / L"Meshes" / L"fo4roulette", data / L"Textures" / L"fo4roulette",
				 data / L"Materials" / L"fo4roulette", data / L"Sound" / L"fo4roulette",
				 data / L"Meshes" / L"Actors" / L"Character" / L"Animations" / L"fo4roulette" }) {
			Remove(dir);
		}
		for (const auto* name : { L"fo4rouletteQuest", L"fo4rouletteGroupAlias", L"fo4rouletteNative" }) {
			Remove(data / L"Scripts" / (std::wstring(name) + L".pex"));
			Remove(data / L"Scripts" / L"Source" / L"User" / (std::wstring(name) + L".psc"));
		}
		if (a_dataLoaded) {
			Remove(data / L"fo4roulette.esl");  // (the game's read it by now)
		}
	}

	bool OldWasRunning()
	{
		return g_oldWasRunning;
	}
}
