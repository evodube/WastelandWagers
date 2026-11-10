// Table talk: what settlers say when they sit back in against you, your session's summary when you cash out, and what
// each settler remembers of your table (in the co-save, record 'PKMM').

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

		std::string Fill(const char* a_line, const std::string& a_name)
		{
			std::string s = a_line;
			if (const auto at = s.find("{}"); at != std::string::npos) {
				s.replace(at, 2, a_name);
			}
			return s;
		}
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
