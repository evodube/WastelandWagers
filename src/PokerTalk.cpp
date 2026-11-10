// Table talk: the dealer's lines through a hand (in amber in the log), what settlers say when they sit back in
// against you, your session's summary when you cash out, and what each settler remembers of your table (in the
// co-save, record 'PKMM').

#include "Game.h"
#include "Settings.h"

namespace fr
{
	extern std::mutex g_npcMutex;

	std::unordered_map<RE::TESFormID, Game::PkMemory> g_pkMemory;

	namespace
	{
		template <std::size_t N>
		const char* Pick(std::mt19937& a_rng, const std::array<const char*, N>& a_lines)
		{
			return a_lines[std::uniform_int_distribution<std::size_t>(0, N - 1)(a_rng)];
		}

		constexpr std::array kNewHand{
			"CARDS IN THE AIR.",
			"SHUFFLE UP AND DEAL. NOBODY TOUCH THE RADIO.",
			"ANTE UP, WASTELANDERS.",
			"FRESH DECK. MOSTLY.",
			"BLINDS IN, HANDS ABOVE THE TABLE.",
			"NO PIPE PISTOLS AT THE TABLE, FRIENDS.",
			"HERE WE GO AGAIN.",
		};
		constexpr std::array kFlopPaired{
			"PAIRED BOARD. SOMEBODY'S SMILING.",
			"A PAIR ON THE FLOP. CAREFUL NOW.",
		};
		constexpr std::array kFlopSuited{
			"THREE OF A SUIT. FLUSH DRAWS ALL ROUND.",
			"ALL ONE SUIT. SOMEBODY'S ALREADY THERE.",
		};
		constexpr std::array kFlopConnected{
			"CONNECTED FLOP. STRAIGHTS EVERYWHERE.",
			"THAT'S A WET ONE.",
		};
		constexpr std::array kFlopDry{
			"DRY AS THE GLOWING SEA.",
			"NOTHING MUCH THERE. OR IS THERE?",
			"THE FLOP.",
		};
		constexpr std::array kTurn{
			"THE TURN. STILL TIME TO RUN.",
			"FOURTH STREET.",
			"TURN CARD'S OUT.",
		};
		constexpr std::array kRiver{
			"THE RIVER. MAY ATOM HAVE MERCY.",
			"LAST CARD. MAKE IT COUNT.",
			"RIVER'S OUT. NO MORE CARDS.",
		};
		constexpr std::array kAllIn{
			"ALL IN. THERE GOES THE PRE-WAR MONEY.",
			"EVERY CAP ON THE LINE.",
			"ALL IN! SOMEBODY GET THE STIMPAKS.",
			"THAT'S THE WHOLE STASH IN THE MIDDLE.",
		};
		constexpr std::array kBigPot{
			"BIGGEST POT SINCE THE BOMBS DROPPED.",
			"THAT'S A NUKA-COLA TRUCK'S WORTH OF CAPS.",
			"NOW THAT'S A POT.",
		};
		constexpr std::array kUncontested{
			"NO CALLERS. TAKE IT DOWN.",
			"UNCONTESTED.",
			"NOBODY WANTED IT.",
		};
		constexpr std::array kBadBeat{
			"OUCH. THAT'S COLD, EVEN FOR THE WASTELAND.",
			"A BEAT LIKE THAT LEAVES A RAD BURN.",
			"BRUTAL. JUST BRUTAL.",
		};
		constexpr std::array kBust{
			"{} IS TAPPED OUT. REST IN PIECES.",
			"THAT'S ALL FOR {}. BETTER LUCK NEXT RAID.",
			"{} IS OUT OF CAPS.",
		};
		constexpr std::array kBackLoser{
			"{}: BACK FOR MORE OF MY CAPS?",
			"{}: I'VE BEEN WAITING FOR A REMATCH.",
			"{}: NOT THIS TIME, BOSS.",
		};
		constexpr std::array kBackWinner{
			"{}: EASY MONEY'S BACK.",
			"{}: CAME TO DONATE AGAIN?",
			"{}: MY FAVOURITE PLAYER.",
		};
		constexpr std::array kBackEven{
			"{}: DEAL ME IN.",
			"{}: ROUND TWO.",
		};

		std::string Caps(std::string a_s)
		{
			for (auto& c : a_s) {
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			}
			return a_s;
		}

		bool Roll(std::mt19937& a_rng, float a_p) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(a_rng) < a_p; }

		std::string Fill(const char* a_line, const std::string& a_name)
		{
			std::string s = a_line;
			if (const auto at = s.find("{}"); at != std::string::npos) {
				s.replace(at, 2, a_name);
			}
			return s;
		}
	}

	bool Game::PkDealer(PkTalk a_what, const std::string& a_name)
	{
		if (!Settings::Get().pkBanter) {
			return false;
		}
		const char* line = nullptr;
		switch (a_what) {
		case PkTalk::kNewHand:
			line = Roll(_rng, 0.3f) ? Pick(_rng, kNewHand) : nullptr;
			break;
		case PkTalk::kFlop:
			{
				const auto& b = _pk.hand.board;
				if (b.size() < 3) {
					break;
				}
				const int r0 = pk::Rank(b[0]), r1 = pk::Rank(b[1]), r2 = pk::Rank(b[2]);
				const bool paired = r0 == r1 || r1 == r2 || r0 == r2;
				const bool suited = pk::Suit(b[0]) == pk::Suit(b[1]) && pk::Suit(b[1]) == pk::Suit(b[2]);
				const int  spread = std::max({ r0, r1, r2 }) - std::min({ r0, r1, r2 });
				if (paired) {
					line = Pick(_rng, kFlopPaired);
				} else if (suited) {
					line = Pick(_rng, kFlopSuited);
				} else if (spread <= 4) {
					line = Pick(_rng, kFlopConnected);
				} else {
					line = Pick(_rng, kFlopDry);
				}
				break;
			}
		case PkTalk::kTurn:
			line = Roll(_rng, 0.5f) ? Pick(_rng, kTurn) : nullptr;
			break;
		case PkTalk::kRiver:
			line = Roll(_rng, 0.6f) ? Pick(_rng, kRiver) : nullptr;
			break;
		case PkTalk::kAllIn:
			line = Pick(_rng, kAllIn);
			break;
		case PkTalk::kBigPot:
			line = Pick(_rng, kBigPot);
			break;
		case PkTalk::kUncontested:
			line = Roll(_rng, 0.35f) ? Pick(_rng, kUncontested) : nullptr;
			break;
		case PkTalk::kBadBeat:
			line = Pick(_rng, kBadBeat);
			break;
		case PkTalk::kBust:
			line = Pick(_rng, kBust);
			break;
		}
		if (!line) {
			return false;
		}
		PkSay("DEALER: " + Fill(line, a_name), 3);
		return true;
	}

	void Game::PkGreet(PkSeat& a_st)
	{
		PkMemory m;
		{
			std::scoped_lock lock(g_npcMutex);
			if (const auto it = g_pkMemory.find(a_st.id); it != g_pkMemory.end()) {
				m = it->second;
			}
		}
		if (m.hands <= 0) {
			return;  // a stranger to your table
		}
		const auto& s = Settings::Get();
		const int   big = s.pkBigBlind * 20;
		// down a lot at your table: they come back sore (a little on tilt)
		if (m.net <= -big) {
			a_st.tilt = std::min(1.0, a_st.tilt + 0.25 * a_st.style.tiltProne + 0.1);
			log::info("poker: {} remembers losing {} here", a_st.name, -m.net);
		}
		if (!Settings::Get().pkBanter) {
			return;
		}
		const char* line = m.net <= -big ? Pick(_rng, kBackLoser) : m.net >= big ? Pick(_rng, kBackWinner) : Pick(_rng, kBackEven);
		PkSay(Fill(line, Caps(a_st.name)), 0);
	}

	void Game::PkRemember(const PkSeat& a_st, int a_change)
	{
		if (a_st.you || a_st.id == 0) {
			return;
		}
		std::scoped_lock lock(g_npcMutex);
		auto& m = g_pkMemory[a_st.id];
		++m.hands;
		m.net += a_change;
		if (a_change > m.biggest) {
			m.biggest = a_change;
		}
	}

	void Game::PkSessionEnd(int a_cashOut)
	{
		auto& ss = _pk.session;
		if (ss.hands <= 0 && ss.buyIn <= 0) {
			ss = PkSession{};
			return;
		}
		const int net = a_cashOut - ss.buyIn;
		const auto text = std::format("Poker: {} hands, {} won, biggest pot {}. You're {} {} caps", ss.hands, ss.won, ss.biggest,
			net >= 0 ? "up" : "down", std::abs(net));
		log::info("poker session: {} hands, {} won, biggest {}, bought in {}, out with {}", ss.hands, ss.won, ss.biggest, ss.buyIn, a_cashOut);
		Notify(text);
		_pk.bannerTitle = net >= 0 ? std::format("UP {} CAPS", net) : std::format("DOWN {} CAPS", -net);
		_pk.bannerSub = std::format("{} HANDS · {} WON · BIGGEST POT {}", ss.hands, ss.won, ss.biggest);
		_pk.bannerTone = net >= 0 ? 1 : 2;
		_pk.bannerAge = 0.0f;
		ss = PkSession{};
	}

	// ---------------------------------------------------------------- the co-save

	void Game::SavePokerMemory(const F4SE::SerializationInterface* a_intfc)
	{
		// (called with g_npcMutex held)
		if (!a_intfc->OpenRecord(kPkMemoryType, kPkMemoryVersion)) {
			return;
		}
		const auto n = static_cast<std::uint32_t>(g_pkMemory.size());
		a_intfc->WriteRecordData(&n, sizeof(n));
		for (const auto& [id, m] : g_pkMemory) {
			a_intfc->WriteRecordData(&id, sizeof(id));
			a_intfc->WriteRecordData(&m.hands, sizeof(m.hands));
			a_intfc->WriteRecordData(&m.net, sizeof(m.net));
			a_intfc->WriteRecordData(&m.biggest, sizeof(m.biggest));
		}
	}

	void Game::LoadPokerMemory(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version)
	{
		// (called with g_npcMutex held)
		if (a_version != kPkMemoryVersion) {
			return;
		}
		std::uint32_t n = 0;
		if (!a_intfc->ReadRecordData(&n, sizeof(n))) {
			return;
		}
		for (std::uint32_t i = 0; i < n; ++i) {
			RE::TESFormID id = 0;
			PkMemory      m;
			if (!a_intfc->ReadRecordData(&id, sizeof(id)) || !a_intfc->ReadRecordData(&m.hands, sizeof(m.hands)) ||
				!a_intfc->ReadRecordData(&m.net, sizeof(m.net)) || !a_intfc->ReadRecordData(&m.biggest, sizeof(m.biggest))) {
				break;
			}
			if (const auto resolved = a_intfc->ResolveFormID(id)) {
				g_pkMemory[*resolved] = m;
			}
		}
		log::info("co-save: {} settlers remember your poker table", g_pkMemory.size());
	}
}
