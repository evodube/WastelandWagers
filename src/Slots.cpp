// The slot machines: fo4roulette.esl's Slot Machine (FURN), the Vault-Tec Workshop's machine re-skinned (tools/
// make_slot_machine.py, tools/build_slot_machine.py). Three reels, one payline, seven symbols; a pull costs 1, 5, 10
// or 25 caps and three 7s at 25 win the jackpot (every machine's, growing with every pull; less than 25 on three 7s
// pays 100 times the bet). Settlers sit down at free machines and play with their own caps (the roulette purses);
// activate one that's free and you play it.
//
// The machine's model (its own space): X across, Y towards its back (the stool at Y -47.7, the player facing +Y), Z up.
// The reels are cylinders round X (nodes WorkshopSlotMachine01ReelA/B/CBone at X -4.08, 0.04, 4.15; Y 3.79; Z 103.64),
// their symbol strip twice round each (14 stops of 360/14 degrees). A reel turned by an angle a round X carries what
// was at angle t (atan2(z, y)) to t + a; the payline's at kPayline. Each reel's strip starts (its top symbol's top
// edge) at kStripStart[reel] (the model's UVs, measured in tools: build_slot_machine.py). The lever
// (WorkshopSlotMachine01ArmBone) turns to +50 degrees round X and back, as the machine's own Pull animation does.
// The machine's behaviour graph is switched off in our copy of the model (its BGED renamed): the plugin turns the
// reels and the lever itself, each frame.
//
// A settler's pulls go with their hand: the DLC's sitting animations pull the lever now and then (PoseA_IdleFlavor1,
// whose annotation furnitureIdleFlavor1Slave, 1.17 seconds in, is where the machine's own graph played its Pull). The
// quest watches the sitter's animation for it (RegisterForAnimationEvent) and hands it to LeverDown here: the lever goes
// down and the reels spin from then. When their hand's been idle a while, the pull's played on them
// (fo4rouletteSlotPullIdle: that same animation) and the pull waits for it.

#include "Game.h"

#include "Audio.h"
#include "Forms.h"
#include "Npc.h"
#include "Settings.h"
#include "Util.h"

namespace fr
{
	extern std::mutex                             g_npcMutex;
	extern std::unordered_map<RE::TESFormID, int> g_purses;

	int Game::s_jackpot = 0;

	namespace
	{
		using Clock = std::chrono::steady_clock;

		// the hands on levers, from the script thread (LeverDown), for the tick
		std::mutex                                               g_leverLock;
		std::vector<std::pair<RE::TESFormID, Clock::time_point>> g_levers;

		void LeverDown(std::monostate, std::int32_t a_actor)
		{
			std::scoped_lock lock(g_leverLock);
			if (g_levers.size() < 64) {
				g_levers.emplace_back(static_cast<RE::TESFormID>(a_actor), Clock::now());
			}
		}

		// the strip, top to bottom (tools/make_slot_machine.py SYMBOLS)
		enum Sym : int
		{
			kCherry,
			kBell,
			kOrange,
			kBar,
			kSeven,
			kLemon,
			kPlum,
			kSyms
		};
		constexpr std::array<const char*, kSyms> kNames{ "CHERRY", "BELL", "ORANGE", "BAR", "7", "LEMON", "PLUM" };
		// each reel's stops: how many of each symbol (out of 32): what the odds come from
		constexpr std::array<int, kSyms> kWeights{ 5, 4, 6, 3, 2, 6, 6 };
		constexpr float                  kSymbol = 180.0f / 7.0f;  // degrees a symbol
		constexpr float                  kPayline = 157.0f;
		constexpr std::array<float, 3>   kStripStart{ 39.0f, 90.0f, 63.5f };
		constexpr std::array<const char*, 3> kReelNodes{ "WorkshopSlotMachine01ReelABone", "WorkshopSlotMachine01ReelBBone",
			"WorkshopSlotMachine01ReelCBone" };
		constexpr const char*            kArmNode = "WorkshopSlotMachine01ArmBone";
		constexpr std::array<int, 4>     kBets{ 1, 5, 10, 25 };
		constexpr int                    kMaxBet = 25;
		constexpr int                    kJackpotSeed = 1000;
		constexpr float                  kSpinUp = 0.35f;   // the reels start this long after the lever's pulled
		constexpr float                  kAccel = 0.15f;
		// where a settler stands to sit down, and where your view is (the machine's space)
		constexpr float kStandY = -78.0f;
		constexpr float kArrived = 30.0f;

		float Distance2D(const RE::NiPoint3& a, const RE::NiPoint3& b) { return std::hypot(a.x - b.x, a.y - b.y); }

		std::string Upper(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return a_s;
		}

		std::string Commas(int a_n)
		{
			std::string s = std::to_string(std::abs(a_n));
			for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) {
				s.insert(static_cast<std::size_t>(i), ",");
			}
			return a_n < 0 ? "-" + s : s;
		}

		bool Usable(RE::Actor* a_actor)
		{
			return a_actor && !a_actor->IsDisabled() && !a_actor->IsDead(false) && a_actor->Get3D() &&
			       !RE::PowerArmor::ActorInPowerArmor(*a_actor);
		}

		bool InFreeCamera()
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			auto* free = camera ? camera->cameraStates[RE::CameraStates::kFree].get() : nullptr;
			return camera && free && camera->currentState.get() == free;
		}

		int TakePurse(RE::TESFormID a_id, int a_most)
		{
			std::scoped_lock lock(g_npcMutex);
			auto             it = g_purses.find(a_id);
			if (it == g_purses.end()) {
				return 0;
			}
			const int take = std::min(it->second, a_most);
			it->second -= take;
			return take;
		}

		// the angle a reel's at with symbol a_sym on the payline (mod 180: the strip's twice round)
		float StopAngle(int a_reel, int a_sym)
		{
			const float centre = kStripStart[a_reel] + (static_cast<float>(a_sym) + 0.5f) * kSymbol;
			float       a = std::fmod(kPayline - centre, 180.0f);
			return a < 0.0f ? a + 180.0f : a;
		}

		float Mod180(float a_a)
		{
			const float m = std::fmod(a_a, 180.0f);
			return m < 0.0f ? m + 180.0f : m;
		}

		// what a line pays, times the bet (-1: the jackpot)
		int Pays(const std::array<int, 3>& a_r)
		{
			if (a_r[0] == a_r[1] && a_r[1] == a_r[2]) {
				switch (a_r[0]) {
				case kSeven: return -1;
				case kBar: return 50;
				case kBell: return 20;
				case kCherry: return 15;
				case kPlum: return 12;
				case kOrange: return 10;
				case kLemon: return 8;
				default: return 0;
				}
			}
			const int cherries = static_cast<int>(std::ranges::count(a_r, static_cast<int>(kCherry)));
			return cherries == 2 ? 4 : cherries == 1 ? 1 : 0;
		}

		// a reel's angle t seconds into a pull
		template <class Spin>  // (Game::SlotSpin: private to Game)
		float ReelAt(const Spin& a_s, int a_i, float a_t)
		{
			if (a_t <= kSpinUp) {
				return a_s.from[a_i];
			}
			if (a_t >= a_s.stopAt[a_i]) {
				const float u = std::clamp((a_t - a_s.stopAt[a_i]) / std::max(0.01f, a_s.brake[a_i]), 0.0f, 1.0f);
				// braking at a steady rate (its speed on the way in matching the spin's), and a small settle back at the end
				float e = 1.0f - (1.0f - u) * (1.0f - u);
				const float settle = u > 0.85f ? std::sin((u - 0.85f) / 0.15f * 3.14159f) * 1.5f : 0.0f;
				return a_s.stopFrom[a_i] + a_s.travel[a_i] * e + settle;
			}
			const float t = a_t - kSpinUp;
			if (t < kAccel) {
				return a_s.from[a_i] + a_s.speed[a_i] * t * t / (2.0f * kAccel);
			}
			return a_s.from[a_i] + a_s.speed[a_i] * (kAccel * 0.5f + (t - kAccel));
		}

		float LeverAt(float a_t)
		{
			// as the machine's own Pull: down to 50 by half a second, held, back by 0.9
			if (a_t <= 0.0f || a_t >= 0.9f) {
				return 0.0f;
			}
			if (a_t < 0.5f) {
				const float u = a_t / 0.5f;
				return 50.0f * (1.0f - (1.0f - u) * (1.0f - u));
			}
			if (a_t < 0.73f) {
				return 50.0f;
			}
			const float u = (a_t - 0.73f) / 0.17f;
			return 50.0f * (1.0f - u * u);
		}

		RE::NiMatrix3 RoundX(float a_deg)
		{
			const float r = a_deg * 0.0174533f;
			const float c = std::cos(r), s = std::sin(r);
			// (rows: the node's axes in its parent's space: Y carried to (0, c, s), so what was at t is at t + a)
			return util::FromAxes({ 1.0f, 0.0f, 0.0f }, { 0.0f, c, s }, { 0.0f, -s, c });
		}

		void Still(RE::NiAVObject* a_obj)
		{
			if (!a_obj) {
				return;
			}
			a_obj->previousWorld = a_obj->world;
			if (auto* node = a_obj->IsNode()) {
				for (auto& child : node->children) {
					Still(child.get());
				}
			}
		}

		void Turn(RE::NiAVObject* a_root, const char* a_name, float a_deg, bool a_still)
		{
			static std::unordered_map<std::string, RE::BSFixedString> names;
			auto it = names.find(a_name);
			if (it == names.end()) {
				it = names.emplace(a_name, RE::BSFixedString(a_name)).first;
			}
			if (auto* node = a_root->GetObjectByName(it->second)) {
				node->local.rotate = RoundX(a_deg);
				RE::NiUpdateData update;
				node->Update(update);
				if (a_still) {
					Still(node);  // (the upscaler / frame generation: a still reel's not moving, no shake)
				}
			}
		}
	}

	bool RegisterNatives(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			return false;
		}
		a_vm->BindNativeMethod("fo4rouletteQuest", "LeverDown", LeverDown, true);
		log::info("papyrus: fo4rouletteQuest.LeverDown bound");
		return true;
	}

	// ---------------------------------------------------------------- the machines

	bool Game::SlFrame(int a_i, Frame& a_out) const
	{
		if (a_i < 0 || a_i >= static_cast<int>(_sl.size())) {
			return false;
		}
		auto ref = _sl[a_i].ref.get();
		return ref && ref->Get3D() && FrameOf(ref.get(), a_out);
	}

	bool Game::SlHasSeat(RE::TESFormID a_id) const
	{
		return a_id != 0 && std::ranges::any_of(_sl, [&](const SlotMachine& m) { return m.step != 0 && !m.you && m.id == a_id; });
	}

	void Game::SlScan()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->GetParentCell()) {
			return;
		}
		const auto& s = Settings::Get();
		if (s_jackpot < kJackpotSeed) {
			s_jackpot = kJackpotSeed;
		}
		// the ones gone or left behind
		for (int i = static_cast<int>(_sl.size()) - 1; i >= 0; --i) {
			auto       ref = _sl[i].ref.get();
			const bool gone = !ref || ref->IsDisabled() || !ref->Get3D();
			if (!gone && (ref->data.location - player->data.location).Length() <= s.activeDistance + 400.0f) {
				continue;
			}
			if (_slSeated && _slYou == i) {
				SlStandUp();
			}
			if (_sl[i].spin.on) {
				SlPay(i);  // (a pull under way: settled now)
			}
			SlRelease(i, gone ? "the machine's gone" : "you walked away");
			_sl.erase(_sl.begin() + i);
			if (_slYou > i) {
				--_slYou;
			} else if (_slYou == i) {
				_slYou = -1;
			}
		}
		// the new ones
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(player->data.location, s.activeDistance, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && Forms::IsSlotMachine(a_ref) && !a_ref->IsDisabled() && a_ref->Get3D() && !IsKitPreview(a_ref) &&
					std::ranges::none_of(_sl, [&](const SlotMachine& m) { return m.ref.get().get() == a_ref; })) {
					SlotMachine m;
					m.ref = a_ref->GetHandle();
					// the reels where the model has them: the strip's angle 0
					_sl.push_back(std::move(m));
					log::info("slots: a machine in play ({:08X})", a_ref->GetFormID());
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		for (auto& m : _sl) {
			if (!m.claimed && m.step == 0) {
				if (auto ref = m.ref.get()) {
					Npc::ClaimChair(ref.get());  // (yours while nobody's at it: nobody passing sits down at it; activating it
					                            // is caught: you play it)
					m.claimed = true;
				}
			}
		}
	}

	void Game::SlEnd(const char* a_why)
	{
		if (_slSeated) {
			SlStandUp();
		}
		for (int i = 0; i < static_cast<int>(_sl.size()); ++i) {
			if (_sl[i].spin.on) {
				SlPay(i);
			}
			SlRelease(i, a_why);
		}
		_sl.clear();
		_slYou = -1;
	}

	void Game::SlBeforeSave()
	{
		// a pull under way: paid now (nothing's spawned: the reels are the model's own nodes)
		for (int i = 0; i < static_cast<int>(_sl.size()); ++i) {
			if (_sl[i].spin.on) {
				SlPay(i);
			}
		}
	}

	void Game::SlRelease(int a_i, const char* a_why)
	{
		auto& m = _sl[a_i];
		if (m.step == 0 || m.you) {
			m.you = false;
			return;
		}
		auto  ref = m.actor.get();
		auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (actor) {
			if (auto* proc = actor->currentProcess; proc && proc->GetOccupiedFurniture()) {
				actor->InitiateGetUpPackage();
			}
			Npc::Leave(actor);
			if (m.watched != 0) {
				Npc::WatchLever(actor, false);
			}
		}
		Remove(m.walk.marker);
		if (auto machine = m.ref.get()) {
			Npc::ClaimChair(machine.get());
		}
		log::info("slots: {} gets up ({})", m.name, a_why ? a_why : "");
		if (a_why && std::string_view(a_why) == "done") {
			SlSay(std::format("{} LEAVES A MACHINE", Upper(m.name)));
		}
		m.actor = {};
		m.id = 0;
		m.name.clear();
		m.walk = Walker{};
		m.step = 0;
		m.sitWas = -1;
		m.pend = -1.0f;
		m.watched = 0;
	}

	// ---------------------------------------------------------------- settlers

	void Game::SlFindPlayer()
	{
		const auto& s = Settings::Get();
		if (!s.slSettlers) {
			return;
		}
		if (const auto* you = RE::PlayerCharacter::GetSingleton(); you && you->IsInCombat()) {
			return;
		}
		int playing = 0;
		std::vector<int> free;
		for (int i = 0; i < static_cast<int>(_sl.size()); ++i) {
			if (_sl[i].step != 0 && !_sl[i].you) {
				++playing;
			} else if (_sl[i].step == 0 && !_sl[i].you && !(_slSeated && _slYou == i)) {
				free.push_back(i);
			}
		}
		if (free.empty() || playing >= s.slMaxSettlers) {
			return;
		}
		const int i = free[std::uniform_int_distribution<std::size_t>(0, free.size() - 1)(_rng)];
		auto      machine = _sl[i].ref.get();
		if (!machine) {
			return;
		}
		auto*        player = RE::PlayerCharacter::GetSingleton();
		static auto* npcType = RE::TESForm::GetFormByID<RE::BGSKeyword>(Forms::kActorTypeNPC);
		std::vector<std::pair<float, RE::Actor*>> found;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(machine->data.location, s.bettorRange, [&](RE::TESObjectREFR* a_ref) {
				auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
				if (!Usable(actor) || actor == player || actor->IsChild() || actor->IsInCombat() || Npc::Busy(actor) || Npc::AtPool(actor) ||
					(player && actor->GetHostileToActor(player)) || (npcType && !actor->HasKeywordHelper(npcType, nullptr)) ||
					SlHasSeat(actor->GetFormID()) || BjHasSeat(actor->GetFormID()) || PkHasSeat(actor->GetFormID()) ||
					IsCroupier(actor->GetFormID()) ||
					std::ranges::any_of(_t.seats, [&](const Seat& st) { return st.id == actor->GetFormID(); }) ||
					std::ranges::any_of(_other.seats, [&](const Seat& st) { return st.id == actor->GetFormID(); })) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				if (Purse(actor->GetFormID()) < 10) {
					AddToPurse(actor->GetFormID(), 100 + static_cast<int>((actor->GetFormID() * 2654435761u) % 200u));
				}
				found.emplace_back((actor->data.location - machine->data.location).Length(), actor);
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		if (found.empty()) {
			return;
		}
		std::ranges::sort(found, {}, &std::pair<float, RE::Actor*>::first);
		auto* actor = found[std::uniform_int_distribution<std::size_t>(0, std::min<std::size_t>(found.size(), 4) - 1)(_rng)].second;
		auto& m = _sl[i];
		m.actor = actor->GetHandle();
		m.id = actor->GetFormID();
		m.name = actor->GetDisplayFullName();
		m.step = 1;
		m.walk = Walker{};
		m.pulls = std::uniform_int_distribution<int>(6, 18)(_rng);
		m.next = 2.0f;
		m.pend = -1.0f;
		m.idleHand = 0.0f;
		m.askAt = std::uniform_real_distribution<float>(3.0f, 6.0f)(_rng);
		m.watched = 0;
		m.sitCheck = 0.0f;
		m.sitTries = 0;
		m.sitWas = -1;
		log::info("slots: {} ({:08X}) comes to play a machine ({} caps in their purse)", m.name, m.id, Purse(m.id));
		SlSay(std::format("{} SITS DOWN AT A MACHINE", Upper(m.name)));
	}

	void Game::SlTickPlayer(int a_i, const Frame& a_frame, float a_dt)
	{
		auto& m = _sl[a_i];
		if (m.step == 0 || m.you) {
			return;
		}
		auto  ref = m.actor.get();
		auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
		auto  machine = m.ref.get();
		const auto* you = RE::PlayerCharacter::GetSingleton();
		if (!Usable(actor) || actor->IsInCombat() || (you && you->IsInCombat()) || Npc::AtPool(actor) || !machine) {
			SlRelease(a_i, "gone");
			return;
		}
		if (m.step == 1) {
			// over to the machine's front, then sat on its stool
			const float heading = HeadingOf(a_frame, 0.0, 1.0);
			const auto  spot = ToWorld(a_frame, 0.0, kStandY, 0.0);
			if (Distance2D(actor->data.location, spot) < 60.0f || WalkTo(actor, m.walk, a_frame, spot, heading, a_dt, false, 15.0f)) {
				m.step = 2;
				m.sitCheck = 0.0f;
				m.sitTries = 0;
				Npc::SitOn(actor, machine.get());
			}
			return;
		}
		// sat at it: kept there (the quest's Sit package, the machine as its furniture), sat again if they're up
		TrackSit("slots", m.name, actor, machine.get(), m.sitWas);
		m.sitCheck -= a_dt;
		if (m.sitCheck <= 0.0f) {
			m.sitCheck = 3.0f;
			const auto sit = actor->DoGetSitSleepState();
			const bool down = sit == RE::SIT_SLEEP_STATE::kWantToSit || sit == RE::SIT_SLEEP_STATE::kWaitingForSitAnim ||
			                  sit == RE::SIT_SLEEP_STATE::kIsSitting;
			if (down) {
				m.sitTries = 0;
			} else {
				if (++m.sitTries > 2) {
					log::info("[sit] slots: {} still isn't at their machine (sit state {}): moved onto it and sat", m.name, static_cast<int>(sit));
					Npc::MoveTo(actor, machine.get());
					m.sitTries = 0;
				} else {
					log::info("[sit] slots: {} isn't at their machine (sit state {}): sat down again", m.name, static_cast<int>(sit));
				}
				Npc::SitOn(actor, machine.get());
			}
		}
		// their hand: the quest watching for it on the lever (again now and then: a watch goes with their 3D)
		m.watchAgain -= a_dt;
		if (m.watched != m.id || m.watchAgain <= 0.0f) {
			Npc::WatchLever(actor, true);
			m.watched = m.id;
			m.watchAgain = 20.0f;
		}
		if (m.spin.on || m.pend >= 0.0f) {
			return;
		}
		if (m.pulls <= 0 || Purse(m.id) < 1) {
			// played out: up once the last pull's settled
			m.next -= a_dt;
			if (m.next <= 0.0f) {
				SlRelease(a_i, "done");
			}
			return;
		}
		// their own idle pulls the lever now and then (SlLever); a while without, a pull's played on them
		if (actor->DoGetSitSleepState() != RE::SIT_SLEEP_STATE::kIsSitting) {
			m.idleHand = 0.0f;
			return;
		}
		m.idleHand += a_dt;
		if (m.idleHand < m.askAt) {
			return;
		}
		m.idleHand = 0.0f;
		m.askAt = std::uniform_real_distribution<float>(4.0f, 8.0f)(_rng);
		auto* pull = Forms::SlotPull();
		if (!pull) {
			// (an older fo4roulette.esl: no pull idle) the lever on its own
			if (const int bet = SlNpcBet(a_i); bet > 0) {
				SlPull(a_i, bet, false, Clock::now());
			}
			return;
		}
		m.pend = 0.0f;
		Npc::PlayIdle(actor, pull->GetFormID());
	}

	int Game::SlNpcBet(int a_i)
	{
		// a pull: mostly a cap or five, now and then ten (0: they're done)
		auto& m = _sl[a_i];
		if (m.pulls <= 0 || Purse(m.id) < 1) {
			return 0;
		}
		const float roll = std::uniform_real_distribution<float>(0.0f, 1.0f)(_rng);
		const int   want = roll < 0.45f ? 1 : roll < 0.9f ? 5 : 10;
		const int   bet = TakePurse(m.id, want);
		if (bet > 0) {
			--m.pulls;
			m.next = 2.0f;
		}
		return bet;
	}

	void Game::SlLever(RE::TESFormID a_who, Clock::time_point a_at)
	{
		// a settler's hand put a machine's lever down
		for (int i = 0; i < static_cast<int>(_sl.size()); ++i) {
			auto& m = _sl[i];
			if (m.you || m.step != 2 || m.id != a_who) {
				continue;
			}
			m.idleHand = 0.0f;
			m.pend = -1.0f;
			if (m.spin.on) {
				m.dry = a_at;  // (the reels still going: the lever goes with their hand, nothing more)
				m.dryOn = true;
				return;
			}
			if (const int bet = SlNpcBet(i); bet > 0) {
				SlPull(i, bet, false, a_at);
			} else {
				m.dry = a_at;  // (played out: the lever goes with their hand, nothing on it)
				m.dryOn = true;
			}
			return;
		}
	}

	// ---------------------------------------------------------------- a pull

	void Game::SlPull(int a_i, int a_bet, bool a_you, Clock::time_point a_at)
	{
		auto& m = _sl[a_i];
		auto& sp = m.spin;
		sp = SlotSpin{};
		sp.on = true;
		sp.start = std::min(a_at, Clock::now());
		m.dryOn = false;
		sp.bet = a_bet;
		sp.you = a_you;
		std::discrete_distribution<int> reel(kWeights.begin(), kWeights.end());
		for (int r = 0; r < 3; ++r) {
			sp.result[r] = reel(_rng);
			sp.from[r] = m.angle[r];
			sp.speed[r] = 1000.0f - 50.0f * static_cast<float>(r);
			sp.stopAt[r] = 1.45f + 0.5f * static_cast<float>(r) + std::uniform_real_distribution<float>(0.0f, 0.15f)(_rng);
			// braking from where it'll be then, over at least a third of a turn, to land on its symbol
			const float at = ReelAt(sp, r, sp.stopAt[r]);
			const float want = StopAngle(r, sp.result[r]);
			float       d = Mod180(want - Mod180(at));
			while (d < sp.speed[r] * 0.33f) {
				d += 180.0f;
			}
			sp.stopFrom[r] = at;
			sp.travel[r] = d;
			sp.brake[r] = 2.0f * d / sp.speed[r];
		}
		sp.done = 0.0f;
		for (int r = 0; r < 3; ++r) {
			sp.done = std::max(sp.done, sp.stopAt[r] + sp.brake[r]);
		}
		// every pull feeds the jackpot a little
		s_jackpot += std::max(1, a_bet * 4 / 100);
		Frame frame;
		if (SlFrame(a_i, frame)) {
			const auto at = ToWorld(frame, 0.0, 0.0, 100.0);
			Audio::Play(Audio::Bank::kSlotPull, at, a_you ? 0.9f : 0.6f);
			Audio::Play(Audio::Bank::kSlotReel, at, a_you ? 0.55f : 0.35f, sp.done - kSpinUp + 0.1f);
		}
		log::info("slots: {} pulls for {} (lands {} {} {})", a_you ? std::string("you") : m.name, a_bet, kNames[sp.result[0]],
			kNames[sp.result[1]], kNames[sp.result[2]]);
	}

	void Game::SlPay(int a_i)
	{
		auto& m = _sl[a_i];
		auto& sp = m.spin;
		if (!sp.on) {
			return;
		}
		sp.on = false;
		for (int r = 0; r < 3; ++r) {
			m.angle[r] = Mod180(StopAngle(r, sp.result[r]));
		}
		m.lever = 0.0f;
		const int   x = Pays(sp.result);
		int         won = 0;
		bool        jackpot = false;
		if (x < 0) {
			if (sp.bet >= kMaxBet) {
				won = s_jackpot;
				jackpot = true;
				s_jackpot = kJackpotSeed;
			} else {
				won = sp.bet * 100;
			}
		} else {
			won = sp.bet * x;
		}
		const std::string line = std::format("{} {} {}", kNames[sp.result[0]], kNames[sp.result[1]], kNames[sp.result[2]]);
		const std::string who = sp.you ? std::string("YOU") : Upper(m.name);
		if (sp.you) {
			if (won > 0) {
				GivePlayer(won);
			}
			_slBanner = won > 0 ? 2 : 3;
			_slBannerAge = 0.0f;
			_slBannerTitle = jackpot ? std::format("JACKPOT · {}", Commas(won)) : won > 0 ? std::format("YOU WIN {}", Commas(won)) : line;
			_slBannerSub = won > 0 ? line : std::string("NO WIN");
		} else if (m.id != 0) {
			if (won > 0) {
				AddToPurse(m.id, won);
			}
			auto  ref = m.actor.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (actor) {
				if (won > 0) {
					Voice(actor, jackpot || x >= 20 ? 2 : won > sp.bet ? 1 : 0);
				} else if (std::uniform_real_distribution<float>(0.0f, 1.0f)(_rng) < 0.3f) {
					if (auto* idle = Forms::SlotDisappointed()) {
						Npc::PlayIdle(actor, idle->GetFormID());
					}
					Voice(actor, -1);
				}
			}
		}
		if (won > 0) {
			SlSay(jackpot ? std::format("{} HIT THE JACKPOT · {}", who, Commas(won)) : std::format("{} · {} WIN{} {}", line, who, sp.you ? "" : "S", Commas(won)),
				sp.you ? 1 : 0);
			Frame frame;
			if (SlFrame(a_i, frame)) {
				Audio::Play(Audio::Bank::kSlotWin, ToWorld(frame, 0.0, 0.0, 100.0), sp.you ? 0.9f : 0.6f);
			}
		} else if (sp.you) {
			SlSay(std::format("{} · NO WIN", line), 0);
		}
		log::info("slots: {} lands {} for {}: {}", sp.you ? std::string("you") : m.name, line, sp.bet, won);
	}

	// ---------------------------------------------------------------- the tick and the frame

	void Game::TickSlots(float a_dt)
	{
		if (_sl.empty()) {
			return;
		}
		_slAge += a_dt;
		_slBannerAge += a_dt;
		_slFind -= a_dt;
		if (_slFind <= 0.0f) {
			_slFind = std::uniform_real_distribution<float>(8.0f, 16.0f)(_rng);
			SlFindPlayer();
		}
		// the hands on levers since the last tick
		std::vector<std::pair<RE::TESFormID, Clock::time_point>> levers;
		{
			std::scoped_lock lock(g_leverLock);
			levers.swap(g_levers);
		}
		for (const auto& [who, at] : levers) {
			SlLever(who, at);
		}
		const auto now = Clock::now();
		for (int i = 0; i < static_cast<int>(_sl.size()); ++i) {
			auto& m = _sl[i];
			Frame frame;
			if (!SlFrame(i, frame)) {
				continue;
			}
			// a pull asked for whose hand never came (the idle didn't play): the lever on its own
			if (m.pend >= 0.0f) {
				m.pend += a_dt;
				if (m.pend > 2.5f) {
					m.pend = -1.0f;
					if (!m.spin.on && m.step == 2 && !m.you) {
						log::info("slots: {}'s pull idle didn't put the lever down: pulled anyway", m.name);
						if (const int bet = SlNpcBet(i); bet > 0) {
							SlPull(i, bet, false, now);
						}
					}
				}
			}
			if (m.dryOn && std::chrono::duration<float>(now - m.dry).count() > 1.0f) {
				m.dryOn = false;
			}
			SlTickPlayer(i, frame, a_dt);
			auto& sp = m.spin;
			if (sp.on) {
				const float t = std::chrono::duration<float>(now - sp.start).count();
				for (int r = 0; r < 3; ++r) {
					if (!sp.stopped[r] && t >= sp.stopAt[r] + sp.brake[r] * 0.85f) {
						sp.stopped[r] = true;
						Audio::Play(Audio::Bank::kSlotStop, ToWorld(frame, -4.0 + 4.0 * r, 0.0, 100.0), sp.you ? 0.7f : 0.45f);
					}
				}
				if (t >= sp.done + 0.25f) {
					SlPay(i);
				}
			}
		}
		if (_slSeated && _slYou >= 0) {
			Frame frame;
			if (SlFrame(_slYou, frame)) {
				SlCamera(frame, a_dt);
			}
		}
	}

	void Game::SlAnimate()
	{
		// every machine's reels and lever where they are now (a pull: from its start's time, so the camera's update
		// and the tick agree)
		const auto now = Clock::now();
		for (auto& m : _sl) {
			auto ref = m.ref.get();
			auto* root = ref ? ref->Get3D() : nullptr;
			if (!root) {
				continue;
			}
			std::array<float, 3> a = m.angle;
			float                lever = 0.0f;
			if (m.spin.on) {
				const float t = std::chrono::duration<float>(now - m.spin.start).count();
				for (int r = 0; r < 3; ++r) {
					a[r] = ReelAt(m.spin, r, t);
				}
				lever = LeverAt(t);
			}
			if (m.dryOn) {
				lever = std::max(lever, LeverAt(std::chrono::duration<float>(now - m.dry).count()));
			}
			for (int r = 0; r < 3; ++r) {
				Turn(root, kReelNodes[r], a[r], !m.spin.on);
			}
			Turn(root, kArmNode, lever, !m.spin.on && !m.dryOn);
		}
	}

	// ---------------------------------------------------------------- you

	void Game::SlSitDown(RE::TESObjectREFR* a_machine)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!player || !camera || AtATable()) {
			return;
		}
		Settings::Get().Load();
		SlScan();
		int i = -1;
		for (int k = 0; k < static_cast<int>(_sl.size()); ++k) {
			if (_sl[k].ref.get().get() == a_machine) {
				i = k;
			}
		}
		if (i < 0) {
			return;
		}
		auto& m = _sl[i];
		if (m.step != 0 && !m.you) {
			Notify(std::format("{} is playing that machine", m.name));
			return;
		}
		m.you = true;
		_slYou = i;
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
			if (camera->cameraRoot) {
				_camPos = camera->cameraRoot->world.translate;
				_haveCamPos = true;
				_lastCam = std::chrono::steady_clock::now();
			} else {
				_haveCamPos = false;
			}
			_mouseDX = _mouseDY = 0.0;
		}
		_slAge = 0.0f;
		_padUsed = false;
		_pkPtrX = _pkPtrY = -1.0;
		_slSeated = true;
		BlockControls(true);
		QuietNpcs(true);
		log::info("slots: you sit down at a machine");
		SlSay(std::format("YOU SIT DOWN · {} A PULL", _slBet));
	}

	void Game::SlStandUp()
	{
		_afterTable = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + 1500;
		if (!_slSeated) {
			return;
		}
		_slSeated = false;
		if (_slYou >= 0 && _slYou < static_cast<int>(_sl.size())) {
			_sl[_slYou].you = false;  // (a pull under way: still yours, paid as it lands)
		}
		_slYou = -1;
		_camPitch = -1.0f;
		HoldPlayerView();
		_haveEntryView = false;
		BlockControls(false);
		QuietNpcs(false);
		PkHoldMenuKeys(false);
		if (auto* camera = RE::PlayerCamera::GetSingleton(); camera && InFreeCamera()) {
			camera->ToggleFreeCameraMode(false);
		}
		log::info("slots: you get up");
	}

	void Game::SlKey(int a_key)
	{
		// 90-93 the bets (1, 5, 10, 25), 94 pull, 95 get up, 96 the bet down, 97 the bet up
		if (_slYou < 0 || _slYou >= static_cast<int>(_sl.size())) {
			return;
		}
		auto& m = _sl[_slYou];
		switch (a_key) {
		case 90:
		case 91:
		case 92:
		case 93:
			if (!m.spin.on) {
				_slBet = kBets[a_key - 90];
			}
			return;
		case 96:
		case 97:
			if (!m.spin.on) {
				auto it = std::ranges::find(kBets, _slBet);
				int  k = it == kBets.end() ? 1 : static_cast<int>(it - kBets.begin());
				k = std::clamp(k + (a_key == 97 ? 1 : -1), 0, 3);
				_slBet = kBets[k];
			}
			return;
		case 94:
			{
				if (m.spin.on) {
					return;
				}
				if (!TakeFromPlayer(_slBet)) {
					SlSay("NOT ENOUGH CAPS", 2);
					return;
				}
				_slBanner = 0;
				SlPull(_slYou, _slBet, true, Clock::now());
				return;
			}
		case 95:
			SlStandUp();
			return;
		default:
			return;
		}
	}

	void Game::SlCamera(const Frame& a_frame, float a_dt)
	{
		(void)a_dt;
		// from the stool, a little above the reels, looking at them
		const auto eye = ToWorld(a_frame, 0.0, -36.0, 113.0);
		std::scoped_lock lock(_camMutex);
		float sw, sh;
		{
			std::scoped_lock blk(_btnMutex);
			sw = _scrW;
			sh = _scrH;
		}
		const auto& cs = Settings::Get();
		const double k = sh / 1080.0 * cs.cursorSpeed / 0.07;
		if (_pkPtrX < 0.0) {
			_pkPtrX = sw * 0.5;
			_pkPtrY = sh * 0.8;
		}
		if (_mouseDX != 0.0 || _mouseDY != 0.0) {
			_padUsed = false;
		}
		_pkPtrX = std::clamp(_pkPtrX + _mouseDX * k, 0.0, static_cast<double>(sw - 1.0f));
		_pkPtrY = std::clamp(_pkPtrY + _mouseDY * k, 0.0, static_cast<double>(sh - 1.0f));
		_mouseDX = _mouseDY = 0.0;
		_camWant = eye;
		_camYaw = HeadingOf(a_frame, 0.0, 1.0);
		_camPitch = std::atan2(113.0f - 101.0f, 36.0f + 1.0f);
	}

	void Game::SlSay(const std::string& a_text, int a_tone)
	{
		if (!_slLog.empty() && _slLog.back().text == a_text) {
			return;
		}
		log::info("[slots] {}", a_text);
		_slLog.push_back({ a_text, a_tone });
		while (_slLog.size() > 6) {
			_slLog.pop_front();
		}
	}

	void Game::SlHud(HudState& a_h) const
	{
		a_h.slots = true;
		a_h.poker = false;
		a_h.seated = true;
		a_h.pkTitle = "SLOTS · ONE PAYLINE";
		a_h.pkBoardLabel = "JACKPOT";
		a_h.pkPot = std::max(s_jackpot, kJackpotSeed);
		a_h.caps = PlayerCaps();
		a_h.pkAge = _slAge;
		a_h.pkPad = _padUsed.load();
		a_h.slBet = _slBet;
		a_h.slJackpot = a_h.pkPot;
		a_h.slMaxBet = kMaxBet;
		a_h.slSpinning = _slYou >= 0 && _slYou < static_cast<int>(_sl.size()) && _sl[_slYou].spin.on;
		a_h.pkInfo = std::format("{} CAPS A PULL · 25 A PULL FOR THE JACKPOT", _slBet);
		{
			std::scoped_lock lock(_camMutex);
			a_h.pkPtrX = static_cast<float>(_pkPtrX);
			a_h.pkPtrY = static_cast<float>(_pkPtrY);
		}
		// who else is playing
		for (const auto& m : _sl) {
			if (m.step == 0 || m.you) {
				continue;
			}
			HudState::PokerRow r;
			r.name = Upper(m.name);
			r.stack = Purse(m.id);
			r.bet = m.spin.on ? m.spin.bet : 0;
			r.status = m.step == 1 ? "COMING OVER" : m.spin.on ? "PULLING" : "";
			a_h.pkRows.push_back(std::move(r));
		}
		a_h.log = _slLog;
		a_h.banner = _slBanner;
		a_h.bannerTitle = _slBannerTitle;
		a_h.bannerSub = _slBannerSub;
		a_h.bannerAge = _slBannerAge;
	}

	// ---------------------------------------------------------------- the jackpot in the co-save

	void Game::SaveJackpot(const F4SE::SerializationInterface* a_intfc)
	{
		if (a_intfc->OpenRecord(kJackpotType, 1)) {
			const std::int32_t j = s_jackpot;
			a_intfc->WriteRecordData(&j, sizeof(j));
		}
	}

	void Game::LoadJackpot(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version)
	{
		std::int32_t j = 0;
		if (a_version == 1 && a_intfc->ReadRecordData(&j, sizeof(j))) {
			s_jackpot = std::max<int>(j, kJackpotSeed);
		}
	}
}
