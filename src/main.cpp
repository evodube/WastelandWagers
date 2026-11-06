#include "Audio.h"
#include "CameraHook.h"
#include "Game.h"
#include "Hook.h"
#include "Hud.h"
#include "Settings.h"

namespace
{
	constexpr auto kName = "fo4roulette";

	void OnMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case F4SE::MessagingInterface::kPostPostLoad:
			fr::Hud::Install();  // F4SE Menu Framework is loaded by now, if it's installed
			break;
		case F4SE::MessagingInterface::kGameDataReady:
			fr::log::info("game data ready");
			fr::Settings::Get().Load();
			fr::Hook::Install();
			fr::CameraHook::Install();
			fr::Audio::Init();
			fr::Game::Listen();
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
			fr::Hook::Install();  // no-op if already hooked
			fr::CameraHook::Install();
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

	fr::log::info("fo4roulette loading on runtime {} ({})", a_f4se->RuntimeVersion().string(),
		REX::FModule::IsRuntimeOG() ? "OG" : REX::FModule::IsRuntimeNG() ? "NG" : "AE");

	auto* messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		fr::log::error("could not register F4SE message listener");
		return false;
	}

	if (auto* serialization = F4SE::GetSerializationInterface()) {
		serialization->SetUniqueID(0x46524C54);  // 'FRLT'
		serialization->SetSaveCallback(fr::Game::Save);
		serialization->SetLoadCallback(fr::Game::Load);
		serialization->SetRevertCallback(fr::Game::Revert);
	} else {
		fr::log::warn("no F4SE serialization: dealers and the settlers' caps won't be saved");
	}

	fr::log::info("loaded; waiting for game data");
	return true;
}
