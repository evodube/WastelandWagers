#include "Settings.h"

#include "SettingsMenu.h"

namespace fr
{
	namespace
	{
		constexpr auto kIniPath = L"Data\\F4SE\\Plugins\\fo4roulette.ini";

		std::wstring ReadString(const wchar_t* a_section, const wchar_t* a_key, const std::wstring& a_default)
		{
			wchar_t buf[512]{};
			::GetPrivateProfileStringW(a_section, a_key, a_default.c_str(), buf, static_cast<DWORD>(std::size(buf)), kIniPath);
			std::wstring value = buf;
			for (std::size_t i = 0; i < value.size(); ++i) {
				if (value[i] == L';' && (i == 0 || value[i - 1] == L' ' || value[i - 1] == L'\t')) {
					value.resize(i);
					break;
				}
			}
			while (!value.empty() && (value.back() == L' ' || value.back() == L'\t')) {
				value.pop_back();
			}
			return value;
		}

		std::string Narrow(const std::wstring& a_str)
		{
			std::string out;
			for (wchar_t c : a_str) {
				out.push_back(c < 128 ? static_cast<char>(c) : '?');
			}
			return out;
		}

		long long ReadInt(const wchar_t* a_section, const wchar_t* a_key, long long a_default)
		{
			const auto str = ReadString(a_section, a_key, std::to_wstring(a_default));
			try {
				return std::stoll(str, nullptr, 0);
			} catch (...) {
				return a_default;
			}
		}

		float ReadFloat(const wchar_t* a_section, const wchar_t* a_key, float a_default)
		{
			const auto str = ReadString(a_section, a_key, std::to_wstring(a_default));
			try {
				return std::stof(str);
			} catch (...) {
				return a_default;
			}
		}

		bool ReadBool(const wchar_t* a_section, const wchar_t* a_key, bool a_default)
		{
			return ReadInt(a_section, a_key, a_default ? 1 : 0) != 0;
		}
	}

	void Settings::Load()
	{
		auto key = [](const wchar_t* a_name, int a_default) { return static_cast<int>(ReadInt(L"Keys", a_name, a_default)); };
		keySpin = key(L"iSpin", keySpin);
		keyLeave = key(L"iLeave", keyLeave);
		keyView = key(L"iView", keyView);
		keyRebet = key(L"iRebet", keyRebet);
		keyClear = key(L"iClear", keyClear);
		keyPackUp = key(L"iPackUp", keyPackUp);
		keyChip[0] = key(L"iChip1", keyChip[0]);
		keyChip[1] = key(L"iChip5", keyChip[1]);
		keyChip[2] = key(L"iChip25", keyChip[2]);
		keyChip[3] = key(L"iChip100", keyChip[3]);

		wheel = ReadInt(L"Table", L"iWheel", wheel) == 1 ? 1 : 0;
		spinDelay = std::clamp(ReadFloat(L"Table", L"fSpinDelay", spinDelay), 3.0f, 300.0f);
		maxBet = std::clamp(static_cast<int>(ReadInt(L"Table", L"iMaxBet", maxBet)), 1, 100000);
		activeDistance = std::clamp(ReadFloat(L"Table", L"fActiveDistance", activeDistance), 500.0f, 20000.0f);

		layoutHeight = std::clamp(ReadFloat(L"Camera", L"fLayoutHeight", layoutHeight), 40.0f, 600.0f);
		wheelHeight = std::clamp(ReadFloat(L"Camera", L"fWheelHeight", wheelHeight), 25.0f, 400.0f);
		wheelViewOnSpin = ReadBool(L"Camera", L"bWheelViewOnSpin", wheelViewOnSpin);
		cursorSpeed = std::clamp(ReadFloat(L"Camera", L"fCursorSpeed", cursorSpeed), 0.005f, 1.0f);

		bettors = ReadBool(L"Bettors", L"bEnabled", bettors);
		maxBettors = std::clamp(static_cast<int>(ReadInt(L"Bettors", L"iMax", maxBettors)), 0, 4);  // (Game::kSpots places to stand)
		bettorIdles = ReadBool(L"Bettors", L"bIdles", bettorIdles);
		pkSmallBlind = std::clamp(static_cast<int>(ReadInt(L"Poker", L"iSmallBlind", pkSmallBlind)), 1, 1000);
		pkBigBlind = std::clamp(static_cast<int>(ReadInt(L"Poker", L"iBigBlind", pkBigBlind)), pkSmallBlind, 2000);
		pkBuyIn = std::clamp(static_cast<int>(ReadInt(L"Poker", L"iBuyIn", pkBuyIn)), pkBigBlind * 2, 100000);
		pkPlayers = ReadBool(L"Poker", L"bSettlers", pkPlayers);
		pkMaxPlayers = std::clamp(static_cast<int>(ReadInt(L"Poker", L"iMaxSettlers", pkMaxPlayers)), 0, 6);
		pkNpcBuyIn = std::clamp(static_cast<int>(ReadInt(L"Poker", L"iSettlerBuyIn", pkNpcBuyIn)), pkBigBlind * 2, 100000);
		pkThink = std::clamp(ReadFloat(L"Poker", L"fThinkTime", pkThink), 0.2f, 5.0f);
		pkTopDown = ReadBool(L"Poker", L"bTopDown", pkTopDown);
		pkTopHeight = std::clamp(ReadFloat(L"Poker", L"fTopHeight", pkTopHeight), 40.0f, 400.0f);
		bettorEvery = std::clamp(ReadFloat(L"Bettors", L"fEvery", bettorEvery), 2.0f, 600.0f);
		bettorRange = std::clamp(ReadFloat(L"Bettors", L"fRange", bettorRange), 200.0f, 6000.0f);
		bettorMinRounds = std::clamp(static_cast<int>(ReadInt(L"Bettors", L"iMinRounds", bettorMinRounds)), 1, 50);
		bettorMaxRounds = std::clamp(static_cast<int>(ReadInt(L"Bettors", L"iMaxRounds", bettorMaxRounds)), bettorMinRounds, 100);

		hudScale = std::clamp(ReadFloat(L"HUD", L"fScale", hudScale), 0.3f, 3.0f);
		notifications = ReadBool(L"HUD", L"bNotifications", notifications);
		sound = ReadBool(L"Sound", L"bEnabled", sound);
		soundVolume = std::clamp(ReadFloat(L"Sound", L"fVolume", soundVolume), 0.0f, 1.0f);

		plugin = Narrow(ReadString(L"Forms", L"sPlugin", std::wstring(plugin.begin(), plugin.end())));

		log::info("settings: {} wheel, max bet {}, bettors {} (up to {}), spins {:.0f}s after a bet", wheel ? "American" : "European", maxBet,
			bettors ? "on" : "off", maxBettors, spinDelay);
	}

	std::string Settings::Key(int a_vk)
	{
		return SettingsMenu::KeyName(a_vk);
	}
}
