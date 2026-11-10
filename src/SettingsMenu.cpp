// Every fo4roulette.ini setting on pages in F4SE Menu Framework's menu (Real Roulette > Table, Camera, ...).
//
// One table below drives it: each row is an INI key, the Settings field it sets, a label, a range and a line of
// help. Changes apply straight away and are written to the INI as soon as they're made.

#include "SettingsMenu.h"

#include "F4SEMenuFramework.h"
#include "Settings.h"

namespace fr::SettingsMenu
{
	namespace
	{
		using namespace ImGuiMCP;

		// (what you change here goes to its own file, read before fo4roulette.ini: a new version's INI doesn't undo it)
		constexpr auto kIniPath = L"Data\\F4SE\\Plugins\\fo4roulette_user.ini";

		enum class Kind
		{
			kFloat,   // float*
			kDouble,  // double* (the table spec)
			kBool,    // bool*
			kInt,     // int*
			kHex,     // std::uint32_t*, a form ID
			kChoice,  // int*, picked from `choices`
			kKey,     // int*, a virtual-key code, rebound by pressing it
			kText,    // std::string*
		};

		struct Choice
		{
			const char* label;
			int         value;
		};

		struct Entry
		{
			const wchar_t*          section;
			const wchar_t*          key;
			const char*             label;
			Kind                    kind;
			void*                   field;
			float                   min{ 0.0f };
			float                   max{ 1.0f };
			const char*             format{ "%.2f" };
			const char*             help{ nullptr };
			std::vector<Choice>     choices{};
			bool                    nextRack{ false };  // only takes effect on the next rack
		};

		struct Page
		{
			const char*        name;
			const char*        intro;
			std::vector<Entry> entries;
		};

		Settings& S() { return Settings::Get(); }

		std::vector<Page>& Pages()
		{
			auto& s = S();
			static std::vector<Page> pages{
				{ "Table", "The wheel, the limits, and how long bets stay open.",
					{
						{ L"Table", L"iWheel", "Wheel", Kind::kChoice, &s.wheel, 0, 0, nullptr,
							"European: one zero. American: 0 and 00 (and the top line bet). Changes the layout and the wheel on every table",
							{ { "European (single 0)", 0 }, { "American (0 and 00)", 1 } } },
						{ L"Table", L"fSpinDelay", "Wheel spins after", Kind::kFloat, &s.spinDelay, 3.0f, 300.0f, "%.0f s",
							"counted from the round's first bet (yours or a settler's); the spin key spins sooner" },
						{ L"Table", L"iMaxBet", "Most on one bet", Kind::kInt, &s.maxBet, 1, 100000, nullptr, "caps" },
						{ L"Table", L"fActiveDistance", "Table in play within", Kind::kFloat, &s.activeDistance, 500.0f, 20000.0f, "%.0f",
							"NPCs come to a table this close to you" },
					} },
				{ "Camera", "The view while you're at the table.",
					{
						{ L"Camera", L"fLayoutHeight", "Layout view height", Kind::kFloat, &s.layoutHeight, 40.0f, 600.0f, "%.0f" },
						{ L"Camera", L"fWheelHeight", "Wheel view height", Kind::kFloat, &s.wheelHeight, 25.0f, 400.0f, "%.0f" },
						{ L"Camera", L"bWheelViewOnSpin", "Watch the wheel on a spin", Kind::kBool, &s.wheelViewOnSpin, 0, 0, nullptr,
							"the camera goes over the wheel while the ball's going round, and back after" },
						{ L"Camera", L"fCursorSpeed", "Cursor speed", Kind::kFloat, &s.cursorSpeed, 0.005f, 0.5f, "%.3f" },
					} },
				{ "NPCs", "Settlers nearby come over to bet with their own caps.",
					{
						{ L"Bettors", L"bEnabled", "Settlers come and bet", Kind::kBool, &s.bettors },
						{ L"Bettors", L"iMax", "Most at the table", Kind::kInt, &s.maxBettors, 0, 4, nullptr },
						{ L"Bettors", L"bIdles", "Idles and reactions", Kind::kBool, &s.bettorIdles, 0, 0, nullptr,
							"they look the layout over, reach in to bet, cheer or shake their heads at the result" },
						{ L"Bettors", L"fEvery", "Look for someone every", Kind::kFloat, &s.bettorEvery, 2.0f, 120.0f, "%.0f s" },
						{ L"Bettors", L"fRange", "From up to", Kind::kFloat, &s.bettorRange, 200.0f, 6000.0f, "%.0f", "how far from the table" },
						{ L"Bettors", L"iMinRounds", "Rounds they stay (fewest)", Kind::kInt, &s.bettorMinRounds, 1, 50, nullptr },
						{ L"Bettors", L"iMaxRounds", "Rounds they stay (most)", Kind::kInt, &s.bettorMaxRounds, 1, 100, nullptr },
					} },
				{ "Poker", "Texas Hold'em, no limit. Settlers sit in with their own caps; 1-4 fold, call, raise, all in at the table.",
					{
						{ L"Poker", L"iSmallBlind", "Small blind", Kind::kInt, &s.pkSmallBlind, 1, 500, nullptr },
						{ L"Poker", L"iBigBlind", "Big blind", Kind::kInt, &s.pkBigBlind, 2, 1000, nullptr },
						{ L"Poker", L"iBuyIn", "Your buy-in (most)", Kind::kInt, &s.pkBuyIn, 20, 20000, nullptr,
							"the most of your caps you sit down with; the rest stay in your pocket" },
						{ L"Poker", L"bSettlers", "Settlers sit in", Kind::kBool, &s.pkPlayers },
						{ L"Poker", L"iMaxSettlers", "Most settlers at the table", Kind::kInt, &s.pkMaxPlayers, 0, 6, nullptr },
						{ L"Poker", L"iSettlerBuyIn", "A settler's buy-in (most)", Kind::kInt, &s.pkNpcBuyIn, 20, 20000, nullptr },
						{ L"Poker", L"fThinkTime", "Settlers' thinking time", Kind::kFloat, &s.pkThink, 0.2f, 5.0f, "%.1fx" },
						{ L"Poker", L"bChairs", "Settlers sit (chairs)", Kind::kBool, &s.pkChairs, 0, 0, nullptr,
							"chairs round the poker table and settlers sit in them (seated, they cheer and laugh but don't reach in to bet)" },
						{ L"Poker", L"bVoices", "Settlers' voices", Kind::kBool, &s.pkVoices, 0, 0, nullptr,
							"settlers cheer or laugh when they win a hand, and groan when they lose one at the showdown" },
						{ L"Poker", L"bBanter", "Table talk", Kind::kBool, &s.pkBanter, 0, 0, nullptr,
							"settlers who remember you greet you when they sit back in" },
						{ L"Poker", L"bTopDown", "View from above", Kind::kBool, &s.pkTopDown, 0, 0, nullptr,
							"start at the poker table looking down on it rather than from your seat (V switches either way at the table)" },
						{ L"Poker", L"fSeatBack", "Seat view: distance behind your cards", Kind::kFloat, &s.pkSeatBack, 0.0f, 150.0f, "%.0f" },
						{ L"Poker", L"fSeatHeight", "Seat view: height over the table", Kind::kFloat, &s.pkSeatHeight, 10.0f, 150.0f, "%.0f" },
						{ L"Poker", L"fSeatAim", "Seat view: how far ahead it looks", Kind::kFloat, &s.pkSeatAim, 0.0f, 120.0f, "%.0f" },
						{ L"Poker", L"fTopHeight", "View from above: height", Kind::kFloat, &s.pkTopHeight, 40.0f, 400.0f, "%.0f" },
						{ L"Poker", L"fTopShift", "View from above: moved towards you", Kind::kFloat, &s.pkTopShift, -40.0f, 60.0f, "%.0f" },
					} },
				{ "HUD & sound", "The HUD sizes itself to your screen height; the scale makes it bigger or smaller on top of that.",
					{
						{ L"HUD", L"fScale", "HUD scale", Kind::kFloat, &s.hudScale, 0.3f, 3.0f, "%.2fx" },
						{ L"HUD", L"bNotifications", "Corner messages too", Kind::kBool, &s.notifications, 0, 0, nullptr,
							"results also as the game's corner notifications" },
						{ L"Sound", L"bEnabled", "Sounds on", Kind::kBool, &s.sound, 0, 0, nullptr, "switching them on takes a game restart" },
						{ L"Sound", L"fVolume", "Volume", Kind::kFloat, &s.soundVolume, 0.0f, 1.0f, "%.2f" },
					} },
				{ "Keys", "Click a key's button, then press the key you want. Spin 'none' = the game's activate key.",
					{
						{ L"Keys", L"iSpin", "Spin", Kind::kKey, &s.keySpin },
						{ L"Keys", L"iLeave", "Leave the table", Kind::kKey, &s.keyLeave },
						{ L"Keys", L"iView", "Layout / wheel view", Kind::kKey, &s.keyView },
						{ L"Keys", L"iRebet", "Same bets again", Kind::kKey, &s.keyRebet },
						{ L"Keys", L"iClear", "Take your chips back", Kind::kKey, &s.keyClear },
						{ L"Keys", L"iPackUp", "Pack a kit table up", Kind::kKey, &s.keyPackUp },
						{ L"Keys", L"iTurnLeft", "Poker: turn the view left", Kind::kKey, &s.keyTurnLeft },
						{ L"Keys", L"iTurnRight", "Poker: turn the view right", Kind::kKey, &s.keyTurnRight },
						{ L"Keys", L"iZoom", "Poker: held, zooms in where you look", Kind::kKey, &s.keyZoom },
						{ L"Keys", L"iChip1", "1 cap chip", Kind::kKey, &s.keyChip[0] },
						{ L"Keys", L"iChip5", "5 cap chip", Kind::kKey, &s.keyChip[1] },
						{ L"Keys", L"iChip25", "25 cap chip", Kind::kKey, &s.keyChip[2] },
						{ L"Keys", L"iChip100", "100 cap chip", Kind::kKey, &s.keyChip[3] },
					} },
			};
			return pages;
		}

		// ------------------------------------------------------------ saving

		void Write(const Entry& a_e)
		{
			std::wstring value;
			switch (a_e.kind) {
			case Kind::kFloat:
				value = std::format(L"{}", *static_cast<float*>(a_e.field));
				break;
			case Kind::kDouble:
				value = std::format(L"{}", *static_cast<double*>(a_e.field));
				break;
			case Kind::kBool:
				value = *static_cast<bool*>(a_e.field) ? L"1" : L"0";
				break;
			case Kind::kInt:
			case Kind::kChoice:
				value = std::format(L"{}", *static_cast<int*>(a_e.field));
				break;
			case Kind::kKey:
				value = std::format(L"0x{:02X}", *static_cast<int*>(a_e.field));
				break;
			case Kind::kHex:
				value = std::format(L"0x{:X}", *static_cast<std::uint32_t*>(a_e.field));
				break;
			case Kind::kText:
				{
					const auto& str = *static_cast<std::string*>(a_e.field);
					value.assign(str.begin(), str.end());
				}
				break;
			}
			// (the file made first if it isn't there: the INI functions wouldn't make a new one under a mod manager's
			// virtual Data folder, and the setting was lost, read back from fo4roulette.ini the next time it was loaded)
			wchar_t full[MAX_PATH]{};
			const auto* path = ::GetFullPathNameW(kIniPath, MAX_PATH, full, nullptr) ? full : kIniPath;
			if (const HANDLE f = ::CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				f != INVALID_HANDLE_VALUE) {
				::CloseHandle(f);
			}
			if (!::WritePrivateProfileStringW(a_e.section, a_e.key, value.c_str(), path)) {
				// (no luck: into fo4roulette.ini itself, as before)
				if (!::WritePrivateProfileStringW(a_e.section, a_e.key, value.c_str(), L"Data\\F4SE\\Plugins\\fo4roulette.ini")) {
					log::warn("settings menu: could not write [{}] to the INI", static_cast<int>(a_e.kind));
				} else {
					log::warn("settings menu: fo4roulette_user.ini couldn't be written: saved to fo4roulette.ini");
				}
			}
		}

		// The same field in a freshly made Settings: the default.
		void* DefaultOf(const Entry& a_e)
		{
			static Settings defaults{};
			const auto offset = static_cast<char*>(a_e.field) - reinterpret_cast<char*>(&S());
			return reinterpret_cast<char*>(&defaults) + offset;
		}

		void Reset(const Entry& a_e)
		{
			void* def = DefaultOf(a_e);
			switch (a_e.kind) {
			case Kind::kFloat:
				*static_cast<float*>(a_e.field) = *static_cast<float*>(def);
				break;
			case Kind::kDouble:
				*static_cast<double*>(a_e.field) = *static_cast<double*>(def);
				break;
			case Kind::kBool:
				*static_cast<bool*>(a_e.field) = *static_cast<bool*>(def);
				break;
			case Kind::kInt:
			case Kind::kChoice:
			case Kind::kKey:
				*static_cast<int*>(a_e.field) = *static_cast<int*>(def);
				break;
			case Kind::kHex:
				*static_cast<std::uint32_t*>(a_e.field) = *static_cast<std::uint32_t*>(def);
				break;
			case Kind::kText:
				*static_cast<std::string*>(a_e.field) = *static_cast<std::string*>(def);
				break;
			}
			Write(a_e);
		}

		// ------------------------------------------------------------ keys

		const Entry* g_rebinding{ nullptr };
		std::array<bool, 256> g_keyWasDown{};

		// while waiting for a key: the first key newly pressed (mouse buttons and Escape aside)
		int NewKeyPress()
		{
			int pressed = 0;
			for (int vk = 0x08; vk < 0xFF; ++vk) {
				const bool down = (::GetAsyncKeyState(vk) & 0x8000) != 0;
				if (down && !g_keyWasDown[vk] && !pressed && vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_MBUTTON && vk != VK_XBUTTON1 &&
					vk != VK_XBUTTON2 && vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU) {
					pressed = vk;
				}
				g_keyWasDown[vk] = down;
			}
			return pressed;
		}

		// ------------------------------------------------------------ drawing

		void Help(const Entry& a_e)
		{
			if (a_e.help && IsItemHovered()) {
				SetTooltip("%s", a_e.help);
			}
		}

		void Row(const Entry& a_e)
		{
			PushID(a_e.key);
			SetNextItemWidth(260.0f);
			bool save = false;
			switch (a_e.kind) {
			case Kind::kFloat:
				{
					auto& v = *static_cast<float*>(a_e.field);
					if (SliderFloat(a_e.label, &v, a_e.min, a_e.max, a_e.format)) {
						v = std::clamp(v, a_e.min, a_e.max);
					}
					save = IsItemDeactivatedAfterEdit();
				}
				break;
			case Kind::kDouble:
				{
					auto& v = *static_cast<double*>(a_e.field);
					float f = static_cast<float>(v);
					if (SliderFloat(a_e.label, &f, a_e.min, a_e.max, a_e.format)) {
						v = std::clamp(f, a_e.min, a_e.max);
					}
					save = IsItemDeactivatedAfterEdit();
				}
				break;
			case Kind::kBool:
				save = Checkbox(a_e.label, static_cast<bool*>(a_e.field));
				break;
			case Kind::kInt:
				{
					auto& v = *static_cast<int*>(a_e.field);
					if (InputInt(a_e.label, &v)) {
						v = std::clamp(v, static_cast<int>(a_e.min), static_cast<int>(a_e.max));
					}
					save = IsItemDeactivatedAfterEdit();
				}
				break;
			case Kind::kChoice:
				{
					auto& v = *static_cast<int*>(a_e.field);
					int   current = 0;
					std::vector<const char*> labels;
					for (std::size_t i = 0; i < a_e.choices.size(); ++i) {
						labels.push_back(a_e.choices[i].label);
						if (a_e.choices[i].value == v) {
							current = static_cast<int>(i);
						}
					}
					if (Combo(a_e.label, &current, labels.data(), static_cast<int>(labels.size()))) {
						v = a_e.choices[current].value;
						save = true;
					}
				}
				break;
			case Kind::kHex:
				{
					auto& v = *static_cast<std::uint32_t*>(a_e.field);
					char  buf[16];
					std::snprintf(buf, sizeof(buf), "%X", v);
					if (InputText(a_e.label, buf, sizeof(buf), ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue) ||
						IsItemDeactivatedAfterEdit()) {
						v = static_cast<std::uint32_t>(std::strtoul(buf, nullptr, 16));
						save = true;
					}
				}
				break;
			case Kind::kText:
				{
					auto& str = *static_cast<std::string*>(a_e.field);
					char  buf[128];
					std::snprintf(buf, sizeof(buf), "%s", str.c_str());
					if (InputText(a_e.label, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue) || IsItemDeactivatedAfterEdit()) {
						str = buf;
						save = true;
					}
				}
				break;
			case Kind::kKey:
				{
					auto& v = *static_cast<int*>(a_e.field);
					const bool waiting = g_rebinding == &a_e;
					const std::string caption = waiting ? "press a key... (Esc cancels)" : KeyName(v);
					if (Button(std::format("{}##key", caption).c_str(), ImVec2{ 260.0f, 0.0f }) && !waiting) {
						g_rebinding = &a_e;
						NewKeyPress();  // forget whatever is held now
					}
					SameLine();
					Text("%s", a_e.label);
					if (waiting) {
						if (const int vk = NewKeyPress()) {
							if (vk != VK_ESCAPE) {
								v = vk;
								save = true;
							}
							g_rebinding = nullptr;
						}
					}
				}
				break;
			}
			Help(a_e);
			if (a_e.kind != Kind::kKey && a_e.kind != Kind::kBool) {
				SameLine();
				if (SmallButton("reset")) {
					Reset(a_e);
				}
			}
			if (save) {
				Write(a_e);
			}
			PopID();
		}

		template <std::size_t I>
		void __stdcall DrawPage()
		{
			auto& page = Pages()[I];
			TextWrapped("%s", page.intro);
			Spacing();
			bool rackNote = false;
			for (const auto& e : page.entries) {
				Row(e);
				rackNote |= e.nextRack;
			}
			Spacing();
			if (Button("Reset this page to the defaults")) {
				for (const auto& e : page.entries) {
					Reset(e);
				}
			}
			(void)rackNote;
			TextDisabled("Saved to Data\\F4SE\\Plugins\\fo4roulette_user.ini as you change them (kept over updates).");
		}

		template <std::size_t... I>
		void AddPages(std::index_sequence<I...>)
		{
			(F4SEMenuFramework::AddSectionItem(Pages()[I].name, &DrawPage<I>), ...);
		}
	}

	std::string KeyName(int a_vk)
	{
		if (a_vk <= 0) {
			return "none";
		}
		if (a_vk >= VK_NUMPAD0 && a_vk <= VK_NUMPAD9) {
			return std::format("Numpad {}", a_vk - VK_NUMPAD0);
		}
		const UINT scan = ::MapVirtualKeyW(static_cast<UINT>(a_vk), MAPVK_VK_TO_VSC);
		LONG       lparam = static_cast<LONG>(scan << 16);
		switch (a_vk) {
		case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
		case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE:
			lparam |= 1 << 24;  // extended keys
			break;
		default:
			break;
		}
		wchar_t name[64]{};
		if (::GetKeyNameTextW(lparam, name, 64) > 0) {
			std::string out;
			for (const wchar_t* c = name; *c; ++c) {
				out.push_back(*c < 128 ? static_cast<char>(*c) : '?');
			}
			return out;
		}
		return std::format("key 0x{:02X}", a_vk);
	}

	void Install()
	{
		static bool installed = false;
		if (installed || !F4SEMenuFramework::IsInstalled()) {
			return;
		}
		installed = true;
		Settings::Get().Load();  // the menu shows the INI's values from the start
		F4SEMenuFramework::SetSection("Real Roulette");
		AddPages(std::make_index_sequence<6>{});
		log::info("settings pages registered in F4SE Menu Framework ({} pages)", Pages().size());
	}
}
