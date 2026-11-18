// The settlers who come over to bet, each at their own spot round the layout, with a few vanilla idles while they play.
// Walking them there is WastelandWagers.esl's quest package (Npc.h); once there they're held with SetRestrained and
// kept facing the table.

#include "Game.h"

#include "Forms.h"
#include "Npc.h"
#include "Settings.h"
#include "Util.h"
#include "rl/Table.h"

namespace fr
{
	namespace
	{
		constexpr float kArrived = 12.0f;        // this close to the spot: there (then put exactly on it)
		constexpr float kSettleIn = 2.5f;        // seconds standing at their spot before they start betting
		constexpr float kAtSpot = 40.0f;         // ...and only while they're this close to it
		constexpr float kStallResend = 6.0f;     // no closer in this long: the walk is sent again
		constexpr int   kMinBudget = 25;         // what a settler puts down in a round: 25 to 100 caps
		constexpr int   kMaxBudget = 100;
		constexpr float kSnap = 6.0f;            // arrived further than this from their spot: put exactly on it (well inside the walk package's 10: there, it's done)
		constexpr float kPersonalSpace = 55.0f;  // a spot with someone (anyone) standing this close isn't free

		// vanilla idles (Fallout4.esm IDLE)
		// (just the thinking idle: the shopping looks turn them about, as if browsing a shop's shelves)
		[[maybe_unused]] constexpr std::array<RE::TESFormID, 1> kWaitIdles{ 0x001E81AC };  // thinking
		[[maybe_unused]] constexpr RE::TESFormID                kPlaceIdle = 0x001A8A0D;  // IdlePlaceEnvelopeOnTable
		constexpr std::array<RE::TESFormID, 2> kWinIdles{ 0x0010BA3A, 0x0010F9C0 };  // cheering, fight winner
		[[maybe_unused]] constexpr RE::TESFormID                kLoseIdle = 0x00038C7A;               // HeadShakeNo
		// standing about: the game's own unarmed standing fidgets and talking hands (dyn_Flavor, so played where they
		// stand: no step and turn to line up first, as the activation idles did)
		constexpr std::array<RE::TESFormID, 2> kStandIdles{ 0x002179C7, 0x002179CA };  // RaiderIdleFlavorMT_PoseARootDefault, RaiderIdleDialogueMT_PoseADefault
		constexpr float kStepBack = 32.0f;       // where people stand: this far out from the table's edge

		float Distance2D(const RE::NiPoint3& a, const RE::NiPoint3& b) { return std::hypot(a.x - b.x, a.y - b.y); }

		std::string Upper(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return a_s;
		}

		std::uint32_t Hash(std::uint32_t a)
		{
			a = a * 0x9E3779B1u + 0x7F4A7C15u;
			a ^= a >> 13;
			a *= 0x85EBCA6Bu;
			a ^= a >> 16;
			return a;
		}

		bool Usable(RE::Actor* a_actor)
		{
			return a_actor && !a_actor->IsDisabled() && !a_actor->IsDead(false) && a_actor->Get3D() &&
			       !RE::PowerArmor::ActorInPowerArmor(*a_actor);  // (nobody in power armour plays or deals)
		}
	}

	// shared with Save.cpp
	std::mutex                                 g_npcMutex;
		std::unordered_map<RE::TESFormID, int>     g_purses;          // an NPC's caps for roulette

	int Game::Purse(RE::TESFormID a_id)
	{
		std::scoped_lock lock(g_npcMutex);
		auto it = g_purses.find(a_id);
		if (it == g_purses.end()) {
			// a settler's first visit: 250 to 1000 caps, fixed per NPC
			it = g_purses.emplace(a_id, 250 + static_cast<int>(Hash(a_id) % 751)).first;
		}
		return it->second;
	}

	void Game::AddToPurse(RE::TESFormID a_id, int a_caps)
	{
		Purse(a_id);
		std::scoped_lock lock(g_npcMutex);
		g_purses[a_id] = std::max(0, g_purses[a_id] + a_caps);
	}

	// ---------------------------------------------------------------- settlers walking over

	RE::NiPoint3 Game::SeatSpot(const Frame& a_frame, int a_spot, float& a_heading) const
	{
		using namespace rl::table;
		// two along the players' side and two along the far side, by the layout, well apart; filled front, back,
		// front, back
		static constexpr std::array<std::pair<double, int>, kSpots> spots{ { { 17.0, -1 }, { 42.0, 1 }, { 89.0, -1 }, { 102.0, 1 } } };
		const auto& [x, side] = spots[static_cast<std::size_t>(a_spot) % spots.size()];
		const double y = side < 0 ? kPlayersEdgeY - kStepBack : kFarEdgeY + kStepBack;
		a_heading = HeadingOf(a_frame, 0.0, side < 0 ? 1.0 : -1.0);
		return ToWorld(a_frame, x, y, 0.0);
	}

	namespace
	{
		// (the turning log: what each held settler's doing, every half second)
		struct TurnWatch
		{
			float                                 yaw{ 0.0f };
			std::chrono::steady_clock::time_point at{};
			int                                   quiet{ 0 };
		};
		std::unordered_map<RE::TESFormID, TurnWatch> g_turns;
		// (the idle log: what each held settler's playing, every change; and which idles were ours, sent when)
		struct IdleWatch
		{
			RE::TESFormID                         current{ 0 }, last{ 0 };
			float                                 yaw{ 0.0f };
			std::chrono::steady_clock::time_point since{};
		};
		std::unordered_map<RE::TESFormID, IdleWatch> g_idles;

		std::string FormName(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return "none";
			}
			const char* id = a_form->GetFormEditorID();
			return std::format("{:08X}{}{}", a_form->GetFormID(), id && *id ? " " : "", id ? id : "");
		}
	}

	void Game::WatchTurn(RE::Actor* a_actor, float a_heading)
	{
		// every idle a held settler starts, ours or the game's own, and what it did to their facing ([idle]): the
		// twist between our idles is whatever plays there
		if (a_actor && Settings::Get().logTurning) {
			auto*       proc = a_actor->currentProcess;
			auto*       mh = proc ? proc->middleHigh : nullptr;
			const auto  cur = mh && mh->currentIdle ? mh->currentIdle->GetFormID() : 0;
			const auto  lastPlayed = mh && mh->lastIdlePlayed ? mh->lastIdlePlayed->GetFormID() : 0;
			auto&       w = g_idles[a_actor->GetFormID()];
			const auto  now = std::chrono::steady_clock::now();
			if (cur != w.current || lastPlayed != w.last) {
				const float held = w.since.time_since_epoch().count() ? std::chrono::duration<float>(now - w.since).count() : 0.0f;
				const auto  ours = Npc::LastIdleSent(a_actor);
				const bool  isOurs = ours.first != 0 && (ours.first == cur || ours.first == lastPlayed) &&
				                    std::chrono::duration<float>(now - ours.second).count() < 3.0f;
				log::info("[idle] {} ({:08X}): now {} (last played {}), {}; the one before for {:.1f} s; facing {:.0f} (the table's {:.0f}, it turned {:+.0f} since)",
					a_actor->GetDisplayFullName() ? a_actor->GetDisplayFullName() : "?", a_actor->GetFormID(),
					cur ? FormName(mh->currentIdle) : std::string("none"), lastPlayed ? FormName(mh->lastIdlePlayed) : std::string("none"),
					isOurs ? "ours" : "NOT OURS (the game's own)", held, a_actor->data.angle.z * 57.2958f, a_heading * 57.2958f,
					util::AngleDiff(w.yaw, a_actor->data.angle.z) * 57.2958f);
				w.current = cur;
				w.last = lastPlayed;
				w.since = now;
				w.yaw = a_actor->data.angle.z;
			}
		}
		// What a settler held at a table is doing while they turn (Settings: bLogTurning): their facing against the
		// table's, how fast it's changing, the package they're running and its target, what their animation graph says
		// about turning and moving, what they're looking at. Logged every half second while they're turning or off the
		// table's heading, and once a minute anyway
		if (!a_actor || !Settings::Get().logTurning) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		auto&      w = g_turns[a_actor->GetFormID()];
		if (w.at.time_since_epoch().count() == 0) {
			w = { a_actor->data.angle.z, now, 0 };
			return;
		}
		const float secs = std::chrono::duration<float>(now - w.at).count();
		if (secs < 0.5f) {
			return;
		}
		const float yaw = a_actor->data.angle.z;
		const float rate = util::AngleDiff(w.yaw, yaw) / secs * 57.2958f;   // degrees a second
		const float off = util::AngleDiff(a_heading, yaw) * 57.2958f;        // degrees off the table's heading
		w.yaw = yaw;
		w.at = now;
		const bool turning = std::abs(rate) > 4.0f || std::abs(off) > 6.0f;
		if (!turning && ++w.quiet < 120) {
			return;
		}
		w.quiet = 0;
		std::string pkg = "none", target = "none", proc = "";
		if (auto* p = a_actor->currentProcess) {
			pkg = FormName(p->currentPackage.package);
			if (auto t = p->currentPackage.target.get()) {
				target = FormName(t.get());
				if (auto* base = t->GetObjectReference()) {
					target += " (" + FormName(base) + ")";
				}
			}
			proc = std::format(" procedure {}", p->currentPackage.currentProcedureIndex);
		}
		auto graphF = [&](const char* a_name) {
			float v = 0.0f;
			return a_actor->GetGraphVariableImplFloat(a_name, v) ? std::format("{:.2f}", v) : std::string("-");
		};
		auto graphB = [&](const char* a_name) {
			bool v = false;
			return a_actor->GetGraphVariableImplBool(a_name, v) ? (v ? "1" : "0") : "-";
		};
		log::info("[turn] {} ({:08X}): facing {:.0f}, the table's {:.0f} ({:+.0f} off), turning {:+.0f}/s; package {}{} at {}; "
				  "graph TurnDelta {} TurnDeltaDamped {} Speed {} Direction {} bAnimationDriven {} IsSyncIdle {}; in combat {}, "
				  "talking {}",
			a_actor->GetDisplayFullName() ? a_actor->GetDisplayFullName() : "?", a_actor->GetFormID(), yaw * 57.2958f, a_heading * 57.2958f,
			off, rate, pkg, proc, target, graphF("TurnDelta"), graphF("TurnDeltaDamped"), graphF("Speed"), graphF("Direction"),
			graphB("bAnimationDriven"), graphB("bIsSynced"), a_actor->IsInCombat(), a_actor->IsTalking());
	}

	void Game::FaceTowards(RE::Actor* a_actor, float a_heading, float& a_faceIn, float a_dt)
	{
		WatchTurn(a_actor, a_heading);
		// turned to the table now and then (every 3 s), only when well off it (more than 20 degrees): their walk could
		// end facing anywhere (two roulette players stood facing each other), and nothing turned them. (Not every tick:
		// snapped back whenever an idle turned them a little, they stuttered round)
		a_faceIn -= a_dt;
		if (!a_actor || a_faceIn > 0.0f) {
			return;
		}
		a_faceIn = 3.0f;
		if (std::abs(util::AngleDiff(a_heading, a_actor->data.angle.z)) > 0.35f) {
			Npc::FaceAngle(a_actor, a_heading);
		}
	}

	void Game::HoldFacing(Walker& a_walk, RE::Actor* a_actor)
	{
		// Held where they stand, their walk package stays on, linked to their spot's marker. The marker kept the
		// heading it was made with while they stood however they'd ended up (and were turned to the table after),
		// and the package went on asking for a turn to it they couldn't make: the idle-turn log showed the same turn
		// asked for over and over (Sally 73, Maisie and Sophie 67 degrees), feet stepping round and snapping back
		// every two or three seconds. The marker turned to wherever they face now: nothing left to turn to.
		// (Only its heading: its place is still their spot, where they're put back to.)
		if (!a_actor) {
			return;
		}
		UnstickTurn(a_actor);
		auto marker = a_walk.marker.get();
		if (!marker) {
			return;
		}
		const float facing = a_actor->data.angle.z;
		if (std::abs(util::AngleDiff(marker->data.angle.z, facing)) > 0.01f) {
			marker->data.angle.z = facing;
		}
	}

	void Game::UnstickTurn(RE::Actor* a_actor)
	{
		// A held NPC caught in the turn-on-the-spot loop: their graph asks for the same turn every two or three
		// seconds (fo4idlefix's log: Sally 73 degrees, Maisie and Sophie 67, from the moment a save loads, before
		// anything here touched them), they can't make it, the feet step round and snap back, over and over. A
		// full-body idle breaks it: Maisie's stopped for good after her dealer's reach over the table
		// (DialogueGiveItem). So: two turn requests within 8 s while held, and out of whatever's looping (IdleStop)
		// into that reach, once; again at most every 20 s if it comes back
		static const RE::BSFixedString kTurnDelta{ "TurnDelta" };
		constexpr RE::TESFormID        kGiveItem = 0x000CEA8D;
		struct Watch
		{
			bool                                  asking{ false };
			int                                   requests{ 0 };
			std::chrono::steady_clock::time_point first{};
			std::chrono::steady_clock::time_point kicked{};
			int                                   kicks{ 0 };
		};
		static std::unordered_map<RE::TESFormID, Watch> watches;

		float turn = 0.0f;
		if (!a_actor || !a_actor->GetGraphVariableImplFloat(kTurnDelta, turn)) {
			return;
		}
		auto&      w = watches[a_actor->GetFormID()];
		const auto now = std::chrono::steady_clock::now();
		const bool asking = std::abs(turn) > 1.0f;
		if (asking && !w.asking) {
			if (w.requests == 0 || now - w.first > std::chrono::seconds(8)) {
				w.requests = 0;
				w.first = now;
			}
			++w.requests;
		}
		w.asking = asking;
		if (w.requests < 2 || (w.kicks > 0 && now - w.kicked < std::chrono::seconds(20))) {
			return;
		}
		w.requests = 0;
		w.kicked = now;
		++w.kicks;
		log::info("{} ({:08X}): turning on the spot and back (asked {:.0f}, held, can't): out of it with a reach ({}{})",
			a_actor->GetDisplayFullName() ? a_actor->GetDisplayFullName() : "?", a_actor->GetFormID(), turn, w.kicks,
			w.kicks == 1 ? "st time" : " times");
		Npc::PlayIdleAfterStop(a_actor, kGiveItem);
	}

	void Game::TurnToFace(RE::Actor* a_actor, float a_x, float a_y)
	{
		if (!a_actor) {
			return;
		}
		const float want = std::atan2(a_x - a_actor->data.location.x, a_y - a_actor->data.location.y);
		if (std::abs(util::AngleDiff(want, a_actor->data.angle.z)) < 0.21f) {
			return;  // (within 12 degrees: left be)
		}
		Npc::FaceAngle(a_actor, want);
	}

	bool Game::WalkTo(RE::Actor* a_actor, Walker& a_walk, const Frame& a_frame, const RE::NiPoint3& a_spot, float a_heading, float a_dt,
		bool a_dealer, float a_timeout)
	{
		(void)a_frame;
		auto marker = a_walk.marker.get();
		if (!marker) {
			// the spot, on the floor where they stand: their package walks them to it
			RE::NiPoint3 spot = a_spot;
			spot.z = a_actor->data.location.z;
			a_walk.marker = Spawn(Forms::Marker(), spot, a_heading);
			marker = a_walk.marker.get();
			if (!marker) {
				return false;
			}
			if (Npc::Joined(a_actor)) {
				Npc::Walk(a_actor, marker.get(), false);
			}
			a_walk.joinSent = false;
		}
		if (!a_walk.joinSent) {
			a_walk.joinSent = true;
			if (Npc::Ready()) {
				Npc::Join(a_actor, marker.get(), a_dealer);
			}
		}
		const float d = Distance2D(a_actor->data.location, a_spot);
		if (d < kArrived) {
			return true;
		}
		// (real seconds: the tick's own are capped a frame at a time, and the tick doesn't come every frame, so a 15s
		// walk timed by them ran 40s and more)
		{
			const auto now = std::chrono::steady_clock::now();
			if (a_walk.last != std::chrono::steady_clock::time_point{}) {
				a_dt = std::clamp(std::chrono::duration<float>(now - a_walk.last).count(), 0.0f, 1.0f);
			}
			a_walk.last = now;
		}
		a_walk.walked += a_dt;
		if (d < a_walk.best - 10.0f) {
			a_walk.best = d;
			a_walk.stall = 0.0f;
		} else {
			a_walk.stall += a_dt;
		}
		// (close, and no closer for a moment: there. The walk sent again with its AI reset, as it was, made them skip in
		// place every few seconds, 12 from their spot: the caller puts them on it)
		// (a dealer: right up to their spot, nothing put them on it after. Close enough, or no closer for a while)
		// (stuck a few seconds anywhere near the table, chairs in the way say: there too, and put on their spot by the
		// caller, once)
		// (the stall only once they've been walking a while: a walk takes a few seconds to start, and a dealer 232 away
		// was put on their spot before they'd taken a step)
		if (a_dealer ? (d < 15.0f || (d < 250.0f && a_walk.stall > 4.0f && a_walk.walked > 8.0f)) : (d < kAtSpot && a_walk.stall > 1.5f)) {
			return true;
		}
		// sat down somewhere on the way (their sandbox's doing): up
		a_walk.getUp -= a_dt;
		if (auto* proc = a_actor->currentProcess; proc && proc->GetOccupiedFurniture() && a_walk.getUp <= 0.0f) {
			a_walk.getUp = 3.0f;
			a_actor->InitiateGetUpPackage();
		}
		const bool heldElsewhere = static_cast<const RE::ActorState*>(a_actor)->lifeState == static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kRestrained);
		if (a_walk.stall > kStallResend && heldElsewhere) {
			// held in place by something else (another table, another mod): can't walk, and a walk sent with its AI
			// reset only turns them on the spot and back, every few seconds. Left be
			if (!a_walk.heldWarned) {
				a_walk.heldWarned = true;
				log::info("{}: held in place by something else, {:.0f} from the table: not sending the walk again", a_actor->GetDisplayFullName(), d);
			}
			a_walk.stall = 0.0f;
			a_walk.best = d;
		}
		if (a_walk.stall > kStallResend) {
			log::info("{}: no closer to the table in {:.0f}s ({:.0f} away, package {}); sending the walk again", a_actor->GetDisplayFullName(),
				a_walk.stall, d, Npc::PackageName(a_actor));
			a_walk.stall = 0.0f;
			a_walk.best = d;
			if (Npc::Joined(a_actor)) {
				Npc::Walk(a_actor, marker.get(), true);
			} else {
				a_walk.joinSent = false;
			}
		}
		if (a_walk.walked > a_timeout) {
			// a while on and not there (tight spaces round a table can't always be walked): near enough, where they
			// are is their place; further, the walk sent again. Never put there: a jump the eye catches
			a_walk.walked = 0.0f;
			if (d < 120.0f) {
				log::info("{}: {:.0f} from their spot after a while: near enough", a_actor->GetDisplayFullName(), d);
				return true;
			}
			log::info("{}: still {:.0f} from the table; walking again", a_actor->GetDisplayFullName(), d);
			if (heldElsewhere) {
				// (see above: no walk with an AI reset for someone held in place)
			} else if (Npc::Joined(a_actor)) {
				Npc::Walk(a_actor, marker.get(), true);
			} else {
				a_walk.joinSent = false;
			}
		}
		return false;
	}

	// ---------------------------------------------------------------- settlers betting

	// ---------------------------------------------------------------- the dealers

	bool Game::IsCroupier(RE::TESFormID a_id) const
	{
		return a_id != 0 && (_t.croupier.id == a_id || _other.croupier.id == a_id || _pk.croupier.id == a_id || _bj.dealerId == a_id);
	}

	void Game::TickCroupier(Croupier& a_c, const Frame& a_frame, double a_x, double a_y, double a_faceX, double a_faceY, float a_dt,
		const char* a_table, bool a_faceMiddle, std::uint8_t a_kind, RE::TESFormID a_tableRef)
	{
		const auto* you = RE::PlayerCharacter::GetSingleton();
		a_c.cool -= a_dt;
		if (a_c.id == 0 && a_tableRef != 0) {
			// after a load: who was dealing here when it was saved, back at it straight away (Roster.cpp)
			std::int32_t rs = -1;
			if (auto* back = RosterNext(a_kind, a_tableRef, rs, true)) {
				a_c.actor = back->GetHandle();
				a_c.id = back->GetFormID();
				a_c.name = back->GetDisplayFullName();
				a_c.walk = Walker{};
				a_c.held = false;
				log::info("{}: {} ({:08X}) back to deal (as when the game was saved)", a_table, a_c.name, a_c.id);
				return;
			}
		}
		if (a_c.id == 0) {
			// nobody dealing: the nearest free settler comes over, now and then
			a_c.lookIn -= a_dt;
			if (a_c.lookIn > 0.0f || (you && you->IsInCombat())) {
				return;
			}
			a_c.lookIn = std::uniform_real_distribution<float>(6.0f, 12.0f)(_rng);
			const auto&  s = Settings::Get();
			auto*        player = RE::PlayerCharacter::GetSingleton();
			static auto* npcType = RE::TESForm::GetFormByID<RE::BGSKeyword>(Forms::kActorTypeNPC);
			RE::Actor*   best = nullptr;
			float        bestD = 1e9f;
			util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
				a_cell->ForEachReferenceInRange(a_frame.origin, s.bettorRange, [&](RE::TESObjectREFR* a_ref) {
					auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
					if (!Usable(actor) || actor == player || actor->IsChild() || actor->IsInCombat() || Npc::Busy(actor) ||
						(player && actor->GetHostileToActor(player)) || (npcType && !actor->HasKeywordHelper(npcType, nullptr)) ||
						IsCroupier(actor->GetFormID()) || RosterHolds(actor->GetFormID()) || BjHasSeat(actor->GetFormID()) || PkHasSeat(actor->GetFormID()) || SlHasSeat(actor->GetFormID()) ||
						std::ranges::any_of(_t.seats, [&](const Seat& st) { return st.id == actor->GetFormID(); }) ||
						std::ranges::any_of(_other.seats, [&](const Seat& st) { return st.id == actor->GetFormID(); })) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
					const float d = (actor->data.location - a_frame.origin).Length();
					if (d < bestD) {
						bestD = d;
						best = actor;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			});
			if (!best) {
				return;
			}
			a_c.actor = best->GetHandle();
			a_c.id = best->GetFormID();
			a_c.name = best->GetDisplayFullName();
			a_c.walk = Walker{};
			a_c.held = false;
			log::info("{}: {} ({:08X}) comes over to deal", a_table, a_c.name, a_c.id);
			return;
		}
		auto  ref = a_c.actor.get();
		auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
		Npc::KeepFromPool(actor);  // (in a game here and called over to the pool table: kept here)
		if (!Usable(actor) || actor->IsInCombat() || (you && you->IsInCombat())) {
			// gone for a moment (their 3D reloaded after a menu, say) isn't gone: a few seconds of it is
			a_c.goneFor += a_dt;
			if (a_c.goneFor > 3.0f || !actor) {
				ReleaseCroupier(a_c, "gone");
			}
			return;
		}
		a_c.goneFor = 0.0f;
		const float heading = HeadingOf(a_frame, a_faceX, a_faceY);
		const auto  spot = ToWorld(a_frame, a_x, a_y, 0.0);
		const float off = std::hypot(actor->data.location.x - spot.x, actor->data.location.y - spot.y);
		if (a_c.held) {
			HoldFacing(a_c.walk, actor);
			// there: they just stand where their walk ended. (Put back on the exact spot every few seconds, as they
			// were, they jumped back and forth: the table's edge pushed them off it again.) Well off it (pushed away),
			// they walk back. Now and then turned to the table's middle (poker), or the way given (roulette: the wheel)
			// and a standing fidget of their own between their gestures
			a_c.idleIn -= a_dt;
			if (a_c.idleIn <= 0.0f) {
				a_c.idleIn = std::uniform_real_distribution<float>(10.0f, 20.0f)(_rng);
				if (a_c.cool <= 0.0f) {
					Npc::PlayIdle(actor, kStandIdles[std::uniform_int_distribution<std::size_t>(0, kStandIdles.size() - 1)(_rng)]);
				}
			}
			a_c.putBackIn -= a_dt;
			a_c.snapIn -= a_dt;
			if (a_c.snapIn <= 0.0f) {
				a_c.snapIn = 3.0f;
				if (a_faceMiddle) {
					TurnToFace(actor, a_frame.origin.x, a_frame.origin.y);
				} else {
					const auto ahead = ToWorld(a_frame, a_x + a_faceX * 100.0, a_y + a_faceY * 100.0, 0.0);
					TurnToFace(actor, ahead.x, ahead.y);
				}
				// off their spot by more than a step (pushed back past the chairs, or gone home over a load): put back on
				// it. Not every check: at most every 20 s, so a nudge from the table's edge isn't a jump back and forth
				// (a few units off is left alone: that was the blackjack dealer's back and forth)
				if (auto marker = a_c.walk.marker.get(); marker && off > 20.0f && (off > 300.0f || a_c.putBackIn <= 0.0f)) {
					log::info("{}: dealer {} {:.0f} off their spot: put back on it", a_table, a_c.name, off);
					Npc::MoveTo(actor, marker.get());
					a_c.putBackIn = 20.0f;
				} else if (!marker && off > 60.0f) {
					log::info("{}: dealer {} {:.0f} off their spot: walking back", a_table, a_c.name, off);
					Npc::SetRestrained(actor, false);
					a_c.held = false;
					a_c.walk.walked = a_c.walk.stall = 0.0f;
					a_c.walk.best = 1e9f;
				}
			}
			return;
		}
		if (WalkTo(actor, a_c.walk, a_frame, spot, heading, a_dt, true, 15.0f)) {
			// their walk ended short of the spot (more than 15 off: the table or someone in the way): put on it, once.
			// (Never again while they stand there: put back every few seconds, the table's edge pushed them off each
			// time and they jumped back and forth)
			// (more than 8 off: the poker dealer stopped 11-12 short, behind the chairs)
			if (auto marker = a_c.walk.marker.get(); marker && off > 8.0f) {
				log::info("{}: dealer {} stopped {:.0f} short of their spot: put on it", a_table, a_c.name, off);
				Npc::MoveTo(actor, marker.get());
			}
			log::info("{}: dealer {} at their spot ({:.0f} off it)", a_table, a_c.name, off);
			a_c.held = Npc::SetRestrained(actor, true);
			a_c.snapIn = 0.5f;  // (turned to the table straight after)
		}
	}

	void Game::TrackSit(const char* a_table, const std::string& a_name, RE::Actor* a_actor, RE::TESObjectREFR* a_chair, int& a_was)
	{
		if (!a_actor) {
			return;
		}
		const int now = static_cast<int>(a_actor->DoGetSitSleepState());
		if (now == a_was) {
			return;
		}
		static constexpr const char* kStates[] = { "standing", "wants to sit", "sitting down", "sitting", "wants to stand", "standing up" };
		const auto name = [](int a_s) { return a_s >= 0 && a_s < 6 ? kStates[a_s] : "?"; };
		std::string in = "nothing";
		if (auto* proc = a_actor->currentProcess) {
			if (auto f = proc->GetOccupiedFurniture().get()) {
				in = f.get() == a_chair ? std::string("their chair") : std::format("{:08X} (not their chair)", f->GetFormID());
			}
		}
		const float d = a_chair ? std::hypot(a_actor->data.location.x - a_chair->data.location.x, a_actor->data.location.y - a_chair->data.location.y) : -1.0f;
		std::string menu;
		if (auto* ui = RE::UI::GetSingleton()) {
			for (const char* m : { "PipboyMenu", "WorkshopMenu", "SleepWaitMenu", "DialogueMenu", "PauseMenu", "Console", "LoadingMenu" }) {
				if (ui->GetMenuOpen(m)) {
					menu += menu.empty() ? m : std::string(", ") + m;
				}
			}
		}
		log::info("[sit] {}: {} {} -> {} (package {}, in {}, {:.0f} from their chair{}{}{})", a_table, a_name, a_was < 0 ? "starts" : name(a_was),
			name(now), Npc::PackageName(a_actor), in, d, a_actor->IsInCombat() ? ", in combat" : "", menu.empty() ? "" : ", menus: ",
			menu);
		a_was = now;
	}

	void Game::Voice(RE::Actor* a_actor, int a_how)
	{
		// the game's crowd lines: cheering, laughing, groaning (the poker table's, at every table now)
		constexpr RE::TESFormID kCheer = 0x00110396;  // Crowd_ApprovalSounds_TopicSubType
		constexpr RE::TESFormID kLaugh = 0x00110395;  // Crowd_Laughter_TopicSubType
		constexpr RE::TESFormID kGroan = 0x00110398;  // Crowd_DisapprovalSounds_TopicSubType
		if (!a_actor || a_how == 0 || !Settings::Get().pkVoices) {
			return;
		}
		const float chance = a_how > 1 ? 1.0f : a_how > 0 ? 0.7f : 0.5f;  // (2: a big one, always)
		if (std::uniform_real_distribution<float>(0.0f, 1.0f)(_rng) >= chance) {
			return;
		}
		if (a_how > 0) {
			Npc::Say(a_actor, std::uniform_real_distribution<float>(0.0f, 1.0f)(_rng) < 0.35f ? kLaugh : kCheer);
		} else {
			Npc::Say(a_actor, kGroan);
		}
	}

	void Game::RlDealer(Gesture a_g, bool a_now)
	{
		if (a_now) {
			_t.croupier.cool = 0.0f;
		}
		using namespace rl::table;
		const double x = -kHalfLength - 20.0, y = kWheelY;
		CroupierGesture(_t.croupier, a_g, x, y, 1.0, 0.0, kWheelX, kWheelY);  // (points at the wheel)
	}

	void Game::DealerGesture(const RE::ObjectRefHandle& a_actor, bool a_held, float& a_cool, Gesture a_g, double a_x, double a_y,
		double a_fx, double a_fy, double a_tx, double a_ty)
	{
		// the game's gestures that play where they stand (the activation ones step and turn to line up first), and that
		// PlayIdle does play (the log: the talk and beckon idles came back false): PointLeft / Right / Forward,
		// DialogueGiveItem (a reach out over the table: dealing, paying, sending the ball round)
		constexpr RE::TESFormID kPointLeft = 0x001793E4, kPointRight = 0x001793E5, kPointForward = 0x001793E3;
		constexpr RE::TESFormID kGiveItem = 0x000CEA8D;
		if (!a_held || a_cool > 0.0f) {
			return;
		}
		auto  ref = a_actor.get();
		auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (!Usable(actor) || actor->IsInCombat()) {
			return;
		}
		RE::TESFormID idle = 0;
		switch (a_g) {
		case Gesture::kPoint:
			{
				// their right: the way they face turned a quarter to the right
				const double vx = a_tx - a_x, vy = a_ty - a_y;
				const double len = std::hypot(vx, vy);
				const double side = len > 1.0 ? (vx * a_fy - vy * a_fx) / len : 0.0;
				idle = side > 0.45 ? kPointRight : side < -0.45 ? kPointLeft : kPointForward;
				break;
			}
		case Gesture::kBeckon:
			idle = kPointForward;  // (bets: a point at the layout)
			break;
		case Gesture::kDeal:
		case Gesture::kTalk:
			idle = kGiveItem;  // (the hand's paid out, the ball's sent round)
			break;
		}
		a_cool = a_g == Gesture::kPoint ? 1.4f : 2.5f;
		Npc::PlayIdle(actor, idle);
	}

	void Game::ReleaseCroupier(Croupier& a_c, const char* a_why)
	{
		if (a_c.id == 0) {
			return;
		}
		if (auto ref = a_c.actor.get(); ref && ref->As<RE::Actor>()) {
			auto* actor = ref->As<RE::Actor>();
			if (a_c.held) {
				Npc::SetRestrained(actor, false);
			}
			Npc::Leave(actor);
		}
		Remove(a_c.walk.marker);
		log::info("dealer {} leaves ({})", a_c.name, a_why ? a_why : "");
		a_c = Croupier{};
	}

	void Game::UnholdCroupier(Croupier& a_c)
	{
		if (a_c.held) {
			if (auto ref = a_c.actor.get(); ref && ref->As<RE::Actor>()) {
				Npc::SetRestrained(ref->As<RE::Actor>(), false);
			}
			a_c.held = false;
		}
	}

	void Game::FindBettor(const Frame& a_frame)
	{
		if (const auto* you = RE::PlayerCharacter::GetSingleton(); you && you->IsInCombat()) {
			return;  // (nobody sits down in a fight)
		}
		// after a load: who was playing here when it was saved, back at their spot first (Roster.cpp)
		if (auto tbl = _t.table.get()) {
			std::int32_t rs = -1;
			while (auto* back = RosterNext(kRosterRoulette, tbl->GetFormID(), rs, false)) {
				if (rs >= 0 && rs < kSpots && std::ranges::none_of(_t.seats, [&](const Seat& st) { return st.spot == rs; })) {
					RlSeatIn(rs, back);
				}
			}
		}
		const auto& s = Settings::Get();
		if (!s.bettors || static_cast<int>(_t.seats.size()) >= s.maxBettors || _t.phase != Phase::kBetting) {
			return;
		}
		if (std::uniform_real_distribution<float>(0.0f, 1.0f)(_rng) > 0.6f) {
			return;  // not this time
		}
		auto*        player = RE::PlayerCharacter::GetSingleton();
		static auto* npcType = RE::TESForm::GetFormByID<RE::BGSKeyword>(Forms::kActorTypeNPC);
		std::vector<std::pair<float, RE::Actor*>> found;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(a_frame.origin, s.bettorRange, [&](RE::TESObjectREFR* a_ref) {
				auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
				if (!Usable(actor) || actor == player || actor->IsChild() || actor->IsInCombat() || Npc::Busy(actor) || (player && actor->GetHostileToActor(player)) ||
					(npcType && !actor->HasKeywordHelper(npcType, nullptr)) ||
					std::ranges::any_of(_t.seats, [&](const Seat& st) { return st.id == actor->GetFormID(); }) ||
					std::ranges::any_of(_other.seats, [&](const Seat& st) { return st.id == actor->GetFormID(); }) ||  // (at the other table)
					PkHasSeat(actor->GetFormID()) || SlHasSeat(actor->GetFormID()) || BjHasSeat(actor->GetFormID()) || IsCroupier(actor->GetFormID()) || RosterHolds(actor->GetFormID()) ||                          // (playing poker, dealing)
					std::ranges::any_of(found, [&](const auto& f) { return f.second == actor; })) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				// (only settlers and the like who are someone's: anyone with a name and nobody's enemy); broke since
				// their last visit: a while later, some caps again
				if (Purse(actor->GetFormID()) < kMinBudget) {
					AddToPurse(actor->GetFormID(), 150 + static_cast<int>(Hash(actor->GetFormID() ^ 0x5EED) % 250));
				}
				found.emplace_back((actor->data.location - a_frame.origin).Length(), actor);
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		if (found.empty()) {
			return;
		}
		std::ranges::sort(found, {}, &std::pair<float, RE::Actor*>::first);
		const std::size_t pick = std::uniform_int_distribution<std::size_t>(0, std::min<std::size_t>(found.size(), 4) - 1)(_rng);
		auto*             actor = found[pick].second;
		// a free spot: nobody else's, and nobody (a settler sandboxing there, you) standing on it
		int spot = -1;
		for (int i = 0; i < kSpots && spot < 0; ++i) {
			if (std::ranges::any_of(_t.seats, [&](const Seat& st) { return st.spot == i; })) {
				continue;
			}
			float      heading = 0.0f;
			const auto at = SeatSpot(a_frame, i, heading);
			bool       taken = false;
			util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
				a_cell->ForEachReferenceInRange(at, kPersonalSpace, [&](RE::TESObjectREFR* a_ref) {
					if (a_ref && a_ref != actor && a_ref->As<RE::Actor>() && !a_ref->IsDisabled()) {
						taken = true;
						return RE::BSContainer::ForEachResult::kStop;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			});
			if (!taken) {
				spot = i;
			}
		}
		if (spot < 0) {
			return;
		}
		RlSeatIn(spot, actor);
	}

	void Game::RlSeatIn(int spot, RE::Actor* actor)
	{
		const auto& s = Settings::Get();
		(void)s;
		Seat seat;
		seat.actor = actor->GetHandle();
		seat.id = actor->GetFormID();
		seat.name = actor->GetDisplayFullName();
		seat.spot = spot;
		seat.rounds = std::uniform_int_distribution<int>(s.bettorMinRounds, s.bettorMaxRounds)(_rng);
		log::info("{} ({:08X}) comes to play: {} caps, {} rounds, spot {}", seat.name, seat.id, Purse(seat.id), seat.rounds, spot);
		Say(std::format("{} COMES TO PLAY", Upper(seat.name)));
		_t.seats.push_back(std::move(seat));
	}

	void Game::SeatBets(Seat& a_seat)
	{
		// 25 to 100 caps this round, in one to three bets, on the kind of bets people like: mostly the outside, now
		// and then a number
		a_seat.betThisRound = true;
		a_seat.budget = std::min(Purse(a_seat.id), std::uniform_int_distribution<int>(kMinBudget, kMaxBudget)(_rng));
		a_seat.chipsToPlace = std::uniform_int_distribution<int>(1, 3)(_rng);
		a_seat.nextChip = std::uniform_real_distribution<float>(0.5f, 2.5f)(_rng);
	}

	void Game::TickSeats(const Frame& a_frame, float a_dt)
	{
		const auto& s = Settings::Get();
		{
			// the dealer: at the wheel's end of the table, behind the wheel, facing down the table
			using namespace rl::table;
			TickCroupier(_t.croupier, a_frame, -kHalfLength - 20.0, kWheelY, 1.0, 0.0, a_dt, "roulette", false, kRosterRoulette,
				_t.table.get() ? _t.table.get()->GetFormID() : 0);  // (RlDealer: the same)
		}
		_t.seatTimer -= a_dt;
		if (_t.seatTimer <= 0.0f) {
			_t.seatTimer = s.bettorEvery * std::uniform_real_distribution<float>(0.7f, 1.3f)(_rng);
			FindBettor(a_frame);
		}
		std::uniform_real_distribution<float> U(0.0f, 1.0f);
		for (auto& seat : _t.seats) {
			auto  ref = seat.actor.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			// (a fight: everyone up. Held, their AI is off and they'd not notice it themselves)
			const auto* you = RE::PlayerCharacter::GetSingleton();
			Npc::KeepFromPool(actor);  // (in a game here and called over to the pool table: kept here)
			if (!Usable(actor) || actor->IsInCombat() || (you && you->IsInCombat())) {
				seat.step = Seat::Step::kLeaving;
				continue;
			}
			float      heading = 0.0f;
			const auto spot = SeatSpot(a_frame, seat.spot, heading);
			switch (seat.step) {
			case Seat::Step::kWalking:
				if (WalkTo(actor, seat.walk, a_frame, spot, heading, a_dt, false, 40.0f)) {
					seat.step = Seat::Step::kPlaying;
					// (put exactly on their spot and turned to the table, always: a step put, their walk's last stride and its
					// turn to face are cut off, where they'd turn hard while the table held them facing it: the spin the
					// turning log showed on arriving, turning 100-180 degrees a second for a moment)
					// (their walk package stays on, linked to the spot they're now on: its travel done, it holds them there.
					// Unlinked, as it was, it went on travelling with nowhere to go: stepping and turning on the spot, the
					// turning log showed, procedure 0 at speed 15)
					// (no putting them on the spot exactly: the step-put showed as a jump)
					seat.nextIdle = std::uniform_real_distribution<float>(4.0f, 10.0f)(_rng);
				}
				break;
			case Seat::Step::kPlaying:
				if (!seat.held) {
					seat.held = Npc::SetRestrained(actor, true);
				}
				HoldFacing(seat.walk, actor);
				FaceTowards(actor, heading, seat.faceIn, a_dt);
				// standing at their spot for a moment before they play (not while still coming up to it)
				if (Distance2D(actor->data.location, spot) < kAtSpot) {
					seat.atSpot += a_dt;
				} else {
					seat.atSpot = 0.0f;
				}
				if (seat.reactIn >= 0.0f) {
					seat.reactIn -= a_dt;
					if (seat.reactIn < 0.0f) {
						SeatIdle(seat, actor, seat.roundNet > 0 ? 2 : seat.roundNet < 0 ? 3 : 4);
						Voice(actor, seat.roundNet > 0 ? (seat.roundNet >= 100 ? 2 : 1) : seat.roundNet < 0 ? -1 : 0);
						seat.nextIdle = std::uniform_real_distribution<float>(8.0f, 16.0f)(_rng);
					}
				} else if (_t.phase != Phase::kResult) {
					seat.nextIdle -= a_dt;
					if (seat.nextIdle <= 0.0f) {
						seat.nextIdle = std::uniform_real_distribution<float>(9.0f, 22.0f)(_rng);
						SeatIdle(seat, actor, 0);
					}
				}
				if (_t.phase == Phase::kBetting) {
					if (seat.rounds <= 0 || Purse(seat.id) + OnTable(seat.id) < 1) {
						if (!HasBets(seat.id)) {
							Say(std::format("{} {}", Upper(seat.name), Purse(seat.id) < 1 ? "IS OUT OF CAPS" : "CALLS IT A NIGHT"));
							seat.step = Seat::Step::kLeaving;
						}
						break;
					}
					if (seat.atSpot < kSettleIn) {
						break;
					}
					if (!seat.betThisRound && _t.bettingTime > 2.0f) {
						SeatBets(seat);
					}
					if (seat.chipsToPlace > 0 && seat.budget > 0) {
						seat.nextChip -= a_dt;
						if (seat.nextChip <= 0.0f) {
							seat.nextChip = std::uniform_real_distribution<float>(2.0f, 4.0f)(_rng);
							--seat.chipsToPlace;
							// the bet: even money half the time, dozens and columns a fifth, the rest on numbers
							const auto all = rl::AllBets(_t.wheel);
							const float roll = U(_rng);
							std::vector<const rl::Bet*> pool;
							for (const auto& b : all) {
								const int pays = b.Payout();
								if ((roll < 0.5f && pays == 1) || (roll >= 0.5f && roll < 0.7f && pays == 2) || (roll >= 0.7f && pays > 2)) {
									pool.push_back(&b);
								}
							}
							if (!pool.empty()) {
								const auto* bet = pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(_rng)];
								// this bet's share of the round's budget, in 5s (the last bet takes what's left)
								int amount = seat.budget;
								if (seat.chipsToPlace > 0) {
									amount = std::max(5, (seat.budget / (seat.chipsToPlace + 1)) / 5 * 5);
								}
								amount = std::min({ amount, seat.budget, Purse(seat.id), Settings::Get().maxBet });
								if (amount > 0 && AddChips(seat.id, *bet, amount)) {
									seat.budget -= amount;
									Say(std::format("{}: {} ON {}", Upper(seat.name), amount, bet->Name()));
									if (std::uniform_real_distribution<float>(0.0f, 1.0f)(_rng) < 0.5f) {
										SeatIdle(seat, actor, 1);
									}
								}
							}
						}
					}
				}
				break;
			case Seat::Step::kLeaving:
				break;
			}
		}
		for (auto& seat : _t.seats) {
			if (seat.step == Seat::Step::kLeaving) {
				ReleaseSeat(seat, nullptr);
			}
		}
		std::erase_if(_t.seats, [](const Seat& st) { return st.step == Seat::Step::kLeaving; });
	}

	void Game::SeatIdle(Seat& a_seat, RE::Actor* a_actor, int a_kind)
	{
		if (!Settings::Get().bettorIdles || !a_actor || !a_seat.held) {
			return;
		}
		auto pick = [&](const auto& a_list) { return a_list[std::uniform_int_distribution<std::size_t>(0, a_list.size() - 1)(_rng)]; };
		RE::TESFormID idle = 0;
		// (only the cheers: the thinking, the chips-down and the head shake are the game's activation idles, which
		// step and turn them to line up first: the shuffle round on the spot between bets, every few seconds)
		switch (a_kind) {
		case 0: idle = pick(kStandIdles); break;  // (now and then while they play)
		case 2: idle = pick(kWinIdles); break;
		default: break;
		}
		if (idle) {
			Npc::PlayIdle(a_actor, idle);
		}
	}

	void Game::ReleaseSeat(Seat& a_seat, const char* a_why)
	{
		auto  ref = a_seat.actor.get();
		auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (_t.phase == Phase::kBetting) {
			TakeAllBack(a_seat.id);
		}
		if (actor) {
			if (a_seat.held) {
				Npc::SetRestrained(actor, false);
			}
			Npc::Leave(actor);
		}
		a_seat.held = false;
		Remove(a_seat.walk.marker);
		a_seat.step = Seat::Step::kLeaving;
		if (a_why) {
			log::info("{} leaves the table: {}", a_seat.name, a_why);
		}
	}
}
