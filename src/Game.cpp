// The table in play: finding it, its wheel and ball, the round (bets, spin, result) and the chips on the layout.
// You at the table (the camera, the cursor) are in Seat.cpp; the settlers who bet in Npcs.cpp; the
// co-save in Save.cpp.

#include "Game.h"
#include "MenuWatch.h"

#include "Audio.h"
#include "Forms.h"
#include "Hud.h"
#include "Npc.h"
#include "Settings.h"
#include "Util.h"
#include "rl/Table.h"

namespace fr
{
	namespace
	{
		constexpr float kMaxFrame = 0.05f;
		constexpr float kPlanLead = 0.6f;  // seconds between the spin starting and the ball going in
		constexpr float kScanEvery = 0.5f;
		constexpr float kLosersGo = 1.5f;     // after the result: losing chips taken
		constexpr float kResultSeconds = 5.0f; // ...and the table cleared for the next round
		constexpr std::size_t kHistory = 16;
		constexpr std::size_t kLogLines = 5;
		constexpr int kMaxStack = 8;           // chips shown in one stack (the chip models are detailed)

		using util::ForEachLoadedCell;

		std::string Upper(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return a_s;
		}

		float Distance2D(const RE::NiPoint3& a, const RE::NiPoint3& b) { return std::hypot(a.x - b.x, a.y - b.y); }
	}

	// ---------------------------------------------------------------- render thread

	void Game::OnPresent()
	{
		PollKeys();
		if (_tickPending.exchange(true)) {
			return;
		}
		auto* tasks = F4SE::GetTaskInterface();
		if (!tasks) {
			_tickPending = false;
			return;
		}
		tasks->AddTask([this]() {
			Tick();
			_tickPending = false;
		});
	}

	namespace
	{
		// the controller, read straight from XInput (loaded at first use; no controller, no pad)
		struct PadState
		{
			WORD buttons{ 0 };
			bool ok{ false };
		};

		PadState ReadPad()
		{
			struct XState
			{
				DWORD packet;
				WORD  buttons;
				BYTE  lt, rt;
				SHORT lx, ly, rx, ry;
			};
			using Get_t = DWORD(WINAPI*)(DWORD, XState*);
			static Get_t get = []() -> Get_t {
				for (const wchar_t* dll : { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" }) {
					if (HMODULE m = ::LoadLibraryW(dll)) {
						if (auto f = reinterpret_cast<Get_t>(::GetProcAddress(m, "XInputGetState"))) {
							return f;
						}
					}
				}
				return nullptr;
			}();
			PadState s;
			XState   x{};
			if (get && get(0, &x) == 0) {
				s.ok = true;
				s.buttons = x.buttons;
			}
			return s;
		}
	}

	void Game::PollPad()
	{
		// at the poker table: A check / call, B fold, X raise, Y all in, the D-pad up / down the raise, LB / RB turn,
		// Back step away, Start the view, the right stick's click your cards, the left stick's cash out
		const bool poker = AtCardTable();
		const auto pad = poker && util::GameHasFocus() ? ReadPad() : PadState{};
		const WORD now = pad.ok ? pad.buttons : 0;
		const WORD pressed = now & ~_padWas;
		_padWas = now;
		_padTurn = (now & 0x0200 ? 1 : 0) - (now & 0x0100 ? 1 : 0);  // RB, LB
		if (now) {
			_padUsed = true;
		}
		auto* tasks = F4SE::GetTaskInterface();
		if (!pressed || !tasks || !poker) {
			return;
		}
		static constexpr std::array<std::pair<WORD, int>, 10> kMap{ { { 0x1000, 1 }, { 0x2000, 0 }, { 0x4000, 2 }, { 0x8000, 3 },
			{ 0x0001, 4 }, { 0x0002, 5 }, { 0x0020, 6 }, { 0x0010, 9 }, { 0x0080, 10 }, { 0x0040, 8 } } };
		// (blackjack: A, B, X, Y as the 1-4 keys; Back leave; Start the view; right stick your hand on the HUD)
		static constexpr std::array<std::pair<WORD, int>, 7> kBjMap{ { { 0x1000, 80 }, { 0x2000, 81 }, { 0x4000, 82 }, { 0x8000, 83 },
			{ 0x0020, 67 }, { 0x0010, 66 }, { 0x0080, 69 } } };
		// (a slot machine: A pull, X / Y the bet down / up, B or Back get up)
		static constexpr std::array<std::pair<WORD, int>, 5> kSlMap{ { { 0x1000, 94 }, { 0x4000, 96 }, { 0x8000, 97 }, { 0x2000, 95 },
			{ 0x0020, 95 } } };
		if (_slSeated) {
			for (const auto& [mask, key] : kSlMap) {
				if (pressed & mask) {
					tasks->AddTask([this, key]() {
						if (!IsGamePaused() || key == 95) {
							SlKey(key);
						}
					});
				}
			}
			return;
		}
		if (_bjSeated) {
			for (const auto& [mask, key] : kBjMap) {
				if (pressed & mask) {
					tasks->AddTask([this, key]() {
						if (!IsGamePaused()) {
							BjKey(key);
						}
					});
				}
			}
			return;
		}
		for (const auto& [mask, key] : kMap) {
			if (pressed & mask) {
				tasks->AddTask([this, key]() {
					if (!IsGamePaused()) {
						PkKey(key);
					}
				});
			}
		}
	}

	void Game::PollKeys()
	{
		const auto& s = Settings::Get();
		const bool  focus = util::GameHasFocus();
		const bool  seated = _seated.load();
		const int   spin = _activateVk.load();  // (activate: blackjack's deal; at roulette nothing, the dealer spins)
		const bool  placing = _kitPlacing.load();
		const bool  poker = AtCardTable();  // (poker or blackjack: the pointer and its buttons)
		const std::array<int, 15> keys{ spin, s.keyLeave, s.keyView, s.keyRebet, s.keyClear, s.keyChip[0], s.keyChip[1], s.keyChip[2],
			s.keyChip[3], s.keyPackUp, VK_LEFT, VK_RIGHT, _activateVk.load(), VK_UP, VK_DOWN };
		auto* tasks = F4SE::GetTaskInterface();
		PollPad();
		// at the poker table: a left click on one of the buttons
		{
			const bool down = focus && poker && (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
			if (down && !_pkLmbWasDown && tasks) {
				double px, py;
				{
					std::scoped_lock lock(_camMutex);
					px = _pkPtrX;
					py = _pkPtrY;
				}
				int id = -1;
				{
					std::scoped_lock lock(_btnMutex);
					for (const auto& b : _pkButtons) {
						if (b.on && px >= b.x0 && px <= b.x1 && py >= b.y0 && py <= b.y1) {
							id = b.id;
						}
					}
				}
				_pkDragging = id == 11 && !_bjSeated && !_slSeated;  // (the raise slider: dragged while the button's held, below)
				if (id >= 0 && (id != 11 || _bjSeated || _slSeated)) {
					tasks->AddTask([this, id]() {
						if (!IsGamePaused()) {
							CardKey(id);
						}
					});
				}
			}
			if (down && _pkDragging && tasks) {
				double px;
				{
					std::scoped_lock lock(_camMutex);
					px = _pkPtrX;
				}
				float frac = 0.0f;
				bool  found = false;
				{
					std::scoped_lock lock(_btnMutex);
					for (const auto& b : _pkButtons) {
						if (b.id == 11 && b.t1 > b.t0) {
							frac = static_cast<float>((px - b.t0) / (b.t1 - b.t0));  // (past either end: clamped)
							found = true;
						}
					}
				}
				if (found) {
					tasks->AddTask([this, frac]() { PkSetRaise(frac); });
				}
			}
			if (!down) {
				_pkDragging = false;
			}
			_pkLmbWasDown = down;
		}
		for (std::size_t i = 0; i < keys.size(); ++i) {
			const bool down = focus && keys[i] > 0 && (::GetAsyncKeyState(keys[i]) & 0x8000) != 0;
			const bool pressed = down && !_keyWasDown[i];
			const bool released = !down && _keyWasDown[i];
			_keyWasDown[i] = down;  // (tracked always, so the press that sat you down isn't also a spin)
			if (placing && i == 12) {
				// setting a kit table up: the activate key sets it down when you let go of it (a press that began
				// before the kit was used, in the Pip-Boy, doesn't count)
				if (pressed) {
					_kitConfirmArmed = true;
				} else if (released && _kitConfirmArmed && tasks) {
					_kitConfirmArmed = false;
					tasks->AddTask([this]() {
						if (!IsGamePaused()) {
							ConfirmKit();
						}
					});
				}
				continue;
			}
			if (!pressed || !tasks) {
				continue;
			}
			if (placing) {
				// setting a kit table up: the arrows turn it, the leave key puts the kit away
				if (i == 1 || i == 10 || i == 11) {
					tasks->AddTask([this, i]() {
						if (IsGamePaused()) {
							return;
						}
						switch (i) {
						case 1: CancelKit(true); break;
						case 10: TurnKit(-1); break;
						default: TurnKit(1); break;
						}
					});
				}
				continue;
			}
			if (_slSeated) {
				// at a slot machine: 1-4 the bet (1, 5, 10, 25), activate or same-again pull, leave gets up
				int sk = -1;
				switch (i) {
				case 5: sk = 90; break;
				case 6: sk = 91; break;
				case 7: sk = 92; break;
				case 8: sk = 93; break;
				case 0:
				case 3: sk = 94; break;
				case 1: sk = 95; break;
				default: break;
				}
				if (sk >= 0) {
					tasks->AddTask([this, sk]() {
						if (!IsGamePaused() || sk == 95) {
							SlKey(sk);
						}
					});
				}
				continue;
			}
			if (_bjSeated) {
				// at the blackjack table: 1-4 the chips while bets are open, hit / stand / double / split on your turn
				// (insurance: 1 yes, 2 no); the spin key deals; same bet, take it back; the view; leave; pack up
				int bk = -1;
				switch (i) {
				case 5: bk = 80; break;
				case 6: bk = 81; break;
				case 7: bk = 82; break;
				case 8: bk = 83; break;
				case 0: bk = 55; break;
				case 3: bk = 56; break;
				case 4: bk = 54; break;
				case 1: bk = 67; break;
				case 2: bk = 66; break;
				case 9: bk = 68; break;
				default: break;
				}
				if (bk >= 0) {
					tasks->AddTask([this, bk]() {
						if (!IsGamePaused() || bk == 67) {
							BjKey(bk);
						}
					});
				}
				continue;
			}
			if (poker) {
				// at the poker table: 1-4 fold, check/call, raise, all in; up/down the raise; leave; pack up
				int pk = -1;
				switch (i) {
				case 5: pk = 0; break;
				case 6: pk = 1; break;
				case 7: pk = 2; break;
				case 8: pk = 3; break;
				case 13: pk = 4; break;
				case 14: pk = 5; break;
				case 1: pk = 6; break;
				case 4: pk = 8; break;
				case 2: pk = 9; break;
				case 9: pk = 7; break;
				default: break;
				}
				if (pk >= 0) {
					tasks->AddTask([this, pk]() {
						if (!IsGamePaused()) {
							PkKey(pk);
						}
					});
				}
				continue;
			}
			if (!seated || i >= 10) {
				continue;
			}
			if (i >= 5 && i <= 8) {
				_chip = static_cast<int>(i - 5);
				continue;
			}
			tasks->AddTask([this, i]() {
				if (i == 1) {
					StandUp();  // leaving always works, whatever's open
					return;
				}
				if (IsGamePaused()) {
					return;
				}
				switch (i) {
				case 0: break;  // (the dealer spins the wheel: it goes by itself after the first bet)
				case 1: StandUp(); break;
				case 2: ToggleView(); break;
				case 3: Rebet(); break;
				case 4:
					if (_t.phase == Phase::kBetting && HasBets(0)) {
						TakeAllBack(0);
						Say("YOUR CHIPS ARE BACK");
					}
					break;
				case 9: PackUpTable(); break;
				default: break;
				}
			});
		}
		// the mouse buttons, at the table
		const bool lmb = seated && focus && (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		const bool rmb = seated && focus && (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
		if (tasks && lmb && !_lmbWasDown) {
			if (_rlUi) {
				// on the buttons: which one
				double px, py;
				{
					std::scoped_lock lock(_camMutex);
					px = _pkPtrX;
					py = _pkPtrY;
				}
				int id = -1;
				{
					std::scoped_lock lock(_btnMutex);
					for (const auto& b : _pkButtons) {
						if (b.on && px >= b.x0 && px <= b.x1 && py >= b.y0 && py <= b.y1) {
							id = b.id;
						}
					}
				}
				if (id >= 20 && id <= 23) {
					_chip = id - 20;
				} else if (id >= 24) {
					tasks->AddTask([this, id]() {
						if (id == 28) {
							StandUp();
							return;
						}
						if (IsGamePaused()) {
							return;
						}
						switch (id) {
						case 25: Rebet(); break;
						case 26:
							if (_t.phase == Phase::kBetting && HasBets(0)) {
								TakeAllBack(0);
								Say("YOUR CHIPS ARE BACK");
							}
							break;
						case 27: ToggleView(); break;
						case 29: PackUpTable(); break;
						default: break;
						}
					});
				}
			} else {
				tasks->AddTask([this]() { PlaceChip(); });
			}
		}
		if (tasks && rmb && !_rmbWasDown && !_rlUi) {
			tasks->AddTask([this]() { RemoveChip(); });
		}
		_lmbWasDown = lmb;
		_rmbWasDown = rmb;
	}

	// ---------------------------------------------------------------- main thread

	bool Game::IsGamePaused()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return false;
		}
		// talking to someone doesn't stop the world (or the wheel): the game counts the conversation as a menu, so
		// with only that open, menu mode is ignored
		if (ui->freezeFramePause != 0 || (ui->menuMode != 0 && !menus::Open(menus::kDialogue))) {
			return true;
		}
		return menus::AnyBlocking();  // (from the menu events: asking the UI holds the menus, see MenuWatch.h)
	}

	void Game::Notify(const std::string& a_message)
	{
		log::info("{}", a_message);
		RE::SendHUDMessage::ShowHUDMessage(a_message.c_str(), nullptr, false, false);
	}

	void Game::Say(const std::string& a_text, int a_tone)
	{
		if (!_t.log.empty() && _t.log.back().text == a_text) {
			return;  // (the same again, a bet over the limit tried again say: once is enough)
		}
		log::info("[table] {}", a_text);
		_t.log.push_back({ a_text, a_tone });
		while (_t.log.size() > kLogLines) {
			_t.log.pop_front();
		}
		if (Settings::Get().notifications || (!_secondary && (!Hud::Showing() || (!_seated && a_tone != 0)))) {
			RE::SendHUDMessage::ShowHUDMessage(("Roulette: " + a_text).c_str(), nullptr, false, false);
		}
	}

	void Game::Tick()
	{
		const auto now = std::chrono::steady_clock::now();
		float      dt = _haveLast ? std::chrono::duration<float>(now - _last).count() : 0.0f;
		_last = now;
		_haveLast = true;
		dt = std::clamp(dt, 0.0f, kMaxFrame);
		// someone talking to you while you're at the table (a greeting that got through): up, so the conversation can
		// happen with your own controls and camera
		if (_seated) {
			if (menus::Open(menus::kDialogue)) {
				log::info("a conversation started while you were at the table: getting up");
				StandUp();
			}
		}
		if (_pkSeated) {
			if (menus::Open(menus::kDialogue)) {
				log::info("a conversation started while you were at the poker table: getting up");
				PkStandUp();
			}
		}
		if (_bjSeated) {
			if (menus::Open(menus::kDialogue)) {
				log::info("a conversation started while you were at the blackjack table: getting up");
				BjStandUp();
			}
		}
		if (_slSeated) {
			if (menus::Open(menus::kDialogue)) {
				log::info("a conversation started while you were at a slot machine: getting up");
				SlStandUp();
			}
		}
		if (IsGamePaused()) {
			return;  // (menus, the Pip-Boy, the workshop: everything holds still; a conversation doesn't)
		}
		if (auto* map = RE::ControlMap::GetSingleton()) {
			// keyboard bindings are virtual-key codes in Fallout 4
			const auto vk = map->GetMappedKey("Activate", RE::INPUT_DEVICE::kKeyboard);
			if (vk != RE::ControlMap::kInvalid && vk > 0 && vk < 0xFF) {
				_activateVk = static_cast<int>(vk);
			}
		}
		if (_clearNpcs && Npc::Ready()) {
			_clearNpcs = false;
			Npc::ClearAll();  // a load: whoever was in the quest's collections in that save goes back to their own business
		}
		if (_kitPlacing) {
			TickKitPlacement();
		}
		_scanTimer -= dt;
		if (_scanTimer <= 0.0f) {
			_scanTimer = kScanEvery;
			ScanTables();
			PkScan();
			BjScan();
			SlScan();
		}
		TableReach();
		TickPoker(dt);
		TickBlackjack(dt);
		TickSlots(dt);
		if (_t.table) {
			TickTable(dt);
		}
		if (_other.table) {
			// the second table in play, the same way, with nothing of you at the table about it
			SwapTables();
			_secondary = true;
			TickTable(dt);
			_secondary = false;
			SwapTables();
		}
		UpdateHud();
	}

	// ---------------------------------------------------------------- tables

	void Game::ScanTables()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !Forms::Table()) {
			return;
		}
		const auto& s = Settings::Get();
		const auto  setting = s.wheel == 1 ? rl::WheelType::kAmerican : rl::WheelType::kEuropean;
		RE::TESObjectREFR* nearest = nullptr;
		float              nearestD = std::numeric_limits<float>::max();
		std::vector<RE::TESObjectREFR*> found;
		ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(player->data.location, s.activeDistance + 3000.0f, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && Forms::IsTable(a_ref) && !a_ref->IsDisabled() && a_ref->Get3D() && std::ranges::find(found, a_ref) == found.end()) {
					found.push_back(a_ref);
					const float d = (a_ref->data.location - player->data.location).Length();
					if (d < nearestD) {
						nearestD = d;
						nearest = a_ref;
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		// every table shows the wheel and layout of the setting (the ones in play: of the round on them)
		for (auto* t : found) {
			const bool here = _t.table && _t.table.get().get() == t;
			const bool other = _other.table && _other.table.get().get() == t;
			ApplyWheel(t, here ? _t.wheel : other ? _other.wheel : setting);
		}
		_nearTables.clear();
		for (auto* t : found) {
			_nearTables.push_back(t->GetHandle());
		}

		(void)nearest;
		// the tables in play: up to two, the nearest within reach; one already in play stays so a little further out
		auto distanceTo = [&](const Slot& a_slot) {
			auto* t = a_slot.table.get().get();
			const bool loaded = t && t->Get3D() && !t->IsDisabled() && std::ranges::find(found, t) != found.end();
			return loaded ? (t->data.location - player->data.location).Length() : std::numeric_limits<float>::max();
		};
		for (int k = 0; k < 2; ++k) {
			if (k == 1) {
				SwapTables();
				_secondary = true;
			}
			if (_t.table) {
				const float d = distanceTo(_t);
				if (d > s.activeDistance + 400.0f) {
					EndSession(d < std::numeric_limits<float>::max() ? "you walked away" : "the table's gone");
				}
			}
			if (k == 1) {
				_secondary = false;
				SwapTables();
			}
		}
		std::ranges::sort(found, {}, [&](RE::TESObjectREFR* a_t) { return (a_t->data.location - player->data.location).Length(); });
		for (auto* t : found) {
			if ((t->data.location - player->data.location).Length() >= s.activeDistance) {
				break;
			}
			if (IsKitPreview(t)) {
				continue;  // (the one following your crosshair while you set a kit up)
			}
			if (_t.table.get().get() == t || _other.table.get().get() == t) {
				continue;
			}
			if (!_t.table) {
				StartSession(t);
			} else if (!_other.table) {
				SwapTables();
				_secondary = true;
				StartSession(t);
				_secondary = false;
				SwapTables();
			}
		}
		// the one you're at (or else the nearer) is the one in _t: the HUD's and your keys'
		if (!_seated && _other.table && distanceTo(_other) + 300.0f < distanceTo(_t)) {
			SwapTables();
		}
		// how high the views can go under whatever's above the table
		if (_seated) {
			if (auto t = _t.table.get()) {
				Frame frame;
				if (FrameOf(t.get(), frame)) {
					_t.roomLayout = CeilingAbove(frame, rl::LayoutCentre().x, rl::LayoutCentre().y, s.layoutHeight + 40.0f);
					_t.roomWheel = CeilingAbove(frame, rl::table::kWheelX, rl::table::kWheelY, s.wheelHeight + 40.0f);
				}
			}
		}
	}

	void Game::ApplyWheel(RE::TESObjectREFR* a_table, rl::WheelType a_type)
	{
		auto* root = a_table ? a_table->Get3D() : nullptr;
		if (!root) {
			return;
		}
		const bool us = a_type == rl::WheelType::kAmerican;
		static const std::array<std::pair<RE::BSFixedString, bool>, 6> nodes{ std::pair{ RE::BSFixedString("LayoutEU"), false },
			std::pair{ RE::BSFixedString("LayoutGridEU"), false }, std::pair{ RE::BSFixedString("RotorEU"), false },
			std::pair{ RE::BSFixedString("LayoutUS"), true }, std::pair{ RE::BSFixedString("LayoutGridUS"), true },
			std::pair{ RE::BSFixedString("RotorUS"), true } };
		for (const auto& [name, isUS] : nodes) {
			if (auto* node = root->GetObjectByName(name)) {
				const bool hide = isUS != us;
				if (node->GetAppCulled() != hide) {
					node->SetAppCulled(hide);
				}
			}
		}
	}

	bool Game::FrameOf(RE::TESObjectREFR* a_table, Frame& a_out) const
	{
		auto* root = a_table ? a_table->Get3D() : nullptr;
		if (!root) {
			return false;
		}
		const auto& w = root->GetWorldTransform();
		a_out.rot = w.rotate;
		a_out.origin = w.translate;
		a_out.scale = w.scale != 0.0f ? w.scale : 1.0f;
		a_out.yaw = a_table->data.angle.z;
		return true;
	}

	RE::NiPoint3 Game::ToWorld(const Frame& a_frame, double a_x, double a_y, double a_z) const
	{
		const RE::NiPoint3 local{ static_cast<float>(a_x), static_cast<float>(a_y), static_cast<float>(a_z) };
		return a_frame.origin + a_frame.rot.Transpose() * (local * a_frame.scale);
	}

	rl::Vec2 Game::ToLocal(const Frame& a_frame, const RE::NiPoint3& a_world) const
	{
		const auto p = a_frame.rot * ((a_world - a_frame.origin) * (1.0f / a_frame.scale));
		return { p.x, p.y };
	}

	float Game::HeadingOf(const Frame& a_frame, double a_dx, double a_dy) const
	{
		const auto w = a_frame.rot.Transpose() * RE::NiPoint3{ static_cast<float>(a_dx), static_cast<float>(a_dy), 0.0f };
		return std::atan2(w.x, w.y);
	}

	RE::ObjectRefHandle Game::Spawn(RE::TESBoundObject* a_form, const RE::NiPoint3& a_pos, float a_angle) const
	{
		// in the cell of the table it's for: whichever table in play (roulette or poker) is nearest the spot; failing
		// both, yours
		auto*                       dh = RE::TESDataHandler::GetSingleton();
		RE::TESObjectCELL*          cell = nullptr;
		float                       best = 1e30f;
		for (auto ref : { _t.table.get(), _other.table.get(), _pk.table.get() }) {
			if (auto* c = ref ? ref->GetParentCell() : nullptr) {
				const float d = (ref->data.location - a_pos).Length();
				if (d < best) {
					best = d;
					cell = c;
				}
			}
		}
		if (!cell) {
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				cell = player->GetParentCell();
			}
		}
		if (!dh || !cell || !a_form) {
			return {};
		}
		RE::NEW_REFR_DATA data;
		data.location = a_pos;
		data.direction = { 0.0f, 0.0f, a_angle };
		data.object = a_form;
		if (cell->IsInterior()) {
			data.interior = cell;
		} else {
			data.world = cell->worldSpace;
		}
		data.initializeScripts = false;
		return dh->CreateReferenceAtLocation(data);
	}

	void Game::Remove(RE::ObjectRefHandle& a_handle)
	{
		if (auto ref = a_handle.get()) {
			ref->Disable();
			ref->SetWantsDelete(true);
		}
		a_handle = {};
	}

	void Game::PlaceNode(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, float a_yaw, bool a_hidden)
	{
		auto* root = a_ref ? a_ref->Get3D() : nullptr;
		if (!root) {
			return;
		}
		if (root->GetAppCulled() != a_hidden) {
			root->SetAppCulled(a_hidden);
		}
		if (a_hidden) {
			return;
		}
		const RE::NiMatrix3 rot = util::FromAxes({ std::cos(a_yaw), -std::sin(a_yaw), 0.0f }, { std::sin(a_yaw), std::cos(a_yaw), 0.0f },
			{ 0.0f, 0.0f, 1.0f });
		RE::NiTransform local;
		if (auto* parent = root->parent) {
			const auto& p = parent->GetWorldTransform();
			const float ps = p.scale != 0.0f ? p.scale : 1.0f;
			local.rotate = rot * p.rotate.Transpose();
			local.translate = (p.rotate * (a_pos - p.translate)) * (1.0f / ps);
			local.scale = 1.0f / ps;
		} else {
			local.rotate = rot;
			local.translate = a_pos;
			local.scale = 1.0f;
		}
		root->SetLocalTransform(local);
		RE::NiUpdateData update;
		root->Update(update);
		a_ref->data.location.x = a_pos.x;
		a_ref->data.location.y = a_pos.y;
		a_ref->data.location.z = a_pos.z;
	}

	void Game::StartSession(RE::TESObjectREFR* a_table)
	{
		Settings::Get().Load();
		_t.table = a_table->GetHandle();
		_t.wheel = Settings::Get().wheel == 1 ? rl::WheelType::kAmerican : rl::WheelType::kEuropean;
		_t.phase = Phase::kBetting;
		_t.planning = false;
		_t.phaseTime = 0.0f;
		_t.bettingTime = 0.0f;
		_t.rotorAngle = std::uniform_real_distribution<double>(0.0, 6.283)(_rng);
		_t.rotorSpeed = std::uniform_real_distribution<double>(0.3, 0.9)(_rng);
		_t.restPocket = -1;
		_t.stakes.clear();
		_t.seatTimer = 3.0f;
		_t.history.clear();
		ApplyWheel(a_table, _t.wheel);
		log::info("table {:08X} in play ({} wheel)", a_table->GetFormID(), _t.wheel == rl::WheelType::kAmerican ? "American" : "European");
	}

	void Game::EndSession(const char* a_why)
	{
		if (!_t.table) {
			return;
		}
		log::info("table {:08X} no longer in play: {}", _t.table.get() ? _t.table.get()->GetFormID() : 0u, a_why);
		if (Here()) {
			StandUp();
			Remove(_ghost);
		}
		RefundEverything();
		Remove(_t.ball);
		for (auto& seat : _t.seats) {
			ReleaseSeat(seat, nullptr);
		}
		_t.seats.clear();
		ReleaseCroupier(_t.croupier, a_why);
		_t.table = {};
		_t.phase = Phase::kBetting;
		_t.planning = false;
		_t.hudBanner = 0;
	}

	void Game::OnLoadingStarts()
	{
		PkEnd("a loading screen");
		BjEnd("a loading screen");
		SlEnd("a loading screen");
		if (_kitPlacing) {
			CancelKit(true);  // travelling while setting a kit up: the kit back
		}
		if (_t.table) {
			EndSession("a loading screen");
		}
		if (_other.table) {
			SwapTables();
			_secondary = true;
			EndSession("a loading screen");
			_secondary = false;
			SwapTables();
		}
	}

	void Game::TickTable(float a_dt)
	{
		auto  table = _t.table.get();
		Frame frame;
		if (!table || !FrameOf(table.get(), frame)) {
			return;
		}
		if (_lifted) {
			return;  // a save is being written
		}

		switch (_t.phase) {
		case Phase::kBetting:
			_t.bettingTime += a_dt;
			_t.rotorSpeed = rl::RotorDrag(_t.rotorSpeed, a_dt);
			_t.rotorAngle += _t.rotorSpeed * a_dt;
			// the wheel spins itself a while after the round's first bet (yours or anyone's)
			if (_t.stakes.empty()) {
				_t.spinIn = -1.0f;
				_t.reached = false;
			} else if (_t.spinIn < 0.0f) {
				_t.spinIn = Settings::Get().spinDelay;
			} else {
				_t.spinIn -= a_dt;
				// the dealer's reach out over the wheel a second before the spin: their hand goes as the ball's sent round
				if (!_t.reached && _t.spinIn <= 1.0f) {
					_t.reached = true;
					RlDealer(Gesture::kDeal, true);
				}
				if (_t.spinIn <= 0.0f) {
					StartSpin(false);
				}
			}
			break;
		case Phase::kSpinning:
			TickSpin(frame, a_dt);
			break;
		case Phase::kResult:
			_t.phaseTime += a_dt;
			_t.rotorSpeed = rl::RotorDrag(_t.rotorSpeed, a_dt);
			_t.rotorAngle += _t.rotorSpeed * a_dt;
			if (_t.phaseTime >= kLosersGo) {
				// the dealer sweeps the losing chips
				bool swept = false;
				for (auto it = _t.stakes.begin(); it != _t.stakes.end();) {
					if (it->won == 0) {
						DespawnStake(*it);
						it = _t.stakes.erase(it);
						swept = true;
					} else {
						++it;
					}
				}
				if (swept) {
					Audio::Play(Audio::Bank::kChip, ToWorld(frame, rl::LayoutCentre().x, rl::LayoutCentre().y, rl::table::kFeltZ), 0.6f, 0.0f, Audio::kFar);
				}
			}
			if (_t.phaseTime >= kResultSeconds) {
				ClearRound();
			}
			break;
		}
		if (!_secondary && _viewBackIn >= 0.0f) {
			_viewBackIn -= a_dt;
			if (_viewBackIn < 0.0f) {
				_wheelView = false;
			}
		}

		_t.rotorAngle = std::remainder(_t.rotorAngle, 6.283185307179586);  // (kept small: the spin plans are in floats)
		TurnRotor(frame);
		PlaceBall(frame);
		MakeStacks(frame);
		TickSeats(frame, a_dt);
		if (Here()) {
			// the cursor: mouse right is the table's +X (the camera's right), mouse forward its +Y (up the screen)
			{
				std::scoped_lock lock(_camMutex);
				const double k = Settings::Get().cursorSpeed;
				const auto   lo = rl::LayoutMin(), hi = rl::LayoutMax();
				// the cursor on the layout; pushed on down past the layout's near edge, the pointer drops onto the
				// buttons along the bottom of the screen (and up off them again, back onto the layout)
				float barTop = 1e9f, sw, sh;
				{
					std::scoped_lock blk(_btnMutex);
					for (const auto& b : _pkButtons) {
						barTop = std::min(barTop, b.y0);
					}
					sw = _scrW;
					sh = _scrH;
				}
				if (!_rlUi) {
					const double y = _cursorY - _mouseDY * k;
					if (y < lo.y && _mouseDY > 0.0) {
						_rlPush += lo.y - y;
					} else if (_mouseDY < 0.0) {
						_rlPush = 0.0;
					}
					_cursorX = std::clamp(_cursorX + _mouseDX * k, lo.x, hi.x);
					_cursorY = std::clamp(y, lo.y, hi.y);
					if (_rlPush > 6.0 && barTop < 1e8f) {
						_rlUi = true;
						_rlPush = 0.0;
						_pkPtrX = sw * 0.5;
						_pkPtrY = barTop + 20.0f;
					}
				} else {
					const double kp = sh / 1080.0 * Settings::Get().cursorSpeed / 0.07;
					_pkPtrX = std::clamp(_pkPtrX + _mouseDX * kp, 0.0, static_cast<double>(sw - 1.0f));
					_pkPtrY = std::clamp(_pkPtrY + _mouseDY * kp, 0.0, static_cast<double>(sh - 1.0f));
					if (_pkPtrY < barTop - 60.0f * sh / 1080.0f) {
						_rlUi = false;  // (up off the buttons: back on the layout, at its near edge)
						_cursorY = lo.y;
					}
				}
				_mouseDX = _mouseDY = 0.0;
				const auto& s = Settings::Get();
				const bool  wheel = _wheelView.load();
				const double cx = wheel ? rl::table::kWheelX : rl::LayoutCentre().x;
				const double cy = wheel ? rl::table::kWheelY : rl::LayoutCentre().y;
				const float  want = wheel ? s.wheelHeight : s.layoutHeight;
				const float  room = (wheel ? _t.roomWheel : _t.roomLayout) - 15.0f;  // (a ceiling: the view comes down under it)
				_camWant = ToWorld(frame, cx, cy - 2.0, rl::table::kFeltZ + std::clamp(room, 30.0f, want));
				_camYaw = HeadingOf(frame, 0.0, 1.0);
			}
			Audio::SetListenerYaw(_camYaw, true);
			PlaceGhost(frame);
		}
	}

	void Game::TurnRotor(const Frame& a_frame)
	{
		(void)a_frame;
		auto  table = _t.table.get();
		auto* root = table ? table->Get3D() : nullptr;
		if (!root) {
			return;
		}
		static const RE::BSFixedString eu("RotorEU"), us("RotorUS");
		auto* node = root->GetObjectByName(_t.wheel == rl::WheelType::kAmerican ? us : eu);
		if (!node) {
			return;
		}
		// the rotor's x axis at _t.rotorAngle round the table's z (rows: its axes in the table's space)
		const float c = static_cast<float>(std::cos(_t.rotorAngle));
		const float s = static_cast<float>(std::sin(_t.rotorAngle));
		node->local.rotate = util::FromAxes({ c, s, 0.0f }, { -s, c, 0.0f }, { 0.0f, 0.0f, 1.0f });
		RE::NiUpdateData update;
		node->Update(update);
	}

	void Game::PlaceBall(const Frame& a_frame)
	{
		using namespace rl::table;
		double r = 0.0, th = 0.0, z = 0.0;
		bool   show = false;
		if (_t.phase == Phase::kSpinning && !_t.plan.samples.empty()) {
			const auto& smp = _t.plan.samples;
			const float f = _t.playT * 60.0f;
			const std::size_t i = std::min<std::size_t>(static_cast<std::size_t>(f), smp.size() - 1);
			const std::size_t j = std::min(i + 1, smp.size() - 1);
			const double      u = std::clamp(static_cast<double>(f - static_cast<float>(i)), 0.0, 1.0);
			r = smp[i].r + (smp[j].r - smp[i].r) * u;
			th = smp[i].theta + std::remainder(static_cast<double>(smp[j].theta) - smp[i].theta, 6.283185307) * u;
			z = smp[i].z + (smp[j].z - smp[i].z) * u;
			show = true;
		} else if (_t.restPocket >= 0) {
			r = kPocketCentre;
			th = _t.rotorAngle + rl::PocketAngle(_t.wheel, _t.restPocket);
			z = kPocketFloorZ + kBallRadius;
			show = true;
		}
		const RE::NiPoint3 pos = ToWorld(a_frame, kWheelX + r * std::cos(th), kWheelY + r * std::sin(th), z);
		if (!_t.ball && show) {
			_t.ball = Spawn(Forms::Ball(), pos, 0.0f);
		}
		if (auto ball = _t.ball.get()) {
			PlaceNode(ball.get(), pos, a_frame.yaw, !show);
		}
	}

	// ---------------------------------------------------------------- the round

	void Game::StartSpin(bool a_byPlayer)
	{
		if (!_t.table || _t.phase != Phase::kBetting) {
			if (a_byPlayer && _t.phase != Phase::kBetting) {
				Say("THE BALL'S ALREADY GOING ROUND");
			}
			return;
		}
		auto  table = _t.table.get();
		Frame frame;
		if (!table || !FrameOf(table.get(), frame)) {
			return;
		}
		// a push on the rotor if it's slowed right down, and the ball thrown the other way, from the wheel's end
		std::uniform_real_distribution<double> U(0.0, 1.0);
		if (_t.rotorSpeed < 1.4) {
			_t.rotorSpeed = 1.6 + 0.6 * U(_rng);
			Audio::Play(Audio::Bank::kSpin, ToWorld(frame, rl::table::kWheelX, rl::table::kWheelY, rl::table::kFeltZ), 0.8f, 0.0f, Audio::kFar);
		}
		// the throw is worked out on another thread (finding one that lands in the drawn pocket and bounces about
		// properly can take a while), for where the rotor will be a moment from now, when the ball goes in
		rl::SpinParams p;
		p.throwAngle = 3.14159265;  // from the wheel's end (-X)
		p.ballSpeed = -(15.0 + 3.0 * U(_rng));
		double angle = _t.rotorAngle, speed = _t.rotorSpeed;
		for (int k = 0; k < 60; ++k) {
			speed = rl::RotorDrag(speed, kPlanLead / 60.0);
			angle += speed * (kPlanLead / 60.0);
		}
		p.rotorAngle = angle;
		p.rotorSpeed = speed;
		const auto type = _t.wheel;
		const auto seed = _rng();
		_t.planFuture = std::async(std::launch::async, [type, p, seed]() {
			std::mt19937 rng(seed);
			return rl::PlanSpin(type, p, rng);
		});
		_t.plan = {};
		_t.planning = true;
		if (!_t.reached) {
			RlDealer(Gesture::kDeal, true);  // (a reach out over the wheel: sending the ball round; a second early, when timed)
		}
		_t.reached = false;
		_t.phase = Phase::kSpinning;
		_t.phaseTime = 0.0f;
		_t.playT = -kPlanLead;
		_t.dropSaid = false;
		_t.hudBanner = 1;
		_t.bannerTitle = "NO MORE BETS";
		_t.bannerSub = a_byPlayer ? std::string("YOU SPIN") : std::string("THE WHEEL SPINS");
		_t.spinIn = -1.0f;
		_t.bannerTime = std::chrono::steady_clock::now();
		if (Here() && Settings::Get().wheelViewOnSpin) {
			_wheelView = true;
			_viewBackIn = -1.0f;
		}
	}

	void Game::LaunchSpin(const Frame& a_frame)
	{
		const auto& frame = a_frame;
		log::info("spin: {} ({} pocket {}; {} tries); drop {:.1f}s, rotor {:.1f}s, rest {:.1f}s; {:.0f} pockets round the rotor, {} frets",
			rl::NumberName(_t.plan.number), _t.plan.drawn ? "drawn" : "NOT drawn", _t.plan.pocket, _t.plan.tries, _t.plan.dropTime, _t.plan.rotorTime,
			_t.plan.settleTime, _t.plan.travel, _t.plan.frets);
		_t.restPocket = -1;
		// the roll: as loud as the ball is fast, round the track and down the cone, gone as it reaches the rotor
		{
			std::vector<std::pair<float, float>> keys;
			const auto& smp = _t.plan.samples;
			double      fastest = 0.0;
			auto speedAt = [&](std::size_t a_i) {
				const std::size_t j = std::min(a_i + 15, smp.size() - 1);
				const std::size_t i = j > 15 ? j - 15 : 0;
				const double      dt = std::max(1e-3, static_cast<double>(smp[j].t - smp[i].t));
				return std::abs(std::remainder(static_cast<double>(smp[j].theta) - smp[i].theta, 6.283185307)) * smp[i].r / dt;
			};
			for (std::size_t i = 0; i < smp.size() && smp[i].t <= _t.plan.rotorTime; i += 15) {
				fastest = std::max(fastest, speedAt(i));
			}
			for (std::size_t i = 0; i < smp.size() && smp[i].t <= _t.plan.rotorTime; i += 15) {
				const double v = fastest > 0.0 ? speedAt(i) / fastest : 0.0;
				keys.emplace_back(smp[i].t, static_cast<float>(std::pow(std::clamp(v, 0.0, 1.0), 0.7) * (smp[i].t < _t.plan.dropTime ? 1.0 : 0.8)));
			}
			if (!keys.empty()) {
				keys.front().second = 0.0f;
				keys.insert(keys.begin() + 1, { 0.08f, keys.size() > 1 ? keys[1].second : 1.0f });
				keys.emplace_back(_t.plan.rotorTime + 0.12f, 0.0f);
				Audio::PlayShaped(Audio::Bank::kRoll, ToWorld(frame, rl::table::kWheelX, rl::table::kWheelY, rl::table::kTrackZ), 0.75f, keys, Audio::kFar);
			}
		}
		_t.eventIdx = 0;
		_t.lastKnock = -1.0f;
	}

	void Game::TickSpin(const Frame& a_frame, float a_dt)
	{
		using namespace rl::table;
		_t.playT += a_dt;
		if (_t.planning) {
			// the rotor carries on as it was until the throw's ready (and as the throw was worked out for)
			_t.rotorSpeed = rl::RotorDrag(_t.rotorSpeed, a_dt);
			_t.rotorAngle += _t.rotorSpeed * a_dt;
			if (_t.playT < 0.0f || !_t.planFuture.valid() ||
				_t.planFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
				return;
			}
			_t.plan = _t.planFuture.get();
			_t.planning = false;
			if (_t.playT > 0.05f) {
				log::info("spin: the throw took {:.2f}s longer to work out than the lead; joining it that far in", _t.playT);
			}
			LaunchSpin(a_frame);
		}
		const auto& smp = _t.plan.samples;
		if (smp.empty()) {
			Settle();
			return;
		}
		const std::size_t i = std::min<std::size_t>(static_cast<std::size_t>(_t.playT * 60.0f), smp.size() - 1);
		const std::size_t j = std::min(i + 1, smp.size() - 1);
		const double      u = std::clamp(static_cast<double>(_t.playT * 60.0f - static_cast<float>(i)), 0.0, 1.0);
		_t.rotorAngle = smp[i].rotor + std::remainder(static_cast<double>(smp[j].rotor) - smp[i].rotor, 6.283185307) * u;
		// each knock as it happens: off a diamond, a fret, coming down from a bounce, rocking into place
		const auto wheel = ToWorld(a_frame, kWheelX, kWheelY, kRotorEdgeZ);
		while (_t.eventIdx < _t.plan.events.size() && _t.plan.events[_t.eventIdx].t <= _t.playT) {
			const auto& ev = _t.plan.events[_t.eventIdx++];
			using Kind = rl::SpinEvent::Kind;
			const float s = ev.strength;
			if (ev.kind != Kind::kSettle && ev.t - _t.lastKnock < 0.03f && s < 0.5f) {
				continue;  // (two in the same instant sound as one)
			}
			switch (ev.kind) {
			case Kind::kDiamond:
				Audio::Play(Audio::Bank::kRattle, wheel, 0.5f + 0.5f * s, 0.0f, Audio::kFar);
				break;
			case Kind::kFret:
				if (s > 0.45f) {
					Audio::Play(Audio::Bank::kRattle, wheel, 0.3f + 0.5f * s, 0.0f, Audio::kFar);
				} else {
					Audio::Play(Audio::Bank::kTick, wheel, 0.25f + 0.75f * s, 0.0f, Audio::kFar);
				}
				break;
			case Kind::kLand:
				if (s < 0.07f) {
					continue;
				}
				Audio::Play(Audio::Bank::kTick, wheel, std::min(1.0f, 0.15f + 0.85f * s), 0.0f, Audio::kFar);
				break;
			case Kind::kSettle:
				Audio::Play(Audio::Bank::kSettle, wheel, 0.45f, 0.0f, Audio::kFar);
				break;
			}
			_t.lastKnock = ev.t;
		}
		if (_t.playT >= smp.back().t) {
			Settle();
		}
	}

	void Game::Settle()
	{
		const int number = _t.plan.number;
		_t.restPocket = _t.plan.pocket;
		if (!_t.plan.samples.empty()) {
			_t.rotorAngle = _t.plan.samples.back().rotor;
		}
		_t.rotorSpeed = _t.plan.rotorSpeed;
		_t.history.push_front(number);
		while (_t.history.size() > kHistory) {
			_t.history.pop_back();
		}
		// who won what
		int  yourStake = 0, yourBack = 0, yourWin = 0;
		std::map<RE::TESFormID, int> npcNet;
		_t.lastRound.clear();
		for (auto& st : _t.stakes) {
			if (st.owner == 0) {
				yourStake += st.amount;
				_t.lastRound.emplace_back(st.bet.key, st.amount);
			}
			if (st.bet.Wins(number)) {
				st.won = st.amount * st.bet.Payout();
				st.dirty = true;  // its winnings go down beside it
				if (st.owner == 0) {
					yourBack += st.amount + st.won;
					yourWin += st.won;
				} else {
					AddToPurse(st.owner, st.amount + st.won);
					npcNet[st.owner] += st.won;
				}
			} else {
				st.won = 0;
				if (st.owner != 0) {
					npcNet[st.owner] -= st.amount;
				}
			}
		}
		if (yourBack > 0) {
			GivePlayer(yourBack);
			Audio::PlayUI(Audio::Bank::kChip, 0.8f);  // your caps coming back
		}
		_t.lastWin = yourBack - yourStake;
		const std::string name = std::format("{} {}", rl::NumberName(number), rl::ColourName(number));
		_t.hudBanner = yourStake == 0 ? 4 : yourWin > 0 ? 2 : 3;
		_t.bannerTitle = name;
		_t.bannerNumber = number;
		if (yourStake == 0) {
			_t.bannerSub.clear();
		} else if (yourWin > 0) {
			_t.bannerSub = std::format("YOU WIN {} CAPS", yourWin);
		} else {
			_t.bannerSub = std::format("YOU LOSE {} CAPS", yourStake);
		}
		_t.bannerTime = std::chrono::steady_clock::now();
		Say(yourStake == 0 ? name : std::format("{} · {}", name, _t.bannerSub), yourStake == 0 ? 0 : yourWin > 0 ? 1 : 2);
		for (auto& seat : _t.seats) {
			seat.roundNet = 0;
			if (auto it = npcNet.find(seat.id); it != npcNet.end()) {
				seat.roundNet = it->second;
				if (it->second > 0) {
					Say(std::format("{} WINS {}", Upper(seat.name), it->second), 1);
				}
			}
			// a moment to see where the ball went, then a reaction to how they did
			if (seat.step == Seat::Step::kPlaying && seat.betThisRound && seat.roundNet != 0) {
				seat.reactIn = std::uniform_real_distribution<float>(0.4f, 1.4f)(_rng);
			}
			if (seat.betThisRound) {
				--seat.rounds;
			}
			seat.betThisRound = false;
		}
		log::info("result {}: you staked {}, got back {}; {} stakes on the table", name, yourStake, yourBack, _t.stakes.size());
		_t.phase = Phase::kResult;
		_t.phaseTime = 0.0f;
		RlDealer(Gesture::kPoint);  // (at the wheel: the number's in)
		if (Here() && _wheelView && Settings::Get().wheelViewOnSpin) {
			_viewBackIn = 2.0f;  // a look at the ball in its pocket, then back to the layout
		}
	}

	void Game::ClearRound()
	{
		for (auto& st : _t.stakes) {
			DespawnStake(st);
		}
		_t.stakes.clear();
		_t.phase = Phase::kBetting;
		_t.planning = false;
		_t.phaseTime = 0.0f;
		_t.bettingTime = 0.0f;
		RlDealer(Gesture::kBeckon);  // (place your bets)
		_t.hudBanner = 0;
		// a changed wheel setting comes in now, between rounds
		const auto setting = Settings::Get().wheel == 1 ? rl::WheelType::kAmerican : rl::WheelType::kEuropean;
		if (setting != _t.wheel) {
			_t.wheel = setting;
			_t.restPocket = -1;
			_t.lastRound.clear();
			if (auto table = _t.table.get()) {
				ApplyWheel(table.get(), _t.wheel);
			}
			Say(_t.wheel == rl::WheelType::kAmerican ? "AMERICAN WHEEL (0 AND 00)" : "EUROPEAN WHEEL (SINGLE 0)");
		}
	}

	bool Game::HasBets(RE::TESFormID a_owner) const
	{
		return std::ranges::any_of(_t.stakes, [&](const Stake& s) { return s.owner == a_owner && s.won < 0; });
	}

	int Game::OnTable(RE::TESFormID a_owner) const
	{
		int sum = 0;
		for (const auto& s : _t.stakes) {
			if (s.owner == a_owner && s.won < 0) {
				sum += s.amount;
			}
		}
		return sum;
	}

	// ---------------------------------------------------------------- chips

	int Game::PlayerCaps()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* caps = Forms::Caps();
		return player && caps ? static_cast<int>(player->GetInventoryObjectCount(caps)) : 0;
	}

	void Game::GivePlayer(int a_caps)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* caps = Forms::Caps();
		if (player && caps && a_caps > 0) {
			player->AddObjectToContainer(caps, nullptr, a_caps, nullptr, RE::ITEM_REMOVE_REASON::kNone);
		}
	}

	bool Game::TakeFromPlayer(int a_caps)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* caps = Forms::Caps();
		if (!player || !caps || a_caps <= 0 || PlayerCaps() < a_caps) {
			return false;
		}
		RE::TESObjectREFR::RemoveItemData data(caps, a_caps);
		player->RemoveItem(data);
		return true;
	}

	Game::Stake* Game::StakeFor(RE::TESFormID a_owner, const std::string& a_key)
	{
		for (auto& s : _t.stakes) {
			if (s.owner == a_owner && s.bet.key == a_key && s.won < 0) {
				return &s;
			}
		}
		return nullptr;
	}

	bool Game::AddChips(RE::TESFormID a_owner, const rl::Bet& a_bet, int a_amount)
	{
		if (_t.phase != Phase::kBetting || a_amount <= 0) {
			return false;
		}
		const int maxBet = Settings::Get().maxBet;
		Stake*    existing = StakeFor(a_owner, a_bet.key);
		const int already = existing ? existing->amount : 0;
		if (already + a_amount > maxBet) {
			if (a_owner == 0) {
				Say(std::format("THE LIMIT IS {} ON ONE BET", maxBet), 2);
			}
			return false;
		}
		if (a_owner == 0) {
			if (!TakeFromPlayer(a_amount)) {
				Say(std::format("NOT ENOUGH CAPS ({} NEEDED)", a_amount), 2);
				return false;
			}
		} else {
			if (Purse(a_owner) < a_amount) {
				return false;
			}
			AddToPurse(a_owner, -a_amount);
		}
		if (!existing) {
			Stake s;
			s.owner = a_owner;
			s.bet = a_bet;
			_t.stakes.push_back(std::move(s));
			existing = &_t.stakes.back();
		}
		existing->amount += a_amount;
		existing->dirty = true;
		if (auto table = _t.table.get()) {
			Frame frame;
			if (FrameOf(table.get(), frame)) {
				Audio::Play(Audio::Bank::kChip, ToWorld(frame, a_bet.spot.x, a_bet.spot.y, rl::table::kFeltZ), 0.7f, 0.0f, Audio::kFar);
			}
		}
		return true;
	}

	void Game::TakeBack(RE::TESFormID a_owner, const std::string& a_key)
	{
		for (auto it = _t.stakes.begin(); it != _t.stakes.end(); ++it) {
			if (it->owner == a_owner && it->bet.key == a_key && it->won < 0) {
				if (a_owner == 0) {
					GivePlayer(it->amount);
				} else {
					AddToPurse(a_owner, it->amount);
				}
				DespawnStake(*it);
				_t.stakes.erase(it);
				return;
			}
		}
	}

	void Game::TakeAllBack(RE::TESFormID a_owner)
	{
		std::vector<std::string> keys;
		for (const auto& s : _t.stakes) {
			if (s.owner == a_owner && s.won < 0) {
				keys.push_back(s.bet.key);
			}
		}
		for (const auto& k : keys) {
			TakeBack(a_owner, k);
		}
	}

	void Game::RefundEverything()
	{
		int yours = 0;
		for (auto& s : _t.stakes) {
			if (s.won < 0) {
				if (s.owner == 0) {
					yours += s.amount;
				} else {
					AddToPurse(s.owner, s.amount);
				}
			}
			DespawnStake(s);
		}
		_t.stakes.clear();
		if (yours > 0) {
			GivePlayer(yours);
			Notify(std::format("Roulette: your {} caps on the table are back", yours));
		}
		if (_t.phase == Phase::kSpinning) {
			_t.phase = Phase::kBetting;
			_t.planning = false;
		}
	}

	void Game::DespawnStake(Stake& a_stake)
	{
		for (auto& h : a_stake.chips) {
			Remove(h);
		}
		for (auto& h : a_stake.paid) {
			Remove(h);
		}
		a_stake.chips.clear();
		a_stake.paid.clear();
		a_stake.dirty = true;
	}

	rl::Vec2 Game::OwnerOffset(RE::TESFormID a_owner) const
	{
		// you right on the spot; each NPC a little to one side, so stacks on the same bet can be told apart
		if (a_owner == 0) {
			return {};
		}
		static constexpr std::array<std::pair<double, double>, 6> offsets{ { { 1.7, 1.3 }, { -1.7, 1.3 }, { 1.7, -1.3 }, { -1.7, -1.3 },
			{ 0.0, 2.1 }, { 0.0, -2.1 } } };
		for (std::size_t i = 0; i < _t.seats.size(); ++i) {
			if (_t.seats[i].id == a_owner) {
				const auto& o = offsets[static_cast<std::size_t>(_t.seats[i].spot) % offsets.size()];
				return { o.first, o.second };
			}
		}
		return { 1.7, 1.3 };
	}

	void Game::MakeStacks(const Frame& a_frame)
	{
		using namespace rl::table;
		std::uniform_real_distribution<float> spinAngle(0.0f, 6.283f);
		// every stack on the layout, so a new one goes where there's room: no chips through each other (stakes on the
		// same bet, or on bets next to each other, from different players)
		struct Foot
		{
			double x, y, top;
		};
		std::vector<Foot> feet;
		const auto height = [](int a_amount) {
			return static_cast<double>(std::min<std::size_t>(rl::ChipsFor(a_amount).size(), static_cast<std::size_t>(kMaxStack))) * kChipHeight;
		};
		for (const auto& st : _t.stakes) {
			if (!st.dirty && st.placed) {
				feet.push_back({ st.at.x, st.at.y, st.base + height(st.amount) });
				if (st.won > 0 && st.paidPlaced) {
					feet.push_back({ st.paidAt.x, st.paidAt.y, st.paidBase + height(st.won) });
				}
			}
		}
		constexpr double kClear = kChipRadius * 2.0 + 0.15;
		// a place for a stack: the one wanted (or where it was), else the nearest clear one round it (a little way
		// out at most: still on its bet); none, and it goes on top of what's there
		const auto place = [&](rl::Vec2 a_want, const rl::Vec2* a_was, double& a_base) {
			const auto clear = [&](double a_x, double a_y) {
				return std::ranges::none_of(feet, [&](const Foot& f) { return std::hypot(f.x - a_x, f.y - a_y) < kClear; });
			};
			if (a_was && clear(a_was->x, a_was->y)) {
				a_base = 0.0;
				return *a_was;
			}
			if (clear(a_want.x, a_want.y)) {
				a_base = 0.0;
				return a_want;
			}
			for (double r = 0.8; r <= 2.81; r += 0.5) {
				for (int k = 0; k < 12; ++k) {
					const double a = k * 0.5235987756;
					const rl::Vec2 p{ a_want.x + r * std::cos(a), a_want.y + r * std::sin(a) };
					if (clear(p.x, p.y)) {
						a_base = 0.0;
						return p;
					}
				}
			}
			a_base = 0.0;
			for (const auto& f : feet) {
				if (std::hypot(f.x - a_want.x, f.y - a_want.y) < kClear) {
					a_base = std::max(a_base, f.top);
				}
			}
			return a_want;
		};
		for (auto& st : _t.stakes) {
			if (!st.dirty) {
				continue;
			}
			st.dirty = false;
			for (auto& h : st.chips) {
				Remove(h);
			}
			for (auto& h : st.paid) {
				Remove(h);
			}
			st.chips.clear();
			st.paid.clear();
			const auto off = OwnerOffset(st.owner);
			auto stack = [&](int a_amount, double a_x, double a_y, double a_base, std::vector<RE::ObjectRefHandle>& a_out) {
				auto chips = rl::ChipsFor(a_amount);
				if (chips.size() > static_cast<std::size_t>(kMaxStack)) {
					chips.erase(chips.begin() + kMaxStack, chips.end());  // (the biggest are first)
				}
				// biggest at the bottom
				for (std::size_t i = 0; i < chips.size(); ++i) {
					const int idx = static_cast<int>(std::ranges::find(rl::kChipValues, chips[i]) - std::begin(rl::kChipValues));
					const auto pos = ToWorld(a_frame, a_x, a_y, kFeltZ + 0.02 + a_base + static_cast<double>(i) * kChipHeight);
					a_out.push_back(Spawn(Forms::Chip(idx), pos, a_frame.yaw + spinAngle(_rng)));
				}
			};
			const rl::Vec2 want{ st.bet.spot.x + off.x, st.bet.spot.y + off.y };
			const rl::Vec2 was = st.at;
			st.at = place(want, st.placed ? &was : nullptr, st.base);
			stack(st.amount, st.at.x, st.at.y, st.base, st.chips);
			feet.push_back({ st.at.x, st.at.y, st.base + height(st.amount) });
			if (st.won > 0) {
				const rl::Vec2 pwas = st.paidAt;
				st.paidAt = place({ st.at.x + kChipRadius * 2.2, st.at.y }, st.paidPlaced ? &pwas : nullptr, st.paidBase);
				st.paidPlaced = true;
				stack(st.won, st.paidAt.x, st.paidAt.y, st.paidBase, st.paid);
				feet.push_back({ st.paidAt.x, st.paidAt.y, st.paidBase + height(st.won) });
			}
			st.placed = true;
		}
	}

	float Game::CeilingAbove(const Frame& a_frame, double a_x, double a_y, float a_upTo) const
	{
		// straight up from just above the felt: how high the room goes, or a_upTo if nothing's in the way
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!cell) {
			return a_upTo;
		}
		constexpr float    kFrom = 15.0f;
		const RE::NiPoint3 start = ToWorld(a_frame, a_x, a_y, rl::table::kTrackZ + kFrom);
		const RE::NiPoint3 end = start + RE::NiPoint3{ 0.0f, 0.0f, a_upTo - kFrom };
		RE::bhkPickData    pick;
		pick.SetStartEnd(start, end);
		cell->Pick(pick);
		if (!pick.HasHit()) {
			return a_upTo;
		}
		return kFrom + (a_upTo - kFrom) * std::clamp(pick.GetHitFraction(), 0.0f, 1.0f);
	}

	// ---------------------------------------------------------------- saves and loads

	void Game::BeforeSave()
	{
		if (_kitPlacing) {
			CancelKit(true);  // a save mid-set-up: the kit back in your inventory, no half-made table saved
		}
		PkBeforeSave();
		BjBeforeSave();
		SlBeforeSave();
		Remove(_ghost);
		_ghostValue = -1;
		BeforeSaveTable();
		SwapTables();
		BeforeSaveTable();
		SwapTables();
	}

	void Game::BeforeSaveTable()
	{
		if (!_t.table) {
			return;
		}
		// nothing of ours in the save: the chips, the ball and the markers off; your caps on the layout back in your
		// pocket for the save (taken again once it's written); nobody held
		_lifted = true;
		_t.liftedCaps = OnTable(0);
		GivePlayer(_t.liftedCaps);
		for (auto& st : _t.stakes) {
			DespawnStake(st);
		}
		Remove(_t.ball);
		UnholdCroupier(_t.croupier);
		for (auto& seat : _t.seats) {
			Remove(seat.walk.marker);
			if (seat.held) {
				if (auto actor = seat.actor.get()) {
					Npc::SetRestrained(actor->As<RE::Actor>(), false);
				}
				seat.held = false;
			}
		}
		log::info("before save: our objects off, {} caps back to you for the save", _t.liftedCaps);
	}

	void Game::AfterSave()
	{
		PkAfterSave();
		BjAfterSave();
		if (!_lifted) {
			return;
		}
		_lifted = false;
		AfterSaveTable();
		SwapTables();
		AfterSaveTable();
		SwapTables();
		log::info("after save: carrying on");
	}

	void Game::AfterSaveTable()
	{
		if (_t.liftedCaps > 0 && !TakeFromPlayer(_t.liftedCaps)) {
			// (can't happen unless something took caps mid-save) the bets are void
			log::warn("after save: couldn't take the {} caps back; your bets are void", _t.liftedCaps);
			for (auto it = _t.stakes.begin(); it != _t.stakes.end();) {
				it = it->owner == 0 && it->won < 0 ? _t.stakes.erase(it) : std::next(it);
			}
		}
		_t.liftedCaps = 0;
	}

	void Game::Forget()
	{
		// a load: everything we'd spawned went with the old game; let go of it all without touching it
		_seated = false;
		BlockControls(false);
		QuietNpcs(false);
		_haveEntryView = false;
		_t = Slot{};
		_other = Slot{};
		_pk = PokerState{};
		_pkSeated = false;
		_pkYou = -1;
		_bj = BlackjackState{};
		_bjSeated = false;
		_slSeated = false;
		_sl.clear();
		_slYou = -1;
		_slSits = false;
		_slSat = false;
		_slGetUp = -1.0f;
		_slLog.clear();
		_bjYou = -1;
		_camPitch = -1.0f;
		_kitPlacing = false;  // (the preview went with the old game)
		_kitPreview = {};
		_ghost = {};
		_ghostValue = -1;
		_lifted = false;
		_wheelView = false;
		_clearNpcs = true;
		_haveLast = false;
		{
			std::scoped_lock lock(_camMutex);
			_camMapped = false;
			_haveCamPos = false;
		}
	}
}
