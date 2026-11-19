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

#include "Game.h"

namespace fr
{
	namespace
	{
		constexpr RE::TESFormID kWorkshopItemKeyword = 0x00054BA6;  // a workshop object's link to its workbench
		constexpr auto          kSayEvery = std::chrono::minutes(5);
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
		int& owed = _houseOwed[bench->GetFormID()];
		owed += a_caps;
		if (owed <= 0) {
			log::info("income: {} at {:08X}: {:+} (the house {} down there)", a_game, bench->GetFormID(), a_caps, -owed);
			return;
		}
		const int pay = static_cast<int>(std::floor(owed * Settings::Get().incomeShare / 100.0f));
		owed = 0;
		if (pay <= 0) {
			return;
		}
		bench->AddObjectToContainer(caps, nullptr, pay, nullptr, RE::ITEM_REMOVE_REASON::kNone);
		_houseMade += pay;
		log::info("income: {} at {:08X}: {} caps into the workbench", a_game, bench->GetFormID(), pay);
	}

	void Game::TickIncome()
	{
		if (_houseMade <= 0) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (_houseSaid.time_since_epoch().count() != 0 && now - _houseSaid < kSayEvery) {
			return;
		}
		Notify(std::format("Wasteland Wagers: the tables made {} caps for your settlement", _houseMade));
		_houseMade = 0;
		_houseSaid = now;
	}
}
