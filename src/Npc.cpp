#include "Npc.h"

#include "Forms.h"
#include "Settings.h"

#include "RE/B/BGSRefAliasInstanceData.h"
#include "RE/E/ExtraAliasInstanceArray.h"
#include "RE/E/ExtraLinkedRef.h"
#include "RE/B/BGSKeyword.h"
#include "RE/T/TESFile.h"
#include "RE/T/TESQuest.h"

// The base class's destructor is declared in CommonLibF4 but not defined there; our functors need one to link.
RE::BSScript::IStackCallbackFunctor::~IStackCallbackFunctor() = default;

namespace fr::Npc
{
	namespace
	{
		// logs what a Papyrus call returned (on the script thread, later)
		class LogResult final : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			explicit LogResult(std::string a_what) :
				_what(std::move(a_what))
			{}

			void CallQueued() override {}
			void CallCanceled() override { log::info("papyrus {}: canceled", _what); }
			void StartMultiDispatch() override {}
			void EndMultiDispatch() override {}
			void operator()(RE::BSScript::Variable a_result) override
			{
				if (a_result.is<bool>()) {
					log::info("papyrus {} -> {}", _what, RE::BSScript::get<bool>(a_result));
				} else if (a_result.is<RE::BSFixedString>()) {
					log::info("papyrus {} -> {}", _what, RE::BSScript::get<RE::BSFixedString>(a_result).c_str());
				}
			}

		private:
			std::string _what;
		};

		std::mutex                        g_joinedMutex;
		std::unordered_set<RE::TESFormID> g_joined;

		// Join's answer: "in ..." if the collection holds them
		class JoinDone final : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			JoinDone(std::string a_what, RE::TESFormID a_id) :
				_what(std::move(a_what)),
				_id(a_id)
			{}

			void CallQueued() override {}
			void CallCanceled() override { log::warn("papyrus {}: canceled (is fo4rouletteQuest.pex installed?)", _what); }
			void StartMultiDispatch() override {}
			void EndMultiDispatch() override {}
			void operator()(RE::BSScript::Variable a_result) override
			{
				std::string text = a_result.is<RE::BSFixedString>() ? RE::BSScript::get<RE::BSFixedString>(a_result).c_str() : "";
				if (text.empty()) {
					text = "nothing (the call failed before Join ran to the end)";
				}
				log::info("papyrus {} -> {}", _what, text);
				if (text.starts_with("in")) {
					std::scoped_lock lock(g_joinedMutex);
					g_joined.insert(_id);
				}
			}

		private:
			std::string   _what;
			RE::TESFormID _id;
		};

		// a form as the script's Int argument (it does Game.GetForm): an NPC with other scripts on them can come through
		// as an object Papyrus won't take as an Actor
		std::int32_t FormArg(const RE::TESForm* a_form)
		{
			return a_form ? static_cast<std::int32_t>(a_form->GetFormID()) : 0;
		}

		std::string What(RE::Actor* a_actor, const char* a_function)
		{
			return a_actor ? std::format("{} ({:08X}).{}", a_actor->GetDisplayFullName(), a_actor->GetFormID(), a_function) : std::string(a_function);
		}

		template <class... Args>
		bool CallQuest(const char* a_function, const std::string& a_what, const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>& a_callback,
			Args... a_args)
		{
			auto* game = RE::GameVM::GetSingleton();
			auto  vm = game ? game->GetVM() : nullptr;
			auto* quest = Forms::Quest();
			if (!vm || !quest) {
				return false;
			}
			auto&      handles = vm->GetObjectHandlePolicy();
			const auto handle = handles.GetHandleForObject(RE::BSScript::GetVMTypeID<RE::TESQuest>(), quest);
			if (handle == handles.EmptyHandle()) {
				log::warn("{}: no script handle for the quest", a_what);
				return false;
			}
			const RE::BSFixedString script{ "fo4rouletteQuest" };
			const RE::BSFixedString function{ a_function };
			const bool              ok = vm->DispatchMethodCall(
                handle, script, function,
                [&](RE::BSScrapArray<RE::BSScript::Variable>& a_out) {
                    a_out = RE::BSScript::detail::PackVariables(a_args...);
                    return true;
                },
                a_callback);
			log::info("{} {}", a_what, ok ? "sent" : "failed (is fo4rouletteQuest.pex installed?)");
			return ok;
		}

		template <class... Args>
		bool CallActor(RE::Actor* a_actor, const char* a_function, Args... a_args)
		{
			auto* game = RE::GameVM::GetSingleton();
			auto  vm = game ? game->GetVM() : nullptr;
			if (!vm || !a_actor) {
				return false;
			}
			auto&      handles = vm->GetObjectHandlePolicy();
			const auto handle = handles.GetHandleForObject(RE::BSScript::kActor, a_actor);
			if (handle == handles.EmptyHandle()) {
				log::warn("{}: no script handle for {:08X}", a_function, a_actor->GetFormID());
				return false;
			}
			const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new LogResult(What(a_actor, a_function)) };
			const RE::BSFixedString script{ "Actor" };
			const RE::BSFixedString function{ a_function };
			return vm->DispatchMethodCall(
				handle, script, function,
				[&](RE::BSScrapArray<RE::BSScript::Variable>& a_out) {
					a_out = RE::BSScript::detail::PackVariables(a_args...);
					return true;
				},
				callback);
		}
	}

	bool Ready()
	{
		return Forms::Quest() && Forms::LinkKeyword();
	}

	void Join(RE::Actor* a_actor, RE::TESObjectREFR* a_target, bool a_dealer)
	{
		if (!a_actor || !a_target || !Ready()) {
			return;
		}
		const std::string what = std::format("{} to {:08X}", What(a_actor, a_dealer ? "JoinDealers" : "JoinBettors"), a_target->GetFormID());
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> done{ new JoinDone(what, a_actor->GetFormID()) };
		CallQuest("Join", what, done, FormArg(a_actor), FormArg(a_target), FormArg(Forms::LinkKeyword()), a_dealer);
	}

	bool Joined(const RE::Actor* a_actor)
	{
		std::scoped_lock lock(g_joinedMutex);
		return a_actor && g_joined.contains(a_actor->GetFormID());
	}

	void Walk(RE::Actor* a_actor, RE::TESObjectREFR* a_target, bool a_reset)
	{
		if (!a_actor || !a_target || !Ready()) {
			return;
		}
		const std::string what = std::format("{} to {:08X}{}", What(a_actor, "Walk"), a_target->GetFormID(), a_reset ? " (AI reset)" : "");
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("Walk", what, logged, FormArg(a_actor), FormArg(a_target), FormArg(Forms::LinkKeyword()), a_reset);
	}

	void Arrived(RE::Actor* a_actor)
	{
		if (!a_actor || !Ready()) {
			return;
		}
		const std::string what = What(a_actor, "Arrived");
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("Arrived", what, logged, FormArg(a_actor), FormArg(Forms::LinkKeyword()));
	}

	void Leave(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}
		{
			std::scoped_lock lock(g_joinedMutex);
			g_joined.erase(a_actor->GetFormID());
		}
		if (!Ready()) {
			return;
		}
		if (auto* keyword = Forms::LinkKeyword()) {
			a_actor->SetLinkedRef(nullptr, keyword);
		}
		const std::string what = What(a_actor, "Leave");
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("Leave", what, logged, FormArg(a_actor), FormArg(Forms::LinkKeyword()));
	}

	void ClearAll()
	{
		{
			std::scoped_lock lock(g_joinedMutex);
			g_joined.clear();
		}
		if (!Ready()) {
			return;
		}
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult("ClearAll") };
		CallQuest("ClearAll", "fo4rouletteQuest.ClearAll", logged, FormArg(Forms::LinkKeyword()));
	}

	bool MoveTo(RE::Actor* a_actor, RE::TESObjectREFR* a_target)
	{
		// ObjectReference.MoveTo(ObjectReference akTarget, float afXOffset, float afYOffset, float afZOffset, bool abMatchRotation)
		return CallActor(a_actor, "MoveTo", a_target, 0.0f, 0.0f, 0.0f, true);
	}

	bool SetRestrained(RE::Actor* a_actor, bool a_on)
	{
		// held where they stand: not moving, and (for the slow turning on the spot while they're held: following what
		// they look at round) head tracking off. Back on when they're let go (and for anyone in the quest's collections
		// when a save loads). (Their AI stays on: off, they stood frozen, not even breathing)
		const bool ok = CallActor(a_actor, "SetRestrained", a_on);
		CallActor(a_actor, "SetHeadTracking", !a_on);
		return ok;
	}

	bool LookAt(RE::Actor* a_actor, RE::TESObjectREFR* a_target)
	{
		return CallActor(a_actor, "SetLookAt", a_target, false);
	}

	bool ClearLookAt(RE::Actor* a_actor)
	{
		return CallActor(a_actor, "ClearLookAt");
	}

	bool AtPool(RE::Actor* a_actor)
	{
		const char* why = Busy(a_actor);
		return why && std::strcmp(why, "at the pool table") == 0;
	}

	const char* Busy(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return "gone";
		}
		if (auto* proc = a_actor->currentProcess; proc && proc->GetOccupiedFurniture()) {
			return "in furniture";
		}
		auto* extra = a_actor->extraList.get();
		// a settler with a job (farming, guarding, a shop, a scavenging station, a supply line): the workshop links
		// them to their work object (WorkshopLinkWork) or their caravan route
		if (auto* links = extra ? extra->GetByType<RE::ExtraLinkedRef>() : nullptr) {
			for (const auto& l : links->linkedRefs) {
				const char* id = l.keyword ? l.keyword->formEditorID.c_str() : nullptr;
				if (l.REFR && id && (std::strstr(id, "LinkWork") || std::strstr(id, "Caravan"))) {
					return "has a job";
				}
				// watching pool: Playable Pool links its watchers to their spot with its own keyword
				auto* kf = l.keyword ? l.keyword->GetFile(0) : nullptr;
				if (l.REFR && kf && _stricmp(std::string(kf->GetFilename()).c_str(), "fo4pool.esl") == 0) {
					return "at the pool table";
				}
			}
		}
		// playing or watching pool (fo4pool.esl's aliases), or a companion
		auto* aliases = extra ? extra->GetByType<RE::ExtraAliasInstanceArray>() : nullptr;
		if (!aliases) {
			return nullptr;
		}
		for (const auto& a : aliases->aliasArray) {
			auto* file = a.quest ? a.quest->GetFile(0) : nullptr;
			if (file && _stricmp(std::string(file->GetFilename()).c_str(), "fo4pool.esl") == 0) {
				return "at the pool table";
			}
			const char* q = a.quest ? a.quest->GetFormEditorID() : nullptr;
			if (q && (std::strstr(q, "Followers") || std::strstr(q, "Companion"))) {
				return "a companion";
			}
		}
		return nullptr;
	}

	namespace
	{
		std::mutex                                                                                         g_sentLock;
		std::unordered_map<RE::TESFormID, std::pair<RE::TESFormID, std::chrono::steady_clock::time_point>> g_sent;
	}

	std::pair<RE::TESFormID, std::chrono::steady_clock::time_point> LastIdleSent(RE::Actor* a_actor)
	{
		std::scoped_lock lock(g_sentLock);
		if (auto it = a_actor ? g_sent.find(a_actor->GetFormID()) : g_sent.end(); it != g_sent.end()) {
			return it->second;
		}
		return {};
	}

	void PlayIdleAfterStop(RE::Actor* a_actor, RE::TESFormID a_idle)
	{
		if (!a_actor || !Ready()) {
			return;
		}
		const std::string what = std::format("{} {:08X}", What(a_actor, "PlayIdleAfterStop"), a_idle);
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("PlayIdleAfterStop", what, logged, FormArg(a_actor), static_cast<std::int32_t>(a_idle));
	}

	void PlayIdle(RE::Actor* a_actor, RE::TESFormID a_idle)
	{
		if (!a_actor || !Ready()) {
			return;
		}
		{
			std::scoped_lock lock(g_sentLock);
			g_sent[a_actor->GetFormID()] = { a_idle, std::chrono::steady_clock::now() };
		}
		const std::string what = std::format("{} {:08X}", What(a_actor, "PlayIdle"), a_idle);
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("PlayIdleOn", what, logged, FormArg(a_actor), static_cast<std::int32_t>(a_idle));
	}

	void WatchLever(RE::Actor* a_actor, bool a_on)
	{
		if (!a_actor || !Ready()) {
			return;
		}
		const std::string what = std::format("{} {}", What(a_actor, "WatchLever"), a_on);
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("WatchLever", what, logged, FormArg(a_actor), a_on);
	}

	void SitPlayerIn(RE::TESObjectREFR* a_furniture)
	{
		if (!a_furniture || !Ready()) {
			return;
		}
		const std::string what = std::format("SitPlayerIn {:08X}", a_furniture->GetFormID());
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("SitPlayerIn", what, logged, FormArg(a_furniture));
	}

	void StandPlayerUp()
	{
		if (!Ready()) {
			return;
		}
		const std::string what = "StandPlayerUp";
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("StandPlayerUp", what, logged);
	}

	void Say(RE::Actor* a_actor, RE::TESFormID a_keyword)
	{
		if (!a_actor || !Ready()) {
			return;
		}
		const std::string what = std::format("{} {:08X}", What(a_actor, "Say"), a_keyword);
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("SayOn", what, logged, FormArg(a_actor), static_cast<std::int32_t>(a_keyword));
	}

	void SitOn(RE::Actor* a_actor, RE::TESObjectREFR* a_chair)
	{
		if (!a_actor || !a_chair || !Ready()) {
			return;
		}
		const std::string what = std::format("{} {:08X}", What(a_actor, "SitOn"), a_chair->GetFormID());
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("SitOn", what, logged, FormArg(a_actor), static_cast<std::int32_t>(a_chair->GetFormID()), FormArg(Forms::LinkKeyword()),
			FormArg(Forms::SeatKeyword()));
	}

	void SitDown(RE::Actor* a_actor, RE::TESObjectREFR* a_chair)
	{
		if (!a_actor || !a_chair || !Ready()) {
			return;
		}
		const std::string what = std::format("{} {:08X}", What(a_actor, "SitDown"), a_chair->GetFormID());
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("SitDown", what, logged, FormArg(a_actor), static_cast<std::int32_t>(a_chair->GetFormID()), FormArg(Forms::LinkKeyword()),
			FormArg(Forms::SeatKeyword()));
	}

	void ClaimChair(RE::TESObjectREFR* a_chair)
	{
		if (!a_chair || !Ready()) {
			return;
		}
		const std::string what = std::format("ClaimChair {:08X}", a_chair->GetFormID());
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> logged{ new LogResult(what) };
		CallQuest("ClaimChair", what, logged, static_cast<std::int32_t>(a_chair->GetFormID()));
	}

	std::string PackageName(RE::Actor* a_actor)
	{
		auto* proc = a_actor ? a_actor->currentProcess : nullptr;
		auto* pkg = proc ? proc->currentPackage.package : nullptr;
		if (!pkg) {
			return "none";
		}
		auto* file = pkg->GetFile(0);
		return std::format("{:08X} ({})", pkg->GetFormID(), file ? std::string(file->GetFilename()) : std::string("?"));
	}
}
