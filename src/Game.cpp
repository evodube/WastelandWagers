// The table in play: finding it, its wheel and ball, the round (bets, spin, result) and the chips on the layout.
// You at the table (the camera, the cursor) are in Seat.cpp; the dealer and the settlers who bet in Npcs.cpp; the
// co-save in Save.cpp.

#include "Game.h"

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
		constexpr float kScanEvery = 0.5f;
		constexpr float kLosersGo = 1.5f;     // after the result: losing chips taken
		constexpr float kResultSeconds = 5.0f; // ...and the table cleared for the next round
		constexpr std::size_t kHistory = 16;
		constexpr std::size_t kLogLines = 5;
		constexpr int kMaxStack = 12;          // chips shown in one stack

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

	void Game::PollKeys()
	{
		const auto& s = Settings::Get();
		const bool  focus = util::GameHasFocus();
		const bool  seated = _seated.load();
		const int   spin = s.keySpin > 0 ? s.keySpin : _activateVk.load();
		const std::array<int, 10> keys{ spin, s.keyLeave, s.keyView, s.keyRebet, s.keyClear, s.keyChip[0], s.keyChip[1], s.keyChip[2],
			s.keyChip[3], s.keyDealer };
		auto* tasks = F4SE::GetTaskInterface();
		for (std::size_t i = 0; i < keys.size(); ++i) {
			const bool down = focus && keys[i] > 0 && (::GetAsyncKeyState(keys[i]) & 0x8000) != 0;
			const bool pressed = down && !_keyWasDown[i];
			_keyWasDown[i] = down;  // (tracked always, so the press that sat you down isn't also a spin)
			if (!pressed || !tasks || (i < 9 && !seated)) {
				continue;
			}
			if (i >= 5 && i <= 8) {
				_chip = static_cast<int>(i - 5);
				continue;
			}
			tasks->AddTask([this, i]() {
				if (IsGamePaused()) {
					return;
				}
				switch (i) {
				case 0: StartSpin(true); break;
				case 1: StandUp(); break;
				case 2: ToggleView(); break;
				case 3: Rebet(); break;
				case 4:
					if (_phase == Phase::kBetting && HasBets(0)) {
						TakeAllBack(0);
						Say("YOUR CHIPS ARE BACK");
					}
					break;
				case 9: AssignDealer(); break;
				default: break;
				}
			});
		}
		// the mouse buttons, at the table
		const bool lmb = seated && focus && (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		const bool rmb = seated && focus && (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
		if (tasks && lmb && !_lmbWasDown) {
			tasks->AddTask([this]() { PlaceChip(); });
		}
		if (tasks && rmb && !_rmbWasDown) {
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
		if (ui->menuMode != 0 || ui->freezeFramePause != 0) {
			return true;
		}
		static const std::array<RE::BSFixedString, 7> menus{ RE::BSFixedString("PauseMenu"), RE::BSFixedString("PipboyMenu"),
			RE::BSFixedString("LoadingMenu"), RE::BSFixedString("MainMenu"), RE::BSFixedString("Console"), RE::BSFixedString("WorkshopMenu"),
			RE::BSFixedString("DialogueMenu") };
		return std::ranges::any_of(menus, [&](const RE::BSFixedString& a_name) { return ui->GetMenuOpen(a_name); });
	}

	void Game::Notify(const std::string& a_message)
	{
		log::info("{}", a_message);
		RE::SendHUDMessage::ShowHUDMessage(a_message.c_str(), nullptr, false, false);
	}

	void Game::Say(const std::string& a_text, int a_tone)
	{
		log::info("[table] {}", a_text);
		_log.push_back({ a_text, a_tone });
		while (_log.size() > kLogLines) {
			_log.pop_front();
		}
		if (Settings::Get().notifications || !Hud::Showing()) {
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
		if (IsGamePaused()) {
			return;  // (menus, the workshop, talking: everything holds still)
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
		_scanTimer -= dt;
		if (_scanTimer <= 0.0f) {
			_scanTimer = kScanEvery;
			ScanTables();
		}
		if (_table) {
			TickTable(dt);
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
		// every table shows the wheel and layout of the setting (the one in play: of the round on it)
		for (auto* t : found) {
			ApplyWheel(t, _table && _table.get().get() == t ? _wheel : setting);
		}
		_nearTables.clear();
		for (auto* t : found) {
			_nearTables.push_back(t->GetHandle());
		}

		// the table in play: the nearest within reach; the one already in play stays so a little further out
		if (_table) {
			auto*       current = _table.get().get();
			const bool  loaded = current && current->Get3D() && !current->IsDisabled() && std::ranges::find(found, current) != found.end();
			const float d = loaded ? (current->data.location - player->data.location).Length() : std::numeric_limits<float>::max();
			if (!loaded || d > s.activeDistance + 400.0f) {
				EndSession(loaded ? "you walked away" : "the table's gone");
			} else if (nearest && nearest != current && nearestD < d - 300.0f && !_seated && _phase == Phase::kBetting) {
				EndSession("another table is nearer");
			}
		}
		if (!_table && nearest && nearestD < s.activeDistance) {
			StartSession(nearest);
		}
		// how high the views can go under whatever's above the table
		if (_seated) {
			if (auto t = _table.get()) {
				Frame frame;
				if (FrameOf(t.get(), frame)) {
					_roomLayout = CeilingAbove(frame, rl::LayoutCentre().x, rl::LayoutCentre().y, s.layoutHeight + 40.0f);
					_roomWheel = CeilingAbove(frame, rl::table::kWheelX, rl::table::kWheelY, s.wheelHeight + 40.0f);
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
		static const std::array<std::pair<RE::BSFixedString, bool>, 4> nodes{ std::pair{ RE::BSFixedString("LayoutEU"), false },
			std::pair{ RE::BSFixedString("RotorEU"), false }, std::pair{ RE::BSFixedString("LayoutUS"), true },
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
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto  table = _table.get();
		auto* cell = table ? table->GetParentCell() : nullptr;
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
		_table = a_table->GetHandle();
		_wheel = Settings::Get().wheel == 1 ? rl::WheelType::kAmerican : rl::WheelType::kEuropean;
		_phase = Phase::kBetting;
		_phaseTime = 0.0f;
		_bettingTime = 0.0f;
		_rotorAngle = std::uniform_real_distribution<double>(0.0, 6.283)(_rng);
		_rotorSpeed = std::uniform_real_distribution<double>(0.3, 0.9)(_rng);
		_restPocket = -1;
		_stakes.clear();
		_seatTimer = 3.0f;
		_dealer = {};
		_history.clear();
		ApplyWheel(a_table, _wheel);
		log::info("table {:08X} in play ({} wheel)", a_table->GetFormID(), _wheel == rl::WheelType::kAmerican ? "American" : "European");
		LoadDealerFor(a_table);
	}

	void Game::EndSession(const char* a_why)
	{
		if (!_table) {
			return;
		}
		log::info("table {:08X} no longer in play: {}", _table.get() ? _table.get()->GetFormID() : 0u, a_why);
		if (_seated) {
			StandUp();
		}
		RefundEverything();
		Remove(_ball);
		Remove(_ghost);
		for (auto& seat : _seats) {
			ReleaseSeat(seat, nullptr);
		}
		_seats.clear();
		ReleaseDealer();
		_dealer = {};
		_table = {};
		_phase = Phase::kBetting;
		_hudBanner = 0;
	}

	void Game::OnLoadingStarts()
	{
		if (_table) {
			EndSession("a loading screen");
		}
	}

	void Game::TickTable(float a_dt)
	{
		auto  table = _table.get();
		Frame frame;
		if (!table || !FrameOf(table.get(), frame)) {
			return;
		}
		if (_lifted) {
			return;  // a save is being written
		}

		switch (_phase) {
		case Phase::kBetting:
			_bettingTime += a_dt;
			_rotorSpeed = rl::RotorDrag(_rotorSpeed, a_dt);
			_rotorAngle += _rotorSpeed * a_dt;
			if (!_seated && DealerReady() && _bettingTime > Settings::Get().autoSpin &&
				std::ranges::any_of(_stakes, [](const Stake& s) { return s.owner != 0; })) {
				StartSpin(false);  // the NPCs have bet and you're not sitting in: the dealer spins for them
			}
			break;
		case Phase::kSpinning:
			TickSpin(frame, a_dt);
			break;
		case Phase::kResult:
			_phaseTime += a_dt;
			_rotorSpeed = rl::RotorDrag(_rotorSpeed, a_dt);
			_rotorAngle += _rotorSpeed * a_dt;
			if (_phaseTime >= kLosersGo) {
				// the dealer sweeps the losing chips
				bool swept = false;
				for (auto it = _stakes.begin(); it != _stakes.end();) {
					if (it->won == 0) {
						DespawnStake(*it);
						it = _stakes.erase(it);
						swept = true;
					} else {
						++it;
					}
				}
				if (swept) {
					Audio::Play(Audio::Bank::kChip, ToWorld(frame, rl::LayoutCentre().x, rl::LayoutCentre().y, rl::table::kFeltZ), 0.6f);
				}
			}
			if (_phaseTime >= kResultSeconds) {
				ClearRound();
			}
			break;
		}
		if (_viewBackIn >= 0.0f) {
			_viewBackIn -= a_dt;
			if (_viewBackIn < 0.0f) {
				_wheelView = false;
			}
		}

		TurnRotor(frame);
		PlaceBall(frame);
		MakeStacks(frame);
		TickDealer(frame, a_dt);
		TickSeats(frame, a_dt);
		if (_seated) {
			// the cursor: mouse right is the table's +X (the camera's right), mouse forward its +Y (up the screen)
			{
				std::scoped_lock lock(_camMutex);
				const double k = Settings::Get().cursorSpeed;
				const auto   lo = rl::LayoutMin(), hi = rl::LayoutMax();
				_cursorX = std::clamp(_cursorX + _mouseDX * k, lo.x, hi.x);
				_cursorY = std::clamp(_cursorY - _mouseDY * k, lo.y, hi.y);
				_mouseDX = _mouseDY = 0.0;
				const auto& s = Settings::Get();
				const bool  wheel = _wheelView.load();
				const double cx = wheel ? rl::table::kWheelX : rl::LayoutCentre().x;
				const double cy = wheel ? rl::table::kWheelY : rl::LayoutCentre().y;
				const float  want = wheel ? s.wheelHeight : s.layoutHeight;
				const float  room = (wheel ? _roomWheel : _roomLayout) - 15.0f;  // (a ceiling: the view comes down under it)
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
		auto  table = _table.get();
		auto* root = table ? table->Get3D() : nullptr;
		if (!root) {
			return;
		}
		static const RE::BSFixedString eu("RotorEU"), us("RotorUS");
		auto* node = root->GetObjectByName(_wheel == rl::WheelType::kAmerican ? us : eu);
		if (!node) {
			return;
		}
		// the rotor's x axis at _rotorAngle round the table's z (rows: its axes in the table's space)
		const float c = static_cast<float>(std::cos(_rotorAngle));
		const float s = static_cast<float>(std::sin(_rotorAngle));
		node->local.rotate = util::FromAxes({ c, s, 0.0f }, { -s, c, 0.0f }, { 0.0f, 0.0f, 1.0f });
		RE::NiUpdateData update;
		node->Update(update);
	}

	void Game::PlaceBall(const Frame& a_frame)
	{
		using namespace rl::table;
		double r = 0.0, th = 0.0, z = 0.0;
		bool   show = false;
		if (_phase == Phase::kSpinning && !_plan.samples.empty()) {
			const auto& smp = _plan.samples;
			const float f = _playT * 60.0f;
			const std::size_t i = std::min<std::size_t>(static_cast<std::size_t>(f), smp.size() - 1);
			const std::size_t j = std::min(i + 1, smp.size() - 1);
			const double      u = std::clamp(static_cast<double>(f - static_cast<float>(i)), 0.0, 1.0);
			r = smp[i].r + (smp[j].r - smp[i].r) * u;
			th = smp[i].theta + std::remainder(static_cast<double>(smp[j].theta) - smp[i].theta, 6.283185307) * u;
			z = smp[i].z + (smp[j].z - smp[i].z) * u;
			show = true;
		} else if (_restPocket >= 0) {
			r = kPocketCentre;
			th = _rotorAngle + rl::PocketAngle(_wheel, _restPocket);
			z = kPocketFloorZ + kBallRadius;
			show = true;
		}
		const RE::NiPoint3 pos = ToWorld(a_frame, kWheelX + r * std::cos(th), kWheelY + r * std::sin(th), z);
		if (!_ball && show) {
			_ball = Spawn(Forms::Ball(), pos, 0.0f);
		}
		if (auto ball = _ball.get()) {
			PlaceNode(ball.get(), pos, a_frame.yaw, !show);
		}
	}

	// ---------------------------------------------------------------- the round

	void Game::StartSpin(bool a_byPlayer)
	{
		if (!_table || _phase != Phase::kBetting) {
			if (a_byPlayer && _phase != Phase::kBetting) {
				Say("THE BALL'S ALREADY GOING ROUND");
			}
			return;
		}
		const bool dealer = DealerReady();
		if (a_byPlayer && !dealer && Settings::Get().requireDealer) {
			Say(std::format("NO DEALER: LOOK AT A SETTLER AND PRESS {}", Upper(Settings::Key(Settings::Get().keyDealer))), 2);
			return;
		}
		auto  table = _table.get();
		Frame frame;
		if (!table || !FrameOf(table.get(), frame)) {
			return;
		}
		// a push on the rotor if it's slowed right down, and the ball thrown the other way, from the dealer's side
		std::uniform_real_distribution<double> U(0.0, 1.0);
		if (_rotorSpeed < 1.4) {
			_rotorSpeed = 1.6 + 0.6 * U(_rng);
			Audio::Play(Audio::Bank::kSpin, ToWorld(frame, rl::table::kWheelX, rl::table::kWheelY, rl::table::kFeltZ), 0.8f);
		}
		rl::SpinParams p;
		p.throwAngle = 3.14159265;  // the dealer stands at the wheel's end (-X)
		p.ballSpeed = -(15.0 + 3.0 * U(_rng));
		p.rotorAngle = _rotorAngle;
		p.rotorSpeed = _rotorSpeed;
		_plan = rl::PlanSpin(_wheel, p, _rng);
		log::info("spin: {} ({} pocket {}; {} tries); drop {:.1f}s, rotor {:.1f}s, rest {:.1f}s", rl::NumberName(_plan.number),
			_plan.drawn ? "drawn" : "NOT drawn", _plan.pocket, _plan.tries, _plan.dropTime, _plan.rotorTime, _plan.settleTime);
		_phase = Phase::kSpinning;
		_phaseTime = 0.0f;
		_playT = 0.0f;
		_hopping = false;
		_settleHeard = false;
		_dropSaid = false;
		_restPocket = -1;
		_hudBanner = 1;
		_bannerTitle = "NO MORE BETS";
		_bannerSub = dealer ? std::format("{} SPINS", Upper(_dealer.name)) : std::string("YOU SPIN");
		_bannerTime = std::chrono::steady_clock::now();
		Audio::Play(Audio::Bank::kRoll, ToWorld(frame, rl::table::kWheelX, rl::table::kWheelY, rl::table::kTrackZ), 0.7f, _plan.rotorTime + 0.3f);  // round the track and down the cone, up to the drop
		if (_seated && Settings::Get().wheelViewOnSpin) {
			_wheelView = true;
			_viewBackIn = -1.0f;
		}
		if (auto* d = DealerActor(); d && dealer && Settings::Get().dealerWatchesBall) {
			if (auto ball = _ball.get()) {
				Npc::LookAt(d, ball.get());
				_dealer.watching = true;
			}
		}
	}

	void Game::TickSpin(const Frame& a_frame, float a_dt)
	{
		using namespace rl::table;
		_playT += a_dt;
		const auto& smp = _plan.samples;
		if (smp.empty()) {
			Settle();
			return;
		}
		const std::size_t i = std::min<std::size_t>(static_cast<std::size_t>(_playT * 60.0f), smp.size() - 1);
		const std::size_t j = std::min(i + 1, smp.size() - 1);
		const double      u = std::clamp(static_cast<double>(_playT * 60.0f - static_cast<float>(i)), 0.0, 1.0);
		_rotorAngle = smp[i].rotor + std::remainder(static_cast<double>(smp[j].rotor) - smp[i].rotor, 6.283185307) * u;
		const auto   wheel = ToWorld(a_frame, kWheelX, kWheelY, kRotorEdgeZ);
		// clicks off the diamonds while it's on the cone; once it reaches the rotor, the recorded drop and clatter
		// (cut to how long it bounces about there)
		const double off = smp[i].z - (rl::SurfaceZ(smp[i].r) + kBallRadius);
		if (_playT < _plan.rotorTime) {
			if (off > 0.25 && !_hopping) {
				_hopping = true;
				Audio::Play(Audio::Bank::kRattle, wheel, std::clamp(static_cast<float>(off) / 2.0f, 0.3f, 1.0f));
			} else if (off < 0.05) {
				_hopping = false;
			}
		}
		if (!_settleHeard && _plan.rotorTime > 0.0f && _playT >= _plan.rotorTime) {
			_settleHeard = true;
			const float bouncing = std::max(0.5f, _plan.settleTime - _plan.rotorTime);
			Audio::Play(Audio::Bank::kDrop, wheel, 0.9f, bouncing + 1.2f);
			Audio::Play(Audio::Bank::kSettle, wheel, 0.6f);  // (only there when the recording isn't)
		}
		if (_playT >= smp.back().t) {
			Settle();
		}
	}

	void Game::Settle()
	{
		const int number = _plan.number;
		_restPocket = _plan.pocket;
		if (!_plan.samples.empty()) {
			_rotorAngle = _plan.samples.back().rotor;
		}
		_rotorSpeed = _plan.rotorSpeed;
		_history.push_front(number);
		while (_history.size() > kHistory) {
			_history.pop_back();
		}
		// who won what
		int  yourStake = 0, yourBack = 0, yourWin = 0;
		std::map<RE::TESFormID, int> npcNet;
		_lastRound.clear();
		for (auto& st : _stakes) {
			if (st.owner == 0) {
				yourStake += st.amount;
				_lastRound.emplace_back(st.bet.key, st.amount);
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
			Audio::PlayUI(Audio::Bank::kWin, 0.8f);
		}
		_lastWin = yourBack - yourStake;
		const std::string name = std::format("{} {}", rl::NumberName(number), rl::ColourName(number));
		_hudBanner = yourStake == 0 ? 4 : yourWin > 0 ? 2 : 3;
		_bannerTitle = name;
		_bannerNumber = number;
		if (yourStake == 0) {
			_bannerSub.clear();
		} else if (yourWin > 0) {
			_bannerSub = std::format("YOU WIN {} CAPS", yourWin);
		} else {
			_bannerSub = std::format("YOU LOSE {} CAPS", yourStake);
		}
		_bannerTime = std::chrono::steady_clock::now();
		Say(yourStake == 0 ? name : std::format("{} · {}", name, _bannerSub), yourStake == 0 ? 0 : yourWin > 0 ? 1 : 2);
		for (auto& seat : _seats) {
			if (auto it = npcNet.find(seat.id); it != npcNet.end()) {
				seat.roundNet = it->second;
				if (it->second > 0) {
					Say(std::format("{} WINS {}", Upper(seat.name), it->second), 1);
				}
			}
			if (seat.betThisRound) {
				--seat.rounds;
			}
			seat.betThisRound = false;
		}
		log::info("result {}: you staked {}, got back {}; {} stakes on the table", name, yourStake, yourBack, _stakes.size());
		_phase = Phase::kResult;
		_phaseTime = 0.0f;
		if (_dealer.watching) {
			if (auto* d = DealerActor()) {
				Npc::ClearLookAt(d);
			}
			_dealer.watching = false;
		}
		if (_wheelView && Settings::Get().wheelViewOnSpin) {
			_viewBackIn = 2.0f;  // a look at the ball in its pocket, then back to the layout
		}
	}

	void Game::ClearRound()
	{
		for (auto& st : _stakes) {
			DespawnStake(st);
		}
		_stakes.clear();
		_phase = Phase::kBetting;
		_phaseTime = 0.0f;
		_bettingTime = 0.0f;
		_hudBanner = 0;
		// a changed wheel setting comes in now, between rounds
		const auto setting = Settings::Get().wheel == 1 ? rl::WheelType::kAmerican : rl::WheelType::kEuropean;
		if (setting != _wheel) {
			_wheel = setting;
			_restPocket = -1;
			_lastRound.clear();
			if (auto table = _table.get()) {
				ApplyWheel(table.get(), _wheel);
			}
			Say(_wheel == rl::WheelType::kAmerican ? "AMERICAN WHEEL (0 AND 00)" : "EUROPEAN WHEEL (SINGLE 0)");
		}
	}

	bool Game::HasBets(RE::TESFormID a_owner) const
	{
		return std::ranges::any_of(_stakes, [&](const Stake& s) { return s.owner == a_owner && s.won < 0; });
	}

	int Game::OnTable(RE::TESFormID a_owner) const
	{
		int sum = 0;
		for (const auto& s : _stakes) {
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
		for (auto& s : _stakes) {
			if (s.owner == a_owner && s.bet.key == a_key && s.won < 0) {
				return &s;
			}
		}
		return nullptr;
	}

	bool Game::AddChips(RE::TESFormID a_owner, const rl::Bet& a_bet, int a_amount)
	{
		if (_phase != Phase::kBetting || a_amount <= 0) {
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
			_stakes.push_back(std::move(s));
			existing = &_stakes.back();
		}
		existing->amount += a_amount;
		existing->dirty = true;
		if (auto table = _table.get()) {
			Frame frame;
			if (FrameOf(table.get(), frame)) {
				Audio::Play(Audio::Bank::kChip, ToWorld(frame, a_bet.spot.x, a_bet.spot.y, rl::table::kFeltZ), 0.7f);
			}
		}
		return true;
	}

	void Game::TakeBack(RE::TESFormID a_owner, const std::string& a_key)
	{
		for (auto it = _stakes.begin(); it != _stakes.end(); ++it) {
			if (it->owner == a_owner && it->bet.key == a_key && it->won < 0) {
				if (a_owner == 0) {
					GivePlayer(it->amount);
				} else {
					AddToPurse(a_owner, it->amount);
				}
				DespawnStake(*it);
				_stakes.erase(it);
				return;
			}
		}
	}

	void Game::TakeAllBack(RE::TESFormID a_owner)
	{
		std::vector<std::string> keys;
		for (const auto& s : _stakes) {
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
		for (auto& s : _stakes) {
			if (s.won < 0) {
				if (s.owner == 0) {
					yours += s.amount;
				} else {
					AddToPurse(s.owner, s.amount);
				}
			}
			DespawnStake(s);
		}
		_stakes.clear();
		if (yours > 0) {
			GivePlayer(yours);
			Notify(std::format("Roulette: your {} caps on the table are back", yours));
		}
		if (_phase == Phase::kSpinning) {
			_phase = Phase::kBetting;
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
		for (std::size_t i = 0; i < _seats.size(); ++i) {
			if (_seats[i].id == a_owner) {
				const auto& o = offsets[static_cast<std::size_t>(_seats[i].spot) % offsets.size()];
				return { o.first, o.second };
			}
		}
		return { 1.7, 1.3 };
	}

	void Game::MakeStacks(const Frame& a_frame)
	{
		using namespace rl::table;
		std::uniform_real_distribution<float> spinAngle(0.0f, 6.283f);
		for (auto& st : _stakes) {
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
			auto stack = [&](int a_amount, double a_x, double a_y, std::vector<RE::ObjectRefHandle>& a_out) {
				auto chips = rl::ChipsFor(a_amount);
				if (chips.size() > static_cast<std::size_t>(kMaxStack)) {
					chips.erase(chips.begin() + kMaxStack, chips.end());  // (the biggest are first)
				}
				// biggest at the bottom
				for (std::size_t i = 0; i < chips.size(); ++i) {
					const int idx = static_cast<int>(std::ranges::find(rl::kChipValues, chips[i]) - std::begin(rl::kChipValues));
					const auto pos = ToWorld(a_frame, a_x, a_y, kFeltZ + 0.02 + static_cast<double>(i) * kChipHeight);
					a_out.push_back(Spawn(Forms::Chip(idx), pos, a_frame.yaw + spinAngle(_rng)));
				}
			};
			stack(st.amount, st.bet.spot.x + off.x, st.bet.spot.y + off.y, st.chips);
			if (st.won > 0) {
				stack(st.won, st.bet.spot.x + off.x + kChipRadius * 2.2, st.bet.spot.y + off.y, st.paid);
			}
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
		if (!_table) {
			return;
		}
		// nothing of ours in the save: the chips, the ball and the markers off; your caps on the layout back in your
		// pocket for the save (taken again once it's written); nobody held
		_lifted = true;
		_liftedCaps = OnTable(0);
		GivePlayer(_liftedCaps);
		for (auto& st : _stakes) {
			DespawnStake(st);
		}
		Remove(_ball);
		Remove(_ghost);
		_ghostValue = -1;
		Remove(_dealer.walk.marker);
		for (auto& seat : _seats) {
			Remove(seat.walk.marker);
			if (seat.held) {
				if (auto actor = seat.actor.get()) {
					Npc::SetRestrained(actor->As<RE::Actor>(), false);
				}
				seat.held = false;
			}
		}
		if (_dealer.held) {
			if (auto* d = DealerActor()) {
				Npc::SetRestrained(d, false);
			}
			_dealer.held = false;
		}
		log::info("before save: our objects off, {} caps back to you for the save", _liftedCaps);
	}

	void Game::AfterSave()
	{
		if (!_lifted) {
			return;
		}
		_lifted = false;
		if (_liftedCaps > 0 && !TakeFromPlayer(_liftedCaps)) {
			// (can't happen unless something took caps mid-save) the bets are void
			log::warn("after save: couldn't take the {} caps back; your bets are void", _liftedCaps);
			for (auto it = _stakes.begin(); it != _stakes.end();) {
				it = it->owner == 0 && it->won < 0 ? _stakes.erase(it) : std::next(it);
			}
		}
		_liftedCaps = 0;
		log::info("after save: carrying on");
	}

	void Game::Forget()
	{
		// a load: everything we'd spawned went with the old game; let go of it all without touching it
		_seated = false;
		BlockControls(false);
		_haveEntryView = false;
		_table = {};
		_ball = {};
		_ghost = {};
		_ghostValue = -1;
		_stakes.clear();
		_seats.clear();
		_dealer = {};
		_phase = Phase::kBetting;
		_lifted = false;
		_liftedCaps = 0;
		_hudBanner = 0;
		_log.clear();
		_history.clear();
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
