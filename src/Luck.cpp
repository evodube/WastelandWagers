// Your Luck (SPECIAL) tilting the games your way, and the settlements' takings from the tables built in them.
//
// Luck: every game is still drawn fairly first. Then, now and then (more often the luckier you are: (Luck - 1) x 2%,
// times [Table] fLuck; Luck 10 about one in six), the game gives you a second chance at it: roulette draws a second
// number and keeps whichever pays you more, a slot pull that lost is pulled again, a blackjack card that would bust you
// is swapped for one a few cards on in the shoe (and the dealer's that would beat you), your poker hole cards swapped
// for the next two in the deck if they're better. Nothing you can see: the cards come off the shoe, the ball's thrown.
//
// Income: settlers play with their own caps. What the house wins from them at a table built in a settlement (less what
// it pays out to them: a run of their winning is made up first) goes into that settlement's workbench; at poker,
// which has no house, the card room takes a cut of what a settler leaves the table up. Kit tables (in no settlement)
// earn nothing.
//
// Away: tables only play with you near them, so with you away each settlement's tables earn a day's takings instead:
// [Income] fAwayPerSettler caps a game day for each table, for each settler living there (the workbench's Population;
// a slot machine half), give or take a quarter, into its workbench (caps straight in, as the vanilla stores' are).
// Each settlement's pieces are noted when you're near them (and kept in the co-save, 'INCM'), counted again each time
// (scrapped ones dropped), and paid every six game hours you're away, up to 30 days at a time.

#include "Game.h"
#include "Forms.h"
#include "Settings.h"
#include "Util.h"

namespace fr
{
	namespace
	{
		constexpr RE::TESFormID kWorkshopItemKeyword = 0x00054BA6;  // a workshop object's link to its workbench
		constexpr auto          kSayEvery = std::chrono::minutes(5);
		constexpr std::uint32_t kIncomeType = 0x494E434D;  // 'INCM'
		constexpr std::uint32_t kIncomeVersion = 1;
		constexpr float         kScanEvery = 10.0f;       // seconds
		constexpr float         kScanRange = 12000.0f;    // a settlement's pieces noted within this of you
		constexpr float         kPayEvery = 0.25f;        // game days (six hours)
		constexpr float         kMostDays = 30.0f;

		struct Site
		{
			float                                  lastDay{ -1.0f };  // game days passed, when last paid (or you were last there)
			int                                    owed{ 0 };         // what the house paid settlers more than it took, to make up
			std::map<RE::TESFormID, std::uint8_t>  pieces;            // its tables and slot machines: 0 roulette 1 poker 2 blackjack 3 slots
		};
		std::mutex                       g_siteMutex;
		std::map<RE::TESFormID, Site>    g_sites;  // by workbench

		float DaysPassed()
		{
			auto* cal = RE::Calendar::GetSingleton();
			return cal && cal->gameDaysPassed ? cal->gameDaysPassed->value : -1.0f;
		}

		RE::ActorValueInfo* PopulationAV()
		{
			static RE::ActorValueInfo* av = nullptr;
			static bool                looked = false;
			if (!looked) {
				looked = true;
				if (auto* data = RE::TESDataHandler::GetSingleton()) {
					for (auto* f : data->GetFormArray<RE::ActorValueInfo>()) {
						if (f && std::string_view(f->formEditorID.c_str() ? f->formEditorID.c_str() : "") == "Population") {
							av = f;
							break;
						}
					}
				}
				log::info("income: the Population rating {}", av ? std::format("is {:08X}", av->GetFormID()) : std::string("wasn't found"));
			}
			return av;
		}

		RE::TESObjectREFR* RefOf(RE::TESFormID a_id)
		{
			auto* f = a_id ? RE::TESForm::GetFormByID(a_id) : nullptr;
			return f ? f->As<RE::TESObjectREFR>() : nullptr;
		}

		const char* KindName(std::uint8_t a_k)
		{
			return a_k == 0 ? "roulette" : a_k == 1 ? "poker" : a_k == 2 ? "blackjack" : "slot machine";
		}
	}

	float Game::LuckChance() const
	{
		const float k = Settings::Get().luck;
		auto*       player = RE::PlayerCharacter::GetSingleton();
		auto*       avs = RE::ActorValue::GetSingleton();
		if (k <= 0.0f || !player || !avs || !avs->luck) {
			return 0.0f;
		}
		const float luck = player->GetActorValue(*avs->luck);
		return std::clamp((luck - 1.0f) * 0.02f * k, 0.0f, 0.4f);
	}

	bool Game::LuckSmiles(const char* a_what)
	{
		const float p = LuckChance();
		if (p <= 0.0f || std::uniform_real_distribution<float>(0.0f, 1.0f)(_rng) >= p) {
			return false;
		}
		log::info("luck: {} ({:.0f}% a game with your Luck)", a_what, p * 100.0f);
		return true;
	}

	RE::TESObjectREFR* Game::Workbench(RE::TESObjectREFR* a_table) const
	{
		auto* f = RE::TESForm::GetFormByID(kWorkshopItemKeyword);
		auto* kw = f ? f->As<RE::BGSKeyword>() : nullptr;
		return a_table && kw ? a_table->GetLinkedRef(kw) : nullptr;
	}

	bool Game::EarnsIncome(RE::TESObjectREFR* a_table) const
	{
		return Settings::Get().income && Workbench(a_table) != nullptr;
	}

	void Game::HouseTake(RE::TESObjectREFR* a_table, int a_caps, const char* a_game)
	{
		if (a_caps == 0 || !Settings::Get().income) {
			return;
		}
		auto* bench = Workbench(a_table);
		auto* caps = Forms::Caps();
		if (!bench || !caps) {
			return;
		}
		int pay = 0;
		{
			std::scoped_lock lock(g_siteMutex);
			int& owed = g_sites[bench->GetFormID()].owed;
			owed += a_caps;
			if (owed <= 0) {
				log::info("income: {} at {:08X}: {:+} (the house {} down there)", a_game, bench->GetFormID(), a_caps, -owed);
				return;
			}
			pay = static_cast<int>(std::floor(owed * Settings::Get().incomeShare / 100.0f));
			owed = 0;
		}
		if (pay <= 0) {
			return;
		}
		bench->AddObjectToContainer(caps, nullptr, pay, nullptr, RE::ITEM_REMOVE_REASON::kNone);
		_houseMade += pay;
		log::info("income: {} at {:08X}: {} caps into the workbench", a_game, bench->GetFormID(), pay);
	}

	void Game::TickIncome(float a_dt)
	{
		const auto& s = Settings::Get();
		_incomeScan -= a_dt;
		if (_incomeScan <= 0.0f) {
			_incomeScan = kScanEvery;
			auto*       player = RE::PlayerCharacter::GetSingleton();
			const float today = DaysPassed();
			if (player && player->GetParentCell() && today >= 0.0f && s.income) {
				// the pieces near you, noted by settlement
				std::vector<std::pair<RE::TESFormID, std::pair<RE::TESFormID, std::uint8_t>>> seen;
				util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
					a_cell->ForEachReferenceInRange(player->data.location, kScanRange, [&](RE::TESObjectREFR* a_ref) {
						if (!a_ref || a_ref->IsDisabled() || a_ref->IsDeleted() || IsKitPreview(a_ref)) {
							return RE::BSContainer::ForEachResult::kContinue;
						}
						const int kind = Forms::IsTable(a_ref) ? 0 : Forms::IsPokerTable(a_ref) ? 1 : Forms::IsBlackjackTable(a_ref) ? 2 :
						                 Forms::IsSlotMachine(a_ref) ? 3 : -1;
						if (kind >= 0) {
							if (auto* bench = Workbench(a_ref)) {
								seen.push_back({ bench->GetFormID(), { a_ref->GetFormID(), static_cast<std::uint8_t>(kind) } });
							}
						}
						return RE::BSContainer::ForEachResult::kContinue;
					});
				});
				std::vector<std::tuple<RE::TESFormID, int, int, float>> pay;  // bench, caps, settlers, days
				{
					std::scoped_lock lock(g_siteMutex);
					for (const auto& [bench, piece] : seen) {
						auto& site = g_sites[bench];
						if (!site.pieces.contains(piece.first)) {
							log::info("income: a {} ({:08X}) in the settlement at {:08X}", KindName(piece.second), piece.first, bench);
						}
						site.pieces[piece.first] = piece.second;
						if (site.lastDay < 0.0f) {
							site.lastDay = today;
						}
					}
					for (auto it = g_sites.begin(); it != g_sites.end();) {
						auto& [benchId, site] = *it;
						auto* bench = RefOf(benchId);
						// you there (its workbench loaded, near you): the tables play, nothing for being away
						const bool there = bench && bench->Get3D() && bench->GetParentCell() &&
						                   (bench->data.location - player->data.location).Length() < s.activeDistance + 3000.0f;
						// still standing: scrapped (or moved to another settlement) pieces dropped. (With you away a piece
						// can be out of memory, its cell unloaded: kept, it's looked at again when you're there)
						for (auto p = site.pieces.begin(); p != site.pieces.end();) {
							auto* ref = RefOf(p->first);
							const bool gone = ref ? (ref->IsDeleted() || ref->IsDisabled() || (there && Workbench(ref) != bench)) : there;
							if (gone) {
								log::info("income: the {} {:08X} is gone from {:08X}", KindName(p->second), p->first, benchId);
								p = site.pieces.erase(p);
							} else {
								++p;
							}
						}
						if (!bench || (site.pieces.empty() && site.owed >= 0)) {
							it = g_sites.erase(it);
							continue;
						}
						if (there || !s.awayIncome || site.lastDay < 0.0f || today < site.lastDay) {
							site.lastDay = today;
							++it;
							continue;
						}
						const float days = today - site.lastDay;
						if (days < kPayEvery) {
							++it;
							continue;
						}
						site.lastDay = today;
						auto*     pop = PopulationAV();
						const int settlers = pop ? static_cast<int>(bench->GetActorValue(*pop)) : 0;
						float     units = 0.0f;
						for (const auto& [ref, kind] : site.pieces) {
							units += kind == 3 ? 0.5f : 1.0f;
						}
						const float give = std::uniform_real_distribution<float>(0.75f, 1.25f)(_rng);
						const int   caps = static_cast<int>(std::floor(units * static_cast<float>(std::clamp(settlers, 0, 40)) * s.awayPerSettler *
                                                                     std::min(days, kMostDays) * give * s.incomeShare / 100.0f));
						if (caps > 0) {
							pay.emplace_back(benchId, caps, settlers, days);
						}
						++it;
					}
				}
				for (const auto& [benchId, caps, settlers, days] : pay) {
					if (auto* bench = RefOf(benchId); bench && Forms::Caps()) {
						bench->AddObjectToContainer(Forms::Caps(), nullptr, caps, nullptr, RE::ITEM_REMOVE_REASON::kNone);
						_awayMade += caps;
						log::info("income: away: {} caps into the workbench {:08X} ({} settlers, {:.2f} days)", caps, benchId, settlers, days);
					}
				}
			}
		}
		if (_houseMade <= 0 && _awayMade <= 0) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (_houseSaid.time_since_epoch().count() != 0 && now - _houseSaid < kSayEvery) {
			return;
		}
		if (_awayMade > 0) {
			Notify(std::format("Wasteland Wagers: your settlements' tables made {} caps while you were away", _awayMade));
		}
		if (_houseMade > 0) {
			Notify(std::format("Wasteland Wagers: the tables made {} caps for your settlement", _houseMade));
		}
		_houseMade = _awayMade = 0;
		_houseSaid = now;
	}

	void Game::SaveIncome(const F4SE::SerializationInterface* a_intfc)
	{
		std::scoped_lock lock(g_siteMutex);
		if (!a_intfc->OpenRecord(kIncomeType, kIncomeVersion)) {
			return;
		}
		const auto n = static_cast<std::uint32_t>(g_sites.size());
		a_intfc->WriteRecordData(&n, sizeof(n));
		for (const auto& [bench, site] : g_sites) {
			a_intfc->WriteRecordData(&bench, sizeof(bench));
			a_intfc->WriteRecordData(&site.lastDay, sizeof(site.lastDay));
			a_intfc->WriteRecordData(&site.owed, sizeof(site.owed));
			const auto m = static_cast<std::uint32_t>(site.pieces.size());
			a_intfc->WriteRecordData(&m, sizeof(m));
			for (const auto& [ref, kind] : site.pieces) {
				a_intfc->WriteRecordData(&ref, sizeof(ref));
				a_intfc->WriteRecordData(&kind, sizeof(kind));
			}
		}
		log::info("co-save: {} settlements' tables written", g_sites.size());
	}

	void Game::LoadIncome(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version)
	{
		if (a_version != kIncomeVersion) {
			return;
		}
		std::scoped_lock lock(g_siteMutex);
		std::uint32_t    n = 0;
		if (!a_intfc->ReadRecordData(&n, sizeof(n))) {
			return;
		}
		for (std::uint32_t i = 0; i < n; ++i) {
			RE::TESFormID bench = 0;
			Site          site;
			std::uint32_t m = 0;
			if (!a_intfc->ReadRecordData(&bench, sizeof(bench)) || !a_intfc->ReadRecordData(&site.lastDay, sizeof(site.lastDay)) ||
				!a_intfc->ReadRecordData(&site.owed, sizeof(site.owed)) || !a_intfc->ReadRecordData(&m, sizeof(m))) {
				return;
			}
			for (std::uint32_t k = 0; k < m; ++k) {
				RE::TESFormID ref = 0;
				std::uint8_t  kind = 0;
				if (!a_intfc->ReadRecordData(&ref, sizeof(ref)) || !a_intfc->ReadRecordData(&kind, sizeof(kind))) {
					return;
				}
				if (const auto r = a_intfc->ResolveFormID(ref)) {
					site.pieces[*r] = kind;
				}
			}
			if (const auto b = a_intfc->ResolveFormID(bench)) {
				g_sites[*b] = std::move(site);
			}
		}
		log::info("co-save: {} settlements' tables read", g_sites.size());
	}

	void Game::RevertIncome()
	{
		std::scoped_lock lock(g_siteMutex);
		g_sites.clear();
	}
}
