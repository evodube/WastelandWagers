#include "Pieces.h"

#include "Forms.h"
#include "Util.h"

namespace fr::pieces
{
	namespace
	{
		std::mutex                        g_mutex;
		std::unordered_set<RE::TESFormID> g_known;

		bool Ours(RE::TESObjectREFR* a_ref)
		{
			return a_ref && (Forms::IsTable(a_ref) || Forms::IsPokerTable(a_ref) || Forms::IsBlackjackTable(a_ref) ||
								Forms::IsSlotMachine(a_ref));
		}

		RE::TESObjectREFR* RefOf(RE::TESFormID a_id)
		{
			auto* f = a_id ? RE::TESForm::GetFormByID(a_id) : nullptr;
			return f ? f->As<RE::TESObjectREFR>() : nullptr;
		}

		class Sink : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent& a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
			{
				auto* ref = RefOf(a_event.formID);
				if (Ours(ref)) {
					std::scoped_lock lock(g_mutex);
					if (a_event.loaded) {
						g_known.insert(a_event.formID);
					} else {
						g_known.erase(a_event.formID);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Listen()
	{
		static Sink sink;
		static bool done = false;
		if (done) {
			return;
		}
		done = true;
		if (auto* source = RE::TESObjectLoadedEvent::GetEventSource()) {
			source->RegisterSink(&sink);
			log::info("pieces: watching models load (no scanning for tables)");
		} else {
			log::warn("pieces: no object-loaded event source");
		}
	}

	void Reseed()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->GetParentCell()) {
			return;
		}
		std::vector<RE::TESFormID> found;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(player->data.location, 1.0e9f, [&](RE::TESObjectREFR* a_ref) {
				if (Ours(a_ref) && a_ref->Get3D()) {
					found.push_back(a_ref->GetFormID());
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		std::scoped_lock lock(g_mutex);
		for (auto id : found) {
			g_known.insert(id);
		}
		log::info("pieces: {} of ours loaded", g_known.size());
	}

	bool Empty()
	{
		std::scoped_lock lock(g_mutex);
		return g_known.empty();
	}

	std::vector<RE::TESObjectREFR*> Near(const RE::NiPoint3& a_pos, float a_range)
	{
		std::vector<RE::TESFormID> ids;
		{
			std::scoped_lock lock(g_mutex);
			ids.assign(g_known.begin(), g_known.end());
		}
		std::vector<RE::TESObjectREFR*> out;
		std::vector<RE::TESFormID>      gone;
		for (auto id : ids) {
			auto* ref = RefOf(id);
			if (!ref || ref->IsDeleted()) {
				gone.push_back(id);
				continue;
			}
			if ((ref->data.location - a_pos).Length() <= a_range) {
				out.push_back(ref);
			}
		}
		if (!gone.empty()) {
			std::scoped_lock lock(g_mutex);
			for (auto id : gone) {
				g_known.erase(id);
			}
		}
		return out;
	}
}
