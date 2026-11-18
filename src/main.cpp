#include "Cleanup.h"
#include "Audio.h"
#include "CameraHook.h"
#include "Forms.h"
#include "Game.h"
#include "Hook.h"
#include "Hud.h"
#include "MenuWatch.h"
#include "Settings.h"

namespace
{
	constexpr auto kName = "WastelandWagers";

	void OnMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case F4SE::MessagingInterface::kPostPostLoad:
			fr::menus::Watch();  // (the UI may not exist yet: then at game data ready)
			fr::Hud::Install();  // F4SE Menu Framework is loaded by now, if it's installed
			break;
		case F4SE::MessagingInterface::kGameDataReady:
			fr::log::info("game data ready");
			fr::Settings::Get().Load();
			fr::cleanup::RemoveOldFiles(true);  // (after the settings: your old settings carried over first)
			fr::Hook::Install();
			fr::CameraHook::Install();
			fr::Audio::Init();
			fr::menus::Watch();
			fr::Game::Listen();
			fr::Game::Get().SetUpKit();
			fr::Forms::AddWorkshopMenu();
			break;
		case F4SE::MessagingInterface::kPreLoadGame:
			fr::Game::Get().Forget();
			break;
		case F4SE::MessagingInterface::kPreSaveGame:
			fr::Game::Get().BeforeSave();
			break;
		case F4SE::MessagingInterface::kPostSaveGame:
			fr::Game::Get().AfterSave();
			break;
		case F4SE::MessagingInterface::kPostLoadGame:
		case F4SE::MessagingInterface::kNewGame:
			if (static bool told = false; !told && fr::cleanup::OldWasRunning()) {
				told = true;
				RE::SendHUDMessage::ShowHUDMessage("Wasteland Wagers: the old fo4roulette files were removed. Restart the game once.", nullptr, false, false);
			}
			fr::Hook::Install();  // no-op if already hooked
			fr::CameraHook::Install();
			fr::Game::Get().StockKit();
			break;
		default:
			break;
		}
	}
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = kName;
	a_info->version = 1;

	if (a_f4se->IsEditor()) {
		return false;
	}
	return a_f4se->RuntimeVersion() >= F4SE::RUNTIME_1_10_163;
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se, { .log = true, .logName = kName, .logRotate = 3, .trampoline = false });
	fr::cleanup::RemoveOldFiles(false);  // (the mod's old name's files: all but its .esl, which the game's still to read)

	fr::log::info("WastelandWagers loading on runtime {} ({})", a_f4se->RuntimeVersion().string(),
		REX::FModule::IsRuntimeOG() ? "OG" : REX::FModule::IsRuntimeNG() ? "NG" : "AE");

	auto* messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		fr::log::error("could not register F4SE message listener");
		return false;
	}

	if (auto* papyrus = F4SE::GetPapyrusInterface(); !papyrus || !papyrus->Register(fr::RegisterNatives)) {
		fr::log::warn("could not register the quest's natives: settlers' slot machine pulls won't go with their hands");
	}

	if (auto* serialization = F4SE::GetSerializationInterface()) {
		serialization->SetUniqueID(0x46524C54);  // 'FRLT'
		serialization->SetSaveCallback(fr::Game::Save);
		serialization->SetLoadCallback(fr::Game::Load);
		serialization->SetRevertCallback(fr::Game::Revert);
	} else {
		fr::log::warn("no F4SE serialization: the settlers' caps won't be saved");
	}

	fr::log::info("loaded; waiting for game data");
	return true;
}
