#pragma once

namespace fr
{
	// Data\F4SE\Plugins\fo4roulette.ini. Read at startup and each time you sit down at a table, so edits apply
	// without restarting the game. Every key is also on the mod's pages in F4SE Menu Framework's menu.
	struct Settings
	{
		static Settings& Get()
		{
			static Settings instance;
			return instance;
		}

		void Load();

		// a key's name for messages ("F", "Numpad 1")
		static std::string Key(int a_vk);

		// [Keys] virtual-key codes. Spin 0 = the game's activate key.
		// (no spin key since 0.7.2: the dealer spins the wheel)
		int keyLeave{ 0x58 };    // X
		int keyView{ 0x56 };     // V: the layout or the wheel
		int keyRebet{ 0x42 };    // B: last round's bets again
		int keyClear{ 0x43 };    // C: take your chips back
		int keyPackUp{ 0x50 };   // P: at a table set up from a kit, pack it back into the kit
		int keyTurnLeft{ 0x51 };  // Q: turn the view left at the poker table
		int keyTurnRight{ 0x45 }; // E
		int keyZoom{ 0x5A };      // Z: poker, held: zooms in where you look (the button keeps it)
		std::array<int, 4> keyChip{ 0x31, 0x32, 0x33, 0x34 };  // 1-4: the 1, 5, 25 and 100 chips

		// [Table]
		int   wheel{ 1 };                  // 0 European (single 0), 1 American (0 and 00, the default)
		float spinDelay{ 15.0f };          // the wheel spins itself this long after the round's first bet
		int   maxBet{ 500 };               // caps on one bet
		float activeDistance{ 3000.0f };   // a table this close to you is played at (NPCs come)

		// [Camera]
		float layoutHeight{ 98.0f };      // the top view over the layout, above the felt
		float wheelHeight{ 60.0f };        // ...over the wheel
		bool  wheelViewOnSpin{ true };     // the camera goes over the wheel while the ball's going round
		float cursorSpeed{ 0.07f };        // how far the cursor moves per bit of mouse movement

		// [Bettors] settlers who come over and play
		bool  bettors{ true };
		int   maxBettors{ 4 };
		bool  bettorIdles{ true };         // they cheer, clap, shake their heads, look the layout over
		bool  logChips{ false };           // log the poker table's chips: moves, and anything else moving, hiding or fading them ([chip])
		bool  logTurning{ false };         // log what settlers held at a table are doing when they turn (fo4roulette.log, [turn])
		float bettorEvery{ 12.0f };        // seconds between looking for someone to come over
		float bettorRange{ 1500.0f };      // ...within this far of the table
		int   bettorMinRounds{ 2 };
		int   bettorMaxRounds{ 6 };

		// [Poker]
		int   pkSmallBlind{ 5 };
		int   pkBigBlind{ 10 };
		int   pkBuyIn{ 500 };              // the most of your caps you sit down with
		bool  pkPlayers{ true };           // settlers come and sit in
		int   pkMaxPlayers{ 5 };           // settlers at the table at once (up to 6)
		int   pkNpcBuyIn{ 300 };           // the most a settler sits down with
		float pkThink{ 1.0f };             // how long settlers think (x)
		bool  pkChairs{ true };            // settlers sit at the poker table (chairs put round it)
		bool  bjChairs{ true };            // settlers sit at the blackjack table (chairs put round it)
		bool  pkCardsDown{ false };        // your cards face down (on the table and the HUD) till you show them (SHOW MY CARDS)
		bool  pkVoices{ true };            // settlers cheer, laugh and groan at the end of a hand
		bool  pkBanter{ true };            // settlers who remember you greet you when they sit back in
		bool  pkTopDown{ false };          // the view from above the table rather than from your seat (V switches at the table)
		float pkTopHeight{ 108.0f };       // ...that view's height over the felt
		float pkTopShift{ 0.0f };          // ...and how far it's moved towards your side from the middle of the table
		float pkSeatBack{ 42.0f };         // the view from your seat: how far behind your cards
		float pkSeatHeight{ 46.0f };       // ...how high over the felt
		float pkSeatAim{ 30.0f };          // ...how far in front of your cards it looks

		// [HUD]
		float hudScale{ 0.9f };
		bool  notifications{ false };      // results as the game's corner messages too

		// [Sound]
		bool  sound{ true };
		float soundVolume{ 0.8f };
		bool  turnChime{ true };   // a chime when it's your turn at the poker or blackjack table (or bets open)

		// [Forms]
		std::string plugin{ "fo4roulette.esl" };
	};
}
