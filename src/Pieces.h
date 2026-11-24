#pragma once

// Our pieces in the world (tables and slot machines) as their models load and unload (the game's object-loaded
// event), so finding the ones near you is a look through a handful, not every object in the loaded cells: those scans
// ran twice a second each (four of them) and every 10 and 20 s, tables or none, and stuttered big interiors (a bug
// report: Vault 111, nothing built).
//
// Drop-in for util::ForEachLoadedCell + TESObjectCELL::ForEachReferenceInRange:
//     pieces::ForEachKnown([&](pieces::Known* a_cell) { a_cell->ForEachReferenceInRange(pos, range, fn); });
namespace fr::pieces
{
	void Listen();   // (at game data ready)
	void Reseed();   // one full look through the loaded cells (after a load: anything the events missed)
	bool Empty();

	struct Known
	{
		template <class F>
		void ForEachReferenceInRange(const RE::NiPoint3& a_pos, float a_range, F&& a_fn);
	};

	std::vector<RE::TESObjectREFR*> Near(const RE::NiPoint3& a_pos, float a_range);

	template <class F>
	void Known::ForEachReferenceInRange(const RE::NiPoint3& a_pos, float a_range, F&& a_fn)
	{
		for (auto* ref : Near(a_pos, a_range)) {
			if (a_fn(ref) == RE::BSContainer::ForEachResult::kStop) {
				return;
			}
		}
	}

	template <class F>
	void ForEachKnown(F&& a_fn)
	{
		Known k;
		a_fn(&k);
	}
}
