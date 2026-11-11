#pragma once

#include "rl/Layout.h"
#include "rl/Wheel.h"
#include "pk/Holdem.h"
#include "bj/Blackjack.h"
#include "Audio.h"

namespace fr
{
	// The roulette table in play: the one nearest you (within [Table] fActiveDistance). Its wheel turns, the ball
	// comes to it, settlers come over to bet, and you sit down at it by activating it.
	//
	// Everything runs on the game's main thread (Tick, queued from the render thread every frame) except OnPresent
	// (render thread), the camera hooks (the camera update; they lock _camMutex) and Hud() (the UI thread; _hudMutex).
	class Game
	{
	public:
		static Game& Get()
		{
			static Game instance;
			return instance;
		}

		// render thread, every frame: keys and mouse buttons, and the main-thread tick queued
		void OnPresent();

		// a save's about to be written: our objects off (and your chips' caps back in your pocket for the save);
		// written: all back
		void BeforeSave();
		void AfterSave();
	private:
		void BeforeSaveTable();  // the table in _t
		void AfterSaveTable();
	public:
		// a save's about to load: everything we had is gone with the old game
		void Forget();
		void OnLoadingStarts();  // a loading screen (a door, fast travel): the table's game ends, chips back
		static void Listen();    // activations and loading screens
		void OnActivated(RE::TESObjectREFR* a_ref);  // main thread: something activated by you

		// the Roulette Table Kit (Kit.cpp)
		void        SetUpKit();           // game data ready: in Vadim's stock, and its use listened for
		void        StockKit();           // a save loaded: one in his stock now if this save never had one
		void        BeginKitPlacement(int a_kind);  // used from the Pip-Boy: 0 the roulette kit, 1 the poker kit
		static bool IsKitTable(const RE::TESObjectREFR* a_table);
		static bool IsKitPreview(const RE::TESObjectREFR* a_ref);
		static void SaveKit(const F4SE::SerializationInterface* a_intfc);
		static void LoadKit(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version);
		static void RevertKit();
		static constexpr std::uint32_t kKitRecordType = 0x5354494B;  // 'KITS'
		static constexpr std::uint32_t kKitRecordVersion = 1;

		// what each settler remembers of your poker table (PokerTalk.cpp; the co-save's 'PKMM')
		struct PkMemory
		{
			int hands{ 0 };    // hands played there
			int net{ 0 };      // caps won (or lost, below 0) there, all told
			int biggest{ 0 };  // the most they've won in a hand
		};
		static void SavePokerMemory(const F4SE::SerializationInterface* a_intfc);
		static void LoadPokerMemory(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version);
		static constexpr std::uint32_t kPkMemoryType = 0x4D4D4B50;  // 'PKMM'
		static constexpr std::uint32_t kPkMemoryVersion = 1;

		// the F4SE co-save: who deals where, the settlers' purses
		static void Save(const F4SE::SerializationInterface* a_intfc);
		static void Load(const F4SE::SerializationInterface* a_intfc);
		static void Revert(const F4SE::SerializationInterface* a_intfc);

		// camera hooks (CameraHook.cpp)
		RE::TESObjectREFR* PkChairTable(const RE::TESObjectREFR* a_ref) const;  // one of the poker table's chairs: its table
		bool AtATable() const { return _seated.load() || _pkSeated.load() || _bjSeated.load(); }  // sitting at a roulette, poker or blackjack table
		bool AtCardTable() const { return _pkSeated.load() || _bjSeated.load(); }  // (poker or blackjack: the pointer and the buttons)
		void BjAnimate();  // the blackjack table's cards and chips moved, once a frame (the camera's update)
		bool SwallowButton(const RE::ButtonEvent* a_event);  // a menu key that's ours at the poker table (Q, E, the workshop's)
		bool OnLookMouse(const RE::MouseMoveEvent* a_event);
		void BeforeFreeCameraUpdate(RE::FreeCameraState* a_free);
		void OnFreeCameraLook(RE::FreeCameraState* a_free, const RE::BSTPoint2<float>& a_before);
		void OnFreeCameraUpdate(RE::FreeCameraState* a_free);
		void PkAnimate();
		bool FreeCameraTranslation(const RE::FreeCameraState* a_free, RE::NiPoint3& a_out);

		struct HudLine
		{
			std::string text;
			int         tone{ 0 };  // 0 normal, 1 good (green), 2 bad (red)
		};

		// what the HUD shows; safe from any thread
		struct HudState
		{
			bool                 seated{ false };      // at the table, the layout camera on
			bool                 nearby{ false };      // not seated, but a game's on at a table you're by
			bool                 american{ false };
			int                  phase{ 0 };           // 0 bets open, 1 the ball's going round, 2 the result
			int                  caps{ 0 };
			int                  onTable{ 0 };         // your caps on the layout
			int                  chip{ 0 };            // the chip you're betting with: 0..3 = 1, 5, 25, 100
			std::string          hover;                // the bet under the cursor ("SPLIT 17/20")
			int                  hoverPays{ 0 };
			int                  hoverMine{ 0 };
			int                  hoverAll{ 0 };        // everyone's caps on it
			struct MyBet
			{
				std::string name;    // "SPLIT 17/20"
				int         amount{ 0 };
				int         won{ -1 };  // -1 not settled, 0 lost, else what it paid
			};
			std::vector<MyBet>   myBets;               // your bets on the layout
			float                spinIn{ -1.0f };      // seconds before the wheel spins itself; -1 no bets yet
			bool                 spinning{ false };
			std::vector<std::string> players;          // the NPCs at the table, "PIPER  120 CAPS"
			std::vector<int>     history;              // last numbers, newest first
			std::deque<HudLine>  log;                  // the latest things that happened, oldest first
			int                  banner{ 0 };          // 0 none, 1 no more bets, 2 result (you won), 3 result (you didn't), 4 result (no bets of yours)
			std::string          bannerTitle;
			std::string          bannerSub;
			int                  bannerNumber{ -1 };
			float                bannerAge{ 0.0f };
			bool                 wheelView{ false };
			bool                 kitTable{ false };    // (roulette: a table from a kit, PACK UP on offer)
			std::array<std::string, 7> rlKeys;         // (roulette's buttons: their keys, (unused), SAME AGAIN, CLEAR, WHEEL, LEAVE, PACK UP, chips)
			std::string          keys;                 // the key hints line
			bool                 placing{ false };     // setting a kit table up: just the key hints
			int                  lastWin{ 0 };

			// at a poker table (Poker.cpp) instead
			struct PokerRow
			{
				std::string        name;
				int                stack{ 0 };
				int                bet{ 0 };
				std::string        status;          // their last action, "FOLDED", "ALL IN", "THINKING"
				bool               toAct{ false };
				bool               bigBlind{ false };  // poker: the big blind this hand (a B by their name)
				bool               you{ false };
				bool               folded{ false };
				std::array<int, 2> shown{ -1, -1 };  // their cards, at a showdown
				float              think{ -1.0f };   // thinking: the time left of it (1 .. 0), or -1
			};
			bool                  poker{ false };
			std::vector<PokerRow> pkRows;
			std::vector<int>      pkBoard;
			std::array<int, 2>    pkHole{ -1, -1 };   // yours
			std::string           pkHand;             // what yours makes ("PAIR OF KINGS")
			int                   pkPot{ 0 };
			std::string           pkInfo;             // "BLINDS 5/10 · THE FLOP"
			bool                  pkYourTurn{ false };
			// the buttons (Hud.cpp draws them under your mouse pointer; Game clicks them)
			float                 pkPtrX{ -1.0f }, pkPtrY{ -1.0f };  // the pointer, in screen pixels
			int                   pkToCall{ 0 };
			int                   pkRaiseTo{ 0 };
			int                   pkAllIn{ 0 };
			int                   pkMinRaise{ 0 }, pkMaxRaise{ 0 };  // the raise slider's ends
			bool                  pkKit{ false };
			bool                  pkPeek{ false };
			bool                  pkTop{ false };
			bool                  pkZoom{ false };
			bool                  pkCardsDown{ false };  // (the setting: your cards face down till you show them)
			bool                  pkDealer{ true };   // the board's cards shown on the HUD, under your hand
			int                   pkPre{ 0 };         // your action picked ahead: 0 none, 1 check / fold, 2 check, 3 call any
			bool                  pkCanPre{ false };  // in the hand, not your turn: an action can be picked ahead
			std::vector<int>      pkWinFive;          // at the showdown: the winning five (cards)
			std::array<int, 2>    pkWinHole{ -1, -1 };  // ...the winner's own two
			std::string           pkWinWho;           // ...and who ("PIPER", "YOU")
			std::string           pkWinName;          // ...with what ("A FLUSH")
			float                 pkAge{ 99.0f };     // seconds since you sat down (the panels slide in)
			bool                  pkPad{ false };     // a controller in use: its buttons on the HUD
			std::string           pkHelp;             // what's happening / what to do, under the board
			std::string           pkTitle;            // the status panel's heading (empty: poker's)
			std::string           pkBoardLabel;       // the panel top right: what its number is (empty: POT)

			// at the blackjack table (Blackjack.cpp; poker's panels and buttons where they fit)
			bool                  blackjack{ false };
			struct BjHand
			{
				std::vector<int> cards;
				std::string      value;     // "16", "SOFT 18", "BLACKJACK", "BUST"
				int              bet{ 0 };
				bool             active{ false };
				std::string      result;    // "WON 50", "LOST", "PUSH" once it's settled
			};
			std::vector<BjHand>   bjHands;            // yours
			std::vector<int>      bjDealer;           // the dealer's cards (52: the one face down)
			std::string           bjDealerValue;
			int                   bjPhase{ 0 };       // 0 bets, 1 dealing / others playing, 2 your turn, 3 insurance offered, 4 the dealer, 5 settled
			int                   bjBet{ 0 };         // your bet for the next hand
			int                   bjLastBet{ 0 };
			bool                  bjCanDouble{ false };
			bool                  bjCanSplit{ false };
			int                   bjInsurance{ 0 };   // what insurance would cost you
			float                 bjDealIn{ -1.0f };  // seconds before the cards come out (-1: waiting for a bet)
			bool                  bjShowHand{ true };   // the SHOW / HIDE HAND button: your hand's panel
			bool                  bjShowDealer{ true }; // the SHOW / HIDE DEALER button: the dealer's panel
		};
		HudState Hud() const;

		// the poker buttons as Hud.cpp drew them this frame (screen pixels), and the screen's size
		struct PkButton
		{
			int   id{ 0 };  // what PkKey does: 0 fold, 1 check/call, 2 raise, 3 all in, 4/5 raise up/down, 6 step away, 7 pack up,
			                // 8 cash out, 9 view, 10 show your cards, 11 the raise slider
			float x0{ 0 }, y0{ 0 }, x1{ 0 }, y1{ 0 };
			bool  on{ true };
			float t0{ 0 }, t1{ 0 };  // (the slider) its track's ends: x0..x1 is the whole of it, to grab anywhere on
		};
		void SetPkButtons(std::vector<PkButton> a_buttons, float a_screenW, float a_screenH);
		void CardKey(int a_key);  // a button at the poker or the blackjack table
		void PkSetRaise(float a_frac);  // the raise slider dragged: 0 the smallest raise .. 1 all in

	private:
		struct Frame  // the table in the world
		{
			RE::NiMatrix3 rot;     // rows: the table's x, y, z axes in the world
			RE::NiPoint3  origin;
			float         scale{ 1.0f };
			float         yaw{ 0.0f };  // the table's own heading (radians)
		};

		// one bettor's chips on one bet
		struct Stake
		{
			RE::TESFormID                    owner{ 0 };  // 0 you, else the NPC
			rl::Bet                          bet;
			int                              amount{ 0 };
			std::vector<RE::ObjectRefHandle> chips;
			bool                             dirty{ true };  // its stack needs (re)making
			int                              won{ -1 };      // -1 not settled, 0 lost, else what it paid (on top of the stake)
			std::vector<RE::ObjectRefHandle> paid;           // the winnings, stacked beside it
		};

		// an NPC on their way to a spot by the table (a bettor's)
		struct Walker
		{
			RE::ObjectRefHandle marker;      // an XMarkerHeading on the spot: their linked ref
			bool                joinSent{ false };
			float               walked{ 0.0f };  // seconds on their way
			float               best{ 1.0e9f };  // the closest they've got
			float               stall{ 0.0f };   // seconds without getting closer
			float               getUp{ 0.0f };   // seconds before trying to get them out of a chair again
		};

		// a settler standing behind a table as its dealer (poker, roulette; the blackjack table has its own): walks over
		// while the table's in play and stands there
		struct Croupier
		{
			RE::ObjectRefHandle actor;
			RE::TESFormID       id{ 0 };
			std::string         name;
			Walker              walk;
			bool                held{ false };
			float               lookIn{ 0.0f };  // seconds before looking for one again
		};

		// an NPC at the table, betting
		struct Seat
		{
			RE::ObjectRefHandle actor;
			RE::TESFormID       id{ 0 };
			std::string         name;
			int                 spot{ 0 };
			Walker              walk;
			enum class Step
			{
				kWalking,
				kPlaying,
				kLeaving,
			} step{ Step::kWalking };
			bool  held{ false };       // SetRestrained on
			int   rounds{ 0 };         // rounds left before they go
			bool  betThisRound{ false };
			float nextChip{ 0.0f };    // seconds before their next chip goes down
			int   chipsToPlace{ 0 };
			int   roundNet{ 0 };       // won (or lost) on the last spin
			float nextIdle{ 6.0f };    // seconds before their next little idle while they wait
			float reactIn{ -1.0f };    // seconds before they react to the result (-1 none due)
			int   budget{ 0 };         // caps still to put down this round
			float atSpot{ 0.0f };      // seconds they've been standing at their spot
		};

		enum class Phase
		{
			kBetting,
			kSpinning,
			kResult,
		};

		void Tick();
		void PollKeys();
		static bool IsGamePaused();
		static void Notify(const std::string& a_message);
		void        Say(const std::string& a_text, int a_tone = 0);  // the HUD log (and a corner message with bNotifications)

		// tables
		void               ScanTables();
		void               ApplyWheel(RE::TESObjectREFR* a_table, rl::WheelType a_type);
		bool               FrameOf(RE::TESObjectREFR* a_table, Frame& a_out) const;
		RE::NiPoint3       ToWorld(const Frame& a_frame, double a_x, double a_y, double a_z) const;
		rl::Vec2           ToLocal(const Frame& a_frame, const RE::NiPoint3& a_world) const;
		RE::ObjectRefHandle Spawn(RE::TESBoundObject* a_form, const RE::NiPoint3& a_pos, float a_angle) const;
		static void        Remove(RE::ObjectRefHandle& a_handle);
		static void        PlaceNode(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, float a_yaw, bool a_hidden);
		void               StartSession(RE::TESObjectREFR* a_table);
		void               EndSession(const char* a_why);
		void               TickTable(float a_dt);
		void               TurnRotor(const Frame& a_frame);
		void               PlaceBall(const Frame& a_frame);

		// the round
		void StartSpin(bool a_byPlayer);
		void LaunchSpin(const Frame& a_frame);  // the throw's worked out: the ball goes in
		void TickSpin(const Frame& a_frame, float a_dt);
		void Settle();
		void ClearRound();
		bool HasBets(RE::TESFormID a_owner) const;
		int  OnTable(RE::TESFormID a_owner) const;

		// chips
		Stake* StakeFor(RE::TESFormID a_owner, const std::string& a_key);
		bool   AddChips(RE::TESFormID a_owner, const rl::Bet& a_bet, int a_amount);  // false: not taken (no money, limit)
		void   TakeBack(RE::TESFormID a_owner, const std::string& a_key);           // the whole stake back to its owner
		void   TakeAllBack(RE::TESFormID a_owner);
		void   RefundEverything();                                                   // every stake back to its owner, chips gone
		void   MakeStacks(const Frame& a_frame);
		void   DespawnStake(Stake& a_stake);
		rl::Vec2 OwnerOffset(RE::TESFormID a_owner) const;
		static int  PlayerCaps();
		static void GivePlayer(int a_caps);
		static bool TakeFromPlayer(int a_caps);

		// you at the table
		void SitDown(RE::TESObjectREFR* a_table);
		void StandUp();
		void PlaceChip();      // left button
		void RemoveChip();     // right button
		void Rebet();
		void ToggleView();
		void BlockControls(bool a_block);
		void QuietNpcs(bool a_quiet);  // the game's greetings off while you're at the table
		bool _npcsQuiet{ false };
		void TableReach();          // a longer reach for activating while you're by one of the tables (Seat.cpp)
		bool _reachLong{ false };
		float _reachWas{ 150.0f };
		void HoldPlayerView();
		void PlaceGhost(const Frame& a_frame);
		std::optional<rl::Bet> HoverBet() const;
		void UpdateHud();

		// setting a kit table up
		std::string         KitPrompt() const;
		void                TickKitPlacement();
		void                TurnKit(int a_dir);
		void                CancelKit(bool a_giveBack);
		void                ConfirmKit();
		void                PackUpTable();  // the kit table you're at, back into a kit
		void                PackUpPokerTable(RE::TESObjectREFR* a_table);
		void                PackUpBlackjackTable(RE::TESObjectREFR* a_table);
		int                 _kitKind{ 0 };
		std::atomic<bool>   _kitPlacing{ false };
		float               _kitTurn{ 0.0f };
		bool                _kitReady{ false };
		int                 _kitSign{ 0 };
		bool                _kitOk{ false };
		RE::ObjectRefHandle _kitPreview;
		RE::NiPoint3        _kitPos;
		float               _kitAngle{ 0.0f };
		float               _kitMadeAngle{ 0.0f };  // the angle the preview was made at (to tell which way its 3D turns)
		bool                _kitConfirmArmed{ false };  // the activate key went down while setting up: setting down on letting go
		static void         PlaceRoot(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot);

		// the NPCs betting
		static constexpr int kSpots = 4;  // places to stand round the layout
		void         TickSeats(const Frame& a_frame, float a_dt);
		void         FindBettor(const Frame& a_frame);
		void         ReleaseSeat(Seat& a_seat, const char* a_why);
		void         SeatBets(Seat& a_seat);
		void         SeatIdle(Seat& a_seat, RE::Actor* a_actor, int a_kind);  // 0 waiting, 1 placing a chip, 2 won, 3 lost
		RE::NiPoint3 SeatSpot(const Frame& a_frame, int a_spot, float& a_heading) const;
		float        HeadingOf(const Frame& a_frame, double a_dx, double a_dy) const;  // a direction in table space as a heading
		float        CeilingAbove(const Frame& a_frame, double a_x, double a_y, float a_upTo) const;
		bool         _clearNpcs{ true };  // a load: whoever the quest's collections hold, the plugin doesn't know
		static int   Purse(RE::TESFormID a_id);
		static void  AddToPurse(RE::TESFormID a_id, int a_caps);
		bool         WalkTo(RE::Actor* a_actor, Walker& a_walk, const Frame& a_frame, const RE::NiPoint3& a_spot, float a_heading, float a_dt,
					bool a_dealer, float a_timeout);  // true: there
		void         FaceTowards(RE::Actor* a_actor, float a_heading);
		// the dealers (Npcs.cpp): found, walked to their place (table space, facing the way given) and stood there
		bool         IsCroupier(RE::TESFormID a_id) const;  // dealing at one of the tables (blackjack's too)
		void         TickCroupier(Croupier& a_c, const Frame& a_frame, double a_x, double a_y, double a_faceX, double a_faceY, float a_dt,
					const char* a_table);
		void         ReleaseCroupier(Croupier& a_c, const char* a_why);
		void         UnholdCroupier(Croupier& a_c);  // (a save: let go, held again after)
		void         WatchTurn(RE::Actor* a_actor, float a_heading);  // (the turning log)

		// ------------------------------------------------------------ poker (Poker.cpp)
		static constexpr int kPkSeats = 6;
		struct PkSeat
		{
			bool                used{ false };
			bool                you{ false };
			bool                away{ false };  // you, up from the table with your seat kept
			RE::ObjectRefHandle actor;
			RE::TESFormID       id{ 0 };
			std::string         name;
			Walker              walk;
			Seat::Step          step{ Seat::Step::kWalking };
			bool                held{ false };
			int                 stack{ 0 };
			int                 hands{ 0 };      // hands left before they go
			float               atSpot{ 0.0f };
			pk::Style           style;
			float               nextIdle{ 8.0f };
			float               sitCheck{ 0.0f };  // (chairs) seconds before checking they're still sat down
			int                 sitTries{ 0 };     // (chairs) told to sit, and not sitting, in a row
			double              tilt{ 0.0 };       // steaming after a bad beat (cools off hand by hand)
			int                 handStart{ 0 };    // their caps when this hand began
			std::string         last;            // what they last did
			int                 hi{ -1 };        // their place in the hand being played, or -1
		};
		struct PkCard
		{
			RE::ObjectRefHandle ref;
			int                 card{ 0 };
			RE::NiPoint3        from;            // table space
			RE::NiPoint3        to;
			float               yaw{ 0.0f };     // which way its top points, table space
			float               delay{ 0.0f };   // seconds before it leaves the deck
			float               t{ 0.0f };       // 0..1 on its way
			float               flip{ 0.0f };    // 0 face down .. 1 face up, now
			bool                faceUp{ false }; // ...where it's going
			int                 seat{ -1 };      // whose hand it's in (-1 the board)
			bool                mucked{ false }; // folded: going back to the dealer, then gone
			float               spin{ 0.0f };    // turning in the air on its way (radians left to turn)
			float               flipWait{ 0.0f };  // seconds before it turns over (the showdown, one hand after another)
			float               lift{ 0.0f };    // raised off the felt now (one of the winning five)...
			float               liftTo{ 0.0f };  // ...and where it's going
		};
		// a chip on its way across the felt (to the pot, to a winner): removed when it gets there
		struct PkChipMove
		{
			RE::ObjectRefHandle ref;
			RE::NiPoint3        from, to;  // table space (z: height over the felt)
			float               t{ 0.0f };
			float               delay{ 0.0f };
			float               dur{ 0.45f };
			float               yaw{ 0.0f };
			bool                remove{ true };
			RE::NiMatrix3       rot{};            // (its turn as the game set it, kept as it moves)
			bool                haveRot{ false };
		};
		// your sitting at the poker table, from buying in to cashing out
		struct PkSession
		{
			int hands{ 0 };
			int won{ 0 };
			int biggest{ 0 };  // the biggest pot you took
			int buyIn{ 0 };
		};
		enum class PkPhase
		{
			kWaiting,
			kDealing,
			kBetting,
			kShowdown,
		};
		struct PokerState
		{
			RE::ObjectRefHandle                                    table;
			std::array<PkSeat, kPkSeats>                           seats;
			pk::Hand                                               hand;
			bool                                                   inHand{ false };
			PkPhase                                                phase{ PkPhase::kWaiting };
			float                                                  timer{ 0.0f };
			float                                                  think{ 0.0f };
			int                                                    button{ -1 };
			std::size_t                                            boardShown{ 0 };
			std::vector<PkCard>                                    cards;
			std::array<std::vector<RE::ObjectRefHandle>, kPkSeats> betChips;
			std::array<int, kPkSeats>                              betShown{};
			std::vector<RE::ObjectRefHandle>                       potChips;
			int                                                    potShown{ -1 };
			float                                                  seatTimer{ 4.0f };
			int                                                    raiseTo{ 0 };
			std::deque<HudLine>                                    log;
			std::string                                            bannerTitle;
			std::string                                            bannerSub;
			float                                                  bannerAge{ 99.0f };
			int                                                    bannerTone{ 0 };
			pk::Action                                             pending;      // what the settler thinking will do
			std::array<std::array<std::vector<RE::ObjectRefHandle>, 4>, kPkSeats> stackChips;  // each player's caps in front of them, by colour (as shown)
			std::array<RE::ObjectRefHandle, kPkSeats>              chairs;       // (the chairs option) one at each seat
			std::array<int, kPkSeats>                              stackShown{};
			std::array<std::array<int, 4>, kPkSeats>               stackCols{};  // ...how many of each colour they have
			std::array<int, 4>                                     potCount{};   // the pot's stacks, by colour
			std::array<std::vector<RE::ObjectRefHandle>, kPkSeats> landed;       // chips won, landing on the winner's stacks
			std::vector<PkChipMove>                                moves;
			float                                                  potHold{ 0.0f };
			float                                                  pushAge{ 99.0f };  // the camera's push in: seconds since
			float                                                  pushLevel{ 0.0f }; // ...and how far in it is now (0..1)
			float                                                  driftYaw{ 0.0f };  // the view turned towards the winner
			float                                                  thinkTotal{ 1.0f };
			float                                                  seatedAge{ 99.0f };
			std::vector<int>                                       winFive;           // the showdown's winning five (cards), its winner's two, who
			std::array<int, 2>                                     winHole{ -1, -1 };
			std::string                                            winWho, winName;
			float                                                  sweep{ 0.0f };  // (seconds to the next look for stray chips)
			int                                                    winnerSeat{ -1 };  // chips on their way in: the pot's stack after  // 1 you won, 2 you lost, 0 neither
			int                                                    lifted{ 0 };  // your stack, back in your pocket over a save
			bool                                                   peeking{ false };  // you're looking at your cards: they're turned up
			PkSession                                              session;
			Croupier                                               croupier;  // the dealer, by the chip tray
		};
		PokerState        _pk;
		std::atomic<bool> _pkSeated{ false };
		bool              _pkZoom{ false };  // (the zoom on the board, kept on)
		bool                                  PkAnimating() const;  // (the camera's update is moving them)
		std::chrono::steady_clock::time_point _animLast{};
		std::chrono::steady_clock::time_point _animAt{};
		bool                                  _animHave{ false };
		bool                                  _animHad{ false };
		std::atomic<std::int64_t> _afterTable{ 0 };  // (up from the poker table: the click or button that did it kept from the game until let go)
		int               _pkYou{ -1 };       // your seat (sitting there, or away with it kept)
		float             _camPitch{ -1.0f };  // the view's pitch at a poker table (below 0: straight down, the roulette layout)
		float             _pkLookYaw{ 0.0f }, _pkLookPitch{ 0.0f };  // looking about from your poker seat (under _camMutex)
		bool              _pkMenusHeld{ false };
		bool              _pkTopView{ false };  // looking down on the poker table (V)
		bool              _pkShowCards{ false };  // the SHOW MY CARDS button: your cards on the HUD
		int               _pkPre{ 0 };  // your action picked ahead (the CHECK / FOLD, CHECK, CALL ANY buttons)
		bool              _pkShowDealer{ true };  // the SHOW / HIDE DEALER button: the board's cards on the HUD
		std::atomic<bool> _rlUi{ false };  // (roulette: the pointer's down on the buttons, off the layout)
		double            _rlPush{ 0.0 };  // (...how far you've pushed the cursor down past the layout's edge)
		double            _pkPtrX{ -1.0 }, _pkPtrY{ -1.0 };  // your mouse pointer at the poker table, screen pixels (under _camMutex)
		mutable std::mutex     _btnMutex;
		std::vector<PkButton>  _pkButtons;
		float                  _scrW{ 1920.0f }, _scrH{ 1080.0f };
		bool                   _pkLmbWasDown{ false };
		WORD                   _padWas{ 0 };
		std::atomic<int>       _padTurn{ 0 };      // LB / RB held: turning the view at the poker table
		std::atomic<bool>      _padUsed{ false };  // a controller's been used at the table (its buttons shown on the HUD)
		void                   PollPad();
		bool                   _pkDragging{ false };  // the raise slider held
		RE::BSTSmartPointer<RE::BSInputEnableLayer> _pkMenuLayer;

		bool         PkFrame(Frame& a_out) const;
		RE::NiPoint3 PkStand(const Frame& a_frame, int a_seat, float& a_heading) const;  // where someone at a seat stands
		void         PkScan();
		void         PkStart(RE::TESObjectREFR* a_table);
		void         PkEnd(const char* a_why);
		void         TickPoker(float a_dt);
		void         PkFindPlayer(const Frame& a_frame);
		void         PkTickSeats(const Frame& a_frame, float a_dt);
		void         PkRelease(int a_seat, const char* a_why);
		void         PkStartHand();
		void         PkTickHand(const Frame& a_frame, float a_dt);
		void         PkAct(pk::Action a_action);  // whoever's to act does this
		void         PkEndHand();
		void         PkClearTable(bool a_cards = true);
		void         PkDeal(int a_card, const RE::NiPoint3& a_to, float a_yaw, bool a_faceUp, float a_delay, int a_seat = -1);
		void         PkSlideChips(std::vector<RE::ObjectRefHandle>& a_chips, int a_seat, float a_delay);  // to a player's stacks
		void         PkSound(Audio::Bank a_bank, float a_gain);  // a sound at the poker table
		void         PkThrowChips(std::vector<RE::ObjectRefHandle>& a_chips, float a_delay);  // into the pot, onto its stack of each colour
		void         PkTickMoves(const Frame& a_frame, float a_dt);
		void         PkWatchChips(const Frame& a_frame, float a_dt, const char* a_from);  // (the chip log)
		void         PkMuck(int a_seat);  // a fold: their cards slide back to the dealer (-2: every card on the table)
		void         PkTickCards(const Frame& a_frame, float a_dt);
		void         PkMakeChips(const Frame& a_frame);
		void         PkSay(const std::string& a_text, int a_tone = 0);
		void         PkGreet(PkSeat& a_st);                     // a settler sitting back in: what they remember of you
		void         PkRemember(const PkSeat& a_st, int a_change);  // a hand played at your table, for their memory
		void         PkSessionEnd(int a_cashOut);              // your session's summary
		void         PkSitDown(RE::TESObjectREFR* a_table);
		void         PkSweep(const Frame& a_frame);
		void         PkIdle(int a_seat, int a_kind);  // a settler's idle: 0 looking on, 1 thinking, 2 chips in, 3 won, 4 folded or lost
		void         PkHoldMenuKeys(bool a_hold);
		void         PkStandUp(bool a_cashOut = false);  // up from the table: your seat kept (or cashed out)
		void         PkCashOut();                        // away from it: your seat given up, your stack back to you
		void         PkKey(int a_key);  // 0 fold, 1 check/call, 2 raise, 3 all in, 4 raise up, 5 raise down, 6 leave, 7 pack up
		void         PkHud(HudState& a_h) const;
		void         PkBeforeSave();
		void         PkAfterSave();
		bool         PkHasSeat(RE::TESFormID a_id) const;

		// ------------------------------------------------------------ blackjack (Blackjack.cpp)
		static constexpr int kBjSeats = 7;
		struct BjHand
		{
			std::vector<int>                 cards;
			std::vector<std::size_t>         refs;    // their cards on the table (indexes into _bj.cards)
			int                              bet{ 0 };
			bool                             doubled{ false };
			bool                             split{ false };  // one of a split pair
			bool                             done{ false };
			std::vector<RE::ObjectRefHandle> chips;   // the bet on the circle (and the double beside it)
			std::vector<RE::ObjectRefHandle> paid;    // winnings set down beside it
			double                           result{ -1.0 };  // what it got back for its bet (bj::Return), -1 not settled
		};
		struct BjSeat
		{
			bool                used{ false };
			bool                you{ false };
			RE::ObjectRefHandle actor;
			RE::TESFormID       id{ 0 };
			std::string         name;
			Walker              walk;
			Seat::Step          step{ Seat::Step::kWalking };
			bool                held{ false };
			int                 rounds{ 0 };       // rounds left before they go
			int                 bet{ 0 };          // put down for the next hand
			int                 lastBet{ 0 };
			std::vector<BjHand> hands;             // this hand (two after a split)
			int                 insurance{ 0 };
			std::vector<RE::ObjectRefHandle> insChips;
			std::vector<RE::ObjectRefHandle> betChips;  // the bet on the circle before the deal
			std::string         last;              // what they last did
			float               atSpot{ 0.0f };
			float               nextIdle{ 8.0f };
			float               sitCheck{ 0.0f };  // (chairs) seconds before checking they're still sat down
			int                 sitTries{ 0 };
		};
		enum class BjPhase
		{
			kBetting,
			kDealing,
			kInsurance,
			kPlayers,
			kDealer,
			kSettle,
			kClear,
		};
		struct BlackjackState
		{
			RE::ObjectRefHandle             table;
			std::array<BjSeat, kBjSeats>    seats;
			bj::Shoe                        shoe;
			BjPhase                         phase{ BjPhase::kBetting };
			float                           timer{ 0.0f };
			float                           dealIn{ -1.0f };   // betting: seconds before the deal (-1 no bets yet)
			std::vector<int>                dealer;            // the dealer's cards
			std::vector<std::size_t>        dealerRefs;
			bool                            holeUp{ false };
			std::vector<PkCard>             cards;
			std::vector<PkChipMove>         moves;
			int                             turnSeat{ -1 }, turnHand{ 0 };
			float                           think{ 0.0f };
			float                           seatTimer{ 4.0f };
			std::deque<HudLine>             log;
			std::string                     bannerTitle, bannerSub;
			float                           bannerAge{ 99.0f };
			int                             bannerTone{ 0 };
			float                           seatedAge{ 99.0f };
			// the dealer: a settler behind the chip rack (or none: the table deals itself)
			RE::ObjectRefHandle             dealerActor;
			RE::TESFormID                   dealerId{ 0 };
			std::string                     dealerName;
			Walker                          dealerWalk;
			bool                            dealerHeld{ false };
			float                           dealerLook{ 0.0f };
			int                             lifted{ 0 };       // your bets back in your pocket over a save
			std::array<RE::ObjectRefHandle, kBjSeats> chairs;  // (the chairs option) one at each place
			bool                            shuffleSaid{ false };
		};
		BlackjackState    _bj;
		std::atomic<bool> _bjSeated{ false };
		int               _bjYou{ -1 };
		bool              _bjTopView{ false };
		float             _bjLookYaw{ 0.0f };
		bool              _bjShowHand{ true };    // your hand on the HUD (the SHOW / HIDE HAND button)
		int               _bjPrompt{ 0 };         // what the table's waiting on you for (the chime when it changes)
		bool              _pkTurnWas{ false };    // (poker: your turn last tick: the chime when it comes)
		bool              _bjShowDealer{ true };  // the dealer's cards on the HUD (SHOW / HIDE DEALER)
		std::chrono::steady_clock::time_point _bjAnimLast{}, _bjAnimAt{};
		bool              _bjAnimHave{ false };
		bool         BjFrame(Frame& a_out) const;
		bool         BjHasSeat(RE::TESFormID a_id) const;
		RE::NiPoint3 BjStand(const Frame& a_frame, int a_seat, float& a_heading) const;
		void         BjScan();
		void         BjStart(RE::TESObjectREFR* a_table);
		void         BjEnd(const char* a_why);
		void         TickBlackjack(float a_dt);
		void         BjFindPlayer(const Frame& a_frame);
		void         BjFindDealer(const Frame& a_frame);
		void         BjTickSeats(const Frame& a_frame, float a_dt);
		void         BjRelease(int a_seat, const char* a_why);
		void         BjTickRound(float a_dt);
		void         BjStartDeal();
		std::size_t  BjDeal(int a_card, const RE::NiPoint3& a_to, float a_yaw, bool a_faceUp, float a_delay);
		void         BjNextTurn();
		void         BjAct(bj::Move a_move);
		void         BjDealerPlay(float a_dt);
		void         BjSettle();
		void         BjClear();
		void         BjTickCards(const Frame& a_frame, float a_dt);
		void         BjTickMoves(const Frame& a_frame, float a_dt);
		void         BjPlaceHandCards(int a_seat, int a_hand);
		void         BjStack(std::vector<RE::ObjectRefHandle>& a_chips, int a_amount, const RE::NiPoint3& a_at);
		void         BjSlide(std::vector<RE::ObjectRefHandle>& a_chips, const RE::NiPoint3& a_to, bool a_remove, float a_delay);
		void         BjSay(const std::string& a_text, int a_tone = 0);
		void         BjSound(Audio::Bank a_bank, float a_gain);
		void         BjSitDown(RE::TESObjectREFR* a_table);
		void         BjStandUp();
		void         BjKey(int a_key);
		void         BjBet(int a_amount);  // your bet for the next hand: up (or 0: taken back)
		void         BjCamera(const Frame& a_frame, float a_dt);
		void         BjHud(HudState& a_h) const;
		void         BjBeforeSave();
		void         BjAfterSave();
		void         BjIdle(int a_seat, int a_kind);

		// ------------------------------------------------------------ state (main thread)
		// one table in play (up to two at once: Tick works on each in turn, with it in _t; the one you're at, or the
		// nearer, is in _t between ticks, the other in _other)
		struct Slot
		{
			RE::ObjectRefHandle               table;
			rl::WheelType                     wheel{ rl::WheelType::kEuropean };
			double                            rotorAngle{ 0.0 };
			double                            rotorSpeed{ 0.8 };
			Phase                             phase{ Phase::kBetting };
			float                             phaseTime{ 0.0f };
			float                             bettingTime{ 0.0f };
			float                             spinIn{ -1.0f };  // seconds to the wheel spinning itself (-1: no bets yet)
			rl::SpinPlan                      plan;
			std::future<rl::SpinPlan>         planFuture;
			bool                              planning{ false };  // the throw's being worked out
			float                             playT{ 0.0f };
			std::size_t                       eventIdx{ 0 };      // the next of the spin's events to sound
			float                             lastKnock{ -1.0f };
			std::size_t                       playIdx{ 0 };
			bool                              dropSaid{ false };
			int                               restPocket{ -1 };  // the ball's pocket between spins (-1: no ball yet)
			RE::ObjectRefHandle               ball;
			std::vector<Stake>                stakes;
			std::vector<std::pair<std::string, int>> lastRound;  // your bets on the last spin, for re-betting
			std::deque<int>                   history;
			std::vector<Seat>                 seats;
			float                             seatTimer{ 0.0f };
			int                               liftedCaps{ 0 };
			int                                         lastWin{ 0 };
			int                                         hudBanner{ 0 };
			std::string                                 bannerTitle;
			std::string                                 bannerSub;
			int                                         bannerNumber{ -1 };
			std::chrono::steady_clock::time_point       bannerTime{};
			std::deque<HudLine>                         log;
			float                                       roomLayout{ 1.0e6f };  // free height above the layout, and the wheel
			float                                       roomWheel{ 1.0e6f };
			Croupier                                    croupier;  // the dealer, by the wheel
		};
		Slot _t;
		Slot _other;
		bool _secondary{ false };  // _t is the other table for now (its tick): nothing of you at the table applies
		bool Here() const { return _seated && !_secondary; }  // you're at the table in _t
		void SwapTables() { std::swap(_t, _other); }

		float                             _scanTimer{ 0.0f };
		std::chrono::steady_clock::time_point _last{};
		bool                              _haveLast{ false };
		bool                              _lifted{ false };      // BeforeSave took our things off; AfterSave puts them back
		std::mt19937                      _rng{ std::random_device{}() };
		int                               _appliedWheel{ -1 };   // the setting the tables' nodes were last shown for
		std::vector<RE::TESFormID>        _appliedTables;
		std::vector<RE::ObjectRefHandle>  _nearTables;

		// you at the table
		RE::ObjectRefHandle                         _ghost;      // the chip under the cursor
		int                                         _ghostValue{ -1 };
		RE::BSTSmartPointer<RE::BSInputEnableLayer> _inputLayer;
		bool                                        _controlsBlocked{ false };
		float                                       _entryYaw{ 0.0f };
		float                                       _entryPitch{ 0.0f };
		RE::NiPoint2                                _entryFreeRotation{};
		bool                                        _haveEntryView{ false };
		float                                       _viewBackIn{ -1.0f };  // seconds until the camera goes back to the layout

		// the camera (camera thread and main thread, under _camMutex)
		mutable std::mutex _camMutex;
		bool               _camMapped{ false };
		int                _mapFrames{ 0 };
		int                _yawIndex{ 1 };
		float              _pitchSign{ 1.0f };
		RE::NiPoint3       _camWant;          // where the camera should be (the tick sets it)
		RE::NiPoint3       _camPos;           // where it is (eased towards _camWant)
		bool               _haveCamPos{ false };
		float              _camYaw{ 0.0f };   // the table's +Y as a heading: up on the screen
		std::chrono::steady_clock::time_point _lastCam{};
		double             _cursorX{ 0.0 };   // the cursor on the layout, table space
		double             _cursorY{ 0.0 };
		double             _mouseDX{ 0.0 };   // mouse counts not yet applied to the cursor
		double             _mouseDY{ 0.0 };
		const void*        _lastMouseEvent{ nullptr };
		std::uint32_t      _lastMouseTime{ 0 };

		mutable std::mutex _hudMutex;
		HudState           _hud;

		// shared with the render thread
		std::atomic<bool> _seated{ false };
		std::atomic<bool> _wheelView{ false };
		std::atomic<int>  _chip{ 0 };
		std::atomic<int>  _activateVk{ 'E' };
		std::atomic<bool> _tickPending{ false };
		std::array<bool, 16> _keyWasDown{};
		bool              _lmbWasDown{ false };
		bool              _rmbWasDown{ false };
	};
}
