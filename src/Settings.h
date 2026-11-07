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
		int keySpin{ 0 };
		int keyLeave{ 0x58 };    // X
		int keyView{ 0x56 };     // V: the layout or the wheel
		int keyRebet{ 0x42 };    // B: last round's bets again
		int keyClear{ 0x43 };    // C: take your chips back
		std::array<int, 4> keyChip{ 0x31, 0x32, 0x33, 0x34 };  // 1-4: the 1, 5, 25 and 100 chips

		// [Table]
		int   wheel{ 0 };                  // 0 European (single 0), 1 American (0 and 00)
		float spinDelay{ 15.0f };          // the wheel spins itself this long after the round's first bet
		int   maxBet{ 500 };               // caps on one bet
		float activeDistance{ 3000.0f };   // a table this close to you is played at (NPCs come)

		// [Camera]
		float layoutHeight{ 115.0f };      // the top view over the layout, above the felt
		float wheelHeight{ 70.0f };        // ...over the wheel
		bool  wheelViewOnSpin{ true };     // the camera goes over the wheel while the ball's going round
		float cursorSpeed{ 0.07f };        // how far the cursor moves per bit of mouse movement

		// [Bettors] settlers who come over and play
		bool  bettors{ true };
		int   maxBettors{ 3 };
		bool  bettorIdles{ true };         // they cheer, clap, shake their heads, look the layout over
		float bettorEvery{ 12.0f };        // seconds between looking for someone to come over
		float bettorRange{ 1500.0f };      // ...within this far of the table
		int   bettorMinRounds{ 2 };
		int   bettorMaxRounds{ 6 };

		// [HUD]
		float hudScale{ 1.4f };
		float hudRange{ 350.0f };          // away from the table, its HUD shows within this far (350 = 5 m)
		bool  notifications{ false };      // results as the game's corner messages too

		// [Sound]
		bool  sound{ true };
		float soundVolume{ 0.8f };

		// [Forms]
		std::string plugin{ "fo4roulette.esl" };
	};
}
