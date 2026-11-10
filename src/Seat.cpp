// You at the table: sitting down (activating it), the camera over the layout or the wheel, the cursor and your
// chips, and the HUD's picture of it all.

#include "Game.h"

#include "Audio.h"
#include "Forms.h"
#include "Settings.h"
#include "Util.h"
#include "rl/Table.h"

namespace fr
{
	namespace
	{
		constexpr float kDegToRad = 0.017453292f;
		constexpr float kDownPitch = 89.0f * kDegToRad;

		RE::FreeCameraState* FreeCamera()
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			return camera ? static_cast<RE::FreeCameraState*>(camera->cameraStates[RE::CameraStates::kFree].get()) : nullptr;
		}

		bool InFreeCamera()
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			auto* free = FreeCamera();
			return camera && free && camera->currentState.get() == free;
		}

		float& Component(RE::FreeCameraState* a_state, int a_index)
		{
			return a_index == 0 ? a_state->rotation.x : a_state->rotation.y;
		}

		std::string Upper(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return a_s;
		}

		// activations (a table activated by you) and loading screens
		struct ActivateSink final : RE::BSTEventSink<RE::TESActivateEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent& a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override
			{
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (a_event.objectActivated && a_event.actionRef && a_event.actionRef.get() == player &&
					(Forms::IsTable(a_event.objectActivated.get()) || Forms::IsPokerTable(a_event.objectActivated.get()) ||
					 Forms::IsBlackjackTable(a_event.objectActivated.get()))) {
					auto handle = a_event.objectActivated->GetHandle();
					if (auto* tasks = F4SE::GetTaskInterface()) {
						tasks->AddTask([handle]() {
							if (auto ref = handle.get()) {
								Game::Get().OnActivated(ref.get());
							}
						});
					}
				} else if (a_event.objectActivated && a_event.actionRef && a_event.actionRef.get() == player) {
					// one of the poker table's chairs (their activation's blocked: you don't sit in it, with the game's
					// Wait prompt up): sits you at the table, at the seat nearest you
					auto handle = a_event.objectActivated->GetHandle();
					if (auto* tasks = F4SE::GetTaskInterface()) {
						tasks->AddTask([handle]() {
							if (auto ref = handle.get()) {
								if (auto* table = Game::Get().PkChairTable(ref.get())) {
									Game::Get().OnActivated(table);
								}
							}
						});
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// (CommonLibF4 only declares it) the game's own layout: the menu's name, and whether it's opening
		struct MenuOpenCloseEventData
		{
			RE::BSFixedString menuName;
			bool              opening;
		};

		// workshop mode started while you're at a table (Workshop Framework's hotkey, anything else that starts it): out
		// again straight away
		struct WorkshopSink final : RE::BSTEventSink<RE::Workshop::WorkshopModeEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::Workshop::WorkshopModeEvent& a_event, RE::BSTEventSource<RE::Workshop::WorkshopModeEvent>*) override
			{
				if (a_event.start && Game::Get().AtATable()) {
					if (auto* tasks = F4SE::GetTaskInterface()) {
						tasks->AddTask([]() {
							log::info("workshop mode started while you're at a table: leaving it");
							RE::Workshop::RequestExitWorkshop(false);
						});
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		struct LoadingSink final : RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				const auto& e = reinterpret_cast<const MenuOpenCloseEventData&>(a_event);
				if (e.opening && e.menuName.c_str() && std::string_view(e.menuName.c_str()) == "LoadingMenu") {
					Game::Get().OnLoadingStarts();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Game::Listen()
	{
		static ActivateSink activate;
		static LoadingSink  loading;
		static WorkshopSink workshop;
		static bool         registered = false;
		if (registered) {
			return;
		}
		registered = true;
		if (auto* source = RE::TESActivateEvent::GetEventSource()) {
			source->RegisterSink(&activate);
		} else {
			log::warn("no activate event source: tables can't be sat at");
		}
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->RegisterSink<RE::MenuOpenCloseEvent>(&loading);
		}
		RE::Workshop::RegisterForWorkshopModeEvent(&workshop);
		log::info("listening for activations and loading screens");
	}

	void Game::OnActivated(RE::TESObjectREFR* a_ref)
	{
		if (_seated || _pkSeated || _bjSeated || _kitPlacing || IsKitPreview(a_ref) || IsGamePaused()) {
			return;
		}
		if (Forms::IsPokerTable(a_ref)) {
			PkSitDown(a_ref);
			return;
		}
		if (Forms::IsBlackjackTable(a_ref)) {
			BjSitDown(a_ref);
			return;
		}
		if (!Forms::IsTable(a_ref)) {
			return;
		}
		SitDown(a_ref);
	}

	// ---------------------------------------------------------------- sitting down and getting up

	void Game::SitDown(RE::TESObjectREFR* a_table)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!player || !camera || !FreeCamera()) {
			return;
		}
		if (_t.table.get().get() != a_table) {
			if (_other.table.get().get() == a_table) {
				SwapTables();  // the other table in play: it's the one you're at now
			} else {
				if (_t.table) {
					if (!_other.table) {
						SwapTables();  // the one in play carries on as the other
					} else {
						EndSession("you sat down at a third table");
					}
				}
				StartSession(a_table);
			}
		} else {
			Settings::Get().Load();
		}
		if (!InFreeCamera()) {
			camera->ToggleFreeCameraMode(false);
		}
		_entryYaw = player->data.angle.z;
		_entryPitch = player->data.angle.x;
		if (auto* third = static_cast<RE::ThirdPersonState*>(camera->cameraStates[RE::CameraStates::k3rdPerson].get())) {
			_entryFreeRotation = third->freeRotation;
		}
		_haveEntryView = true;
		{
			std::scoped_lock lock(_camMutex);
			_camMapped = false;
			_mapFrames = 0;
			// the view glides over from where your eyes were, rather than jumping
			if (camera->cameraRoot) {
				_camPos = camera->cameraRoot->world.translate;
				_haveCamPos = true;
				_lastCam = std::chrono::steady_clock::now();
			} else {
				_haveCamPos = false;
			}
			const auto c = rl::LayoutCentre();
			_cursorX = c.x;
			_cursorY = c.y;
			_mouseDX = _mouseDY = 0.0;
		}
		_wheelView = _t.phase == Phase::kSpinning && Settings::Get().wheelViewOnSpin;
		_viewBackIn = -1.0f;
		_rlUi = false;
		_rlPush = 0.0;
		_seated = true;
		BlockControls(true);
		QuietNpcs(true);
		const auto& s = Settings::Get();
		const std::string spin = Upper(Settings::Key(s.keySpin > 0 ? s.keySpin : _activateVk.load()));
		log::info("sat down at the table");
		Say(std::format("LEFT BUTTON BETS · {} SPINS · {} LEAVES", spin, Upper(Settings::Key(s.keyLeave))));
	}

	void Game::StandUp()
	{
		if (!_seated) {
			return;
		}
		_seated = false;
		_wheelView = false;
		Audio::SetListenerYaw(0.0f, false);
		HoldPlayerView();
		_haveEntryView = false;
		BlockControls(false);
		QuietNpcs(false);
		if (InFreeCamera()) {
			if (auto* camera = RE::PlayerCamera::GetSingleton()) {
				camera->ToggleFreeCameraMode(false);
			}
		}
		Remove(_ghost);
		_ghostValue = -1;
		log::info("left the table");
	}

	void Game::QuietNpcs(bool a_quiet)
	{
		// the game's own settings for greetings and comments: off while you're at the table (a settler coming over to
		// bet would otherwise start a conversation, which the table's camera and blocked controls can't take part in),
		// back as they were after. From Playable Pool.
		if (a_quiet == _npcsQuiet) {
			return;
		}
		_npcsQuiet = a_quiet;
		static std::unordered_map<std::string, float> was;
		auto* settings = RE::GameSettingCollection::GetSingleton();
		if (!settings) {
			return;
		}
		static constexpr std::pair<const char*, float> kQuiet[] = {
			{ "fAIMinGreetingDistance", 0.0f },
			{ "fAIGreetingTimer", 100000.0f },
			{ "fAIHelloTimer", 100000.0f },
			{ "fCommentOnPlayerActionsTimer", 100000.0f },
		};
		for (const auto& [name, quiet] : kQuiet) {
			auto* setting = settings->GetSetting(name);
			if (!setting) {
				continue;
			}
			if (a_quiet) {
				was[name] = setting->GetFloat();
				setting->SetFloat(quiet);
			} else if (auto it = was.find(name); it != was.end()) {
				setting->SetFloat(it->second);
			}
		}
		log::info("NPC greetings {}", a_quiet ? "off while you're at the table" : "back on");
	}

	void Game::BlockControls(bool a_block)
	{
		if (a_block == _controlsBlocked) {
			return;
		}
		auto* manager = RE::BSInputEnableManager::GetSingleton();
		if (!manager) {
			return;
		}
		if (!_inputLayer && !manager->AllocateNewLayer(_inputLayer, "fo4roulette table")) {
			log::warn("could not get an input layer; controls stay on at the table");
			return;
		}
		using Flag = RE::UserEvents::USER_EVENT_FLAG;
		const auto flags = static_cast<Flag>(
			std::to_underlying(Flag::kMovement) | std::to_underlying(Flag::kActivate) | std::to_underlying(Flag::kPOVSwitch) |
			std::to_underlying(Flag::kFighting) | std::to_underlying(Flag::kSneaking) | std::to_underlying(Flag::kMainFour) |
			std::to_underlying(Flag::kWheelZoom) | std::to_underlying(Flag::kJumping) | std::to_underlying(Flag::kVATS));
		manager->EnableUserEvent(_inputLayer->layerID, flags, !a_block, RE::UserEvents::SENDER_ID::kGameplay);
		_controlsBlocked = a_block;
	}

	void Game::HoldPlayerView()
	{
		if (!_haveEntryView) {
			return;
		}
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			player->data.angle.x = _entryPitch;
			player->data.angle.z = _entryYaw;
		}
		if (auto* camera = RE::PlayerCamera::GetSingleton()) {
			if (auto* third = static_cast<RE::ThirdPersonState*>(camera->cameraStates[RE::CameraStates::k3rdPerson].get())) {
				third->freeRotation = _entryFreeRotation;
			}
		}
	}

	void Game::ToggleView()
	{
		_wheelView = !_wheelView;
		_viewBackIn = -1.0f;
	}

	// ---------------------------------------------------------------- your chips

	std::optional<rl::Bet> Game::HoverBet() const
	{
		std::scoped_lock lock(_camMutex);
		return rl::BetAt(_t.wheel, { _cursorX, _cursorY });
	}

	void Game::PlaceChip()
	{
		if (!_seated || IsGamePaused()) {
			return;
		}
		if (_wheelView) {
			ToggleView();  // a click on the wheel view brings the layout back
			return;
		}
		if (_t.phase != Phase::kBetting) {
			Say("NO MORE BETS", 2);
			return;
		}
		auto bet = HoverBet();
		if (!bet) {
			return;
		}
		AddChips(0, *bet, rl::kChipValues[std::clamp(_chip.load(), 0, 3)]);
	}

	void Game::RemoveChip()
	{
		if (!_seated || IsGamePaused() || _t.phase != Phase::kBetting) {
			return;
		}
		if (auto bet = HoverBet(); bet && StakeFor(0, bet->key)) {
			TakeBack(0, bet->key);
			Audio::PlayUI(Audio::Bank::kChip, 0.5f);
		}
	}

	void Game::Rebet()
	{
		if (!_seated || _t.phase != Phase::kBetting) {
			return;
		}
		if (_t.lastRound.empty()) {
			Say("NO BETS TO REPEAT");
			return;
		}
		int placed = 0;
		for (const auto& [key, amount] : _t.lastRound) {
			if (auto bet = rl::BetFromKey(_t.wheel, key)) {
				if (!AddChips(0, *bet, amount)) {
					break;
				}
				placed += amount;
			}
		}
		if (placed > 0) {
			Say(std::format("SAME AGAIN: {} CAPS", placed));
		}
	}

	void Game::PlaceGhost(const Frame& a_frame)
	{
		using namespace rl::table;
		const int  value = std::clamp(_chip.load(), 0, 3);
		const bool show = Here() && !_wheelView && _t.phase == Phase::kBetting && !_rlUi;
		if (_ghostValue != value) {
			Remove(_ghost);  // a different chip picked: a different object
			_ghostValue = value;
		}
		double x, y, z;
		{
			std::scoped_lock lock(_camMutex);
			x = _cursorX;
			y = _cursorY;
		}
		z = kFeltZ + 2.5;
		if (auto bet = rl::BetAt(_t.wheel, { x, y })) {
			x = bet->spot.x;
			y = bet->spot.y;
			// over the top of your stack there
			if (const Stake* st = StakeFor(0, bet->key)) {
				z += std::min<int>(static_cast<int>(rl::ChipsFor(st->amount).size()), 8) * kChipHeight;
			}
		}
		const auto pos = ToWorld(a_frame, x, y, z);
		if (!_ghost && show) {
			_ghost = Spawn(Forms::Chip(value), pos, a_frame.yaw);
		}
		if (auto g = _ghost.get()) {
			PlaceNode(g.get(), pos, a_frame.yaw, !show);
		}
	}

	// ---------------------------------------------------------------- the camera (camera update thread)

	bool Game::OnLookMouse(const RE::MouseMoveEvent* a_event)
	{
		if ((!_seated && !AtCardTable()) || !a_event) {
			return false;
		}
		std::scoped_lock lock(_camMutex);
		const bool repeat = a_event == _lastMouseEvent && a_event->timeCode == _lastMouseTime;  // the same event reaches both handlers
		_lastMouseEvent = a_event;
		_lastMouseTime = a_event->timeCode;
		if (!repeat) {
			_mouseDX += a_event->mouseInputX;
			_mouseDY += a_event->mouseInputY;
		}
		return true;  // the mouse moves the cursor, never the view
	}

	void Game::BeforeFreeCameraUpdate(RE::FreeCameraState* a_free)
	{
		if ((!_seated && !AtCardTable()) || !a_free) {
			return;
		}
		if (auto* camera = a_free->camera) {
			camera->rotationInput.x = 0.0f;
			camera->rotationInput.y = 0.0f;
		}
		if (auto* controls = RE::PlayerControls::GetSingleton()) {
			auto& d = controls->data;
			d.lookInputVec = {};
			d.lookInputVecNormalized = {};
			d.prevLookVec = {};
			d.rotationSpeedSpring.position = {};
			d.rotationSpeedSpring.velocity = {};
		}
	}

	void Game::OnFreeCameraLook(RE::FreeCameraState* a_free, const RE::BSTPoint2<float>& a_before)
	{
		if ((!_seated && !AtCardTable()) || !a_free) {
			return;
		}
		std::scoped_lock lock(_camMutex);
		if (!_camMapped) {
			return;
		}
		(void)a_before;
		Component(a_free, _yawIndex) = _camYaw;
		Component(a_free, 1 - _yawIndex) = _pitchSign * (_camPitch >= 0.0f ? _camPitch : kDownPitch);
	}

	void Game::OnFreeCameraUpdate(RE::FreeCameraState* a_free)
	{
		if ((!_seated && !AtCardTable()) || !a_free) {
			return;
		}
		std::scoped_lock lock(_camMutex);
		// The free camera keeps two angles; work out once which one is yaw by comparing with where you faced when you
		// sat down (the free camera starts there)
		if (!_camMapped) {
			const float a = a_free->rotation.x;
			const float b = a_free->rotation.y;
			if (a == 0.0f && b == 0.0f && ++_mapFrames < 10) {
				return;
			}
			const float errA = std::abs(util::AngleDiff(a, _entryYaw));
			const float errB = std::abs(util::AngleDiff(b, _entryYaw));
			_yawIndex = errA <= errB ? 0 : 1;
			const float pitchValue = _yawIndex == 0 ? b : a;
			_pitchSign = 1.0f;
			if (std::abs(_entryPitch) > 0.05f && std::abs(pitchValue) > 0.05f && (pitchValue > 0.0f) != (_entryPitch > 0.0f)) {
				_pitchSign = -1.0f;
			}
			log::info("free camera rotation ({:.3f}, {:.3f}) vs your yaw {:.3f} pitch {:.3f}: yaw is value {}, pitch sign {}", a, b, _entryYaw,
				_entryPitch, _yawIndex, _pitchSign);
			_camMapped = true;
		}
		Component(a_free, _yawIndex) = _camYaw;
		Component(a_free, 1 - _yawIndex) = _pitchSign * (_camPitch >= 0.0f ? _camPitch : kDownPitch);
		// glide to where the view should be (the layout, the wheel)
		const auto  now = std::chrono::steady_clock::now();
		const float dt = _haveCamPos ? std::min(0.1f, std::chrono::duration<float>(now - _lastCam).count()) : 0.0f;
		_lastCam = now;
		if (!_haveCamPos) {
			_camPos = _camWant;
			_haveCamPos = true;
		} else {
			_camPos = _camPos + (_camWant - _camPos) * std::min(1.0f, dt * 4.0f);
		}
		a_free->translation = _camPos;
		HoldPlayerView();
	}

	bool Game::FreeCameraTranslation(const RE::FreeCameraState* a_free, RE::NiPoint3& a_out)
	{
		if ((!_seated && !AtCardTable()) || !a_free) {
			return false;
		}
		std::scoped_lock lock(_camMutex);
		if (!_haveCamPos) {
			return false;
		}
		a_out = _camPos;
		return true;
	}

	// ---------------------------------------------------------------- the HUD

	void Game::UpdateHud()
	{
		if (_bjSeated) {
			HudState h;
			BjHud(h);
			std::scoped_lock lock(_hudMutex);
			_hud = std::move(h);
			return;
		}
		if (_pkSeated) {
			HudState h;
			PkHud(h);
			std::scoped_lock lock(_hudMutex);
			_hud = std::move(h);
			return;
		}
		HudState h;
		h.seated = _seated;
		h.nearby = false;  // the HUD is only for when you're sitting at the table
		h.american = _t.wheel == rl::WheelType::kAmerican;
		h.phase = static_cast<int>(_t.phase);
		h.caps = PlayerCaps();
		h.onTable = OnTable(0);
		h.chip = std::clamp(_chip.load(), 0, 3);
		if (_seated && !_wheelView) {
			if (auto bet = HoverBet()) {
				h.hover = bet->Name();
				h.hoverPays = bet->Payout();
				for (const auto& s : _t.stakes) {
					if (s.bet.key == bet->key && s.won < 0) {
						h.hoverAll += s.amount;
						if (s.owner == 0) {
							h.hoverMine += s.amount;
						}
					}
				}
			}
		}
		for (const auto& s : _t.stakes) {
			if (s.owner == 0 && s.amount > 0) {
				h.myBets.push_back({ s.bet.Name(), s.amount, s.won });
			}
		}
		h.spinIn = _t.phase == Phase::kBetting ? _t.spinIn : -1.0f;
		h.spinning = _t.phase != Phase::kBetting;
		for (const auto& seat : _t.seats) {
			if (seat.step == Seat::Step::kPlaying) {
				h.players.push_back(std::format("{}  {} CAPS", Upper(seat.name), Purse(seat.id) + OnTable(seat.id)));
			}
		}
		h.history.assign(_t.history.begin(), _t.history.end());
		h.log = _t.log;
		h.banner = _t.hudBanner;
		h.bannerTitle = _t.bannerTitle;
		h.bannerSub = _t.bannerSub;
		h.bannerNumber = _t.bannerNumber;
		h.bannerAge = std::chrono::duration<float>(std::chrono::steady_clock::now() - _t.bannerTime).count();
		h.wheelView = _wheelView;
		h.lastWin = _t.lastWin;
		const auto& s = Settings::Get();
		auto key = [](int a_vk) { return Upper(Settings::Key(a_vk)); };
		h.keys = std::format("LMB BET · RMB TAKE BACK · {}-{} CHIP · {} SPIN · {} SAME AGAIN · {} CLEAR · {} WHEEL · {} LEAVE", key(s.keyChip[0]),
			key(s.keyChip[3]), key(s.keySpin > 0 ? s.keySpin : _activateVk.load()), key(s.keyRebet), key(s.keyClear), key(s.keyView),
			key(s.keyLeave));
		if (_seated && IsKitTable(_t.table.get().get())) {
			h.keys += std::format(" · {} PACK UP", key(s.keyPackUp));
			h.kitTable = true;
		}
		h.rlKeys = { key(s.keySpin > 0 ? s.keySpin : _activateVk.load()), key(s.keyRebet), key(s.keyClear), key(s.keyView), key(s.keyLeave),
			key(s.keyPackUp), std::format("{}-{}", key(s.keyChip[0]), key(s.keyChip[3])) };
		// the pointer, down on the buttons
		if (_rlUi) {
			std::scoped_lock lock(_camMutex);
			h.pkPtrX = static_cast<float>(_pkPtrX);
			h.pkPtrY = static_cast<float>(_pkPtrY);
		} else {
			h.pkPtrX = h.pkPtrY = -1.0f;
		}
		if (_kitPlacing) {
			h.placing = true;
			h.keys = KitPrompt();
		}
		std::scoped_lock lock(_hudMutex);
		_hud = std::move(h);
	}

	Game::HudState Game::Hud() const
	{
		std::scoped_lock lock(_hudMutex);
		return _hud;
	}
}
