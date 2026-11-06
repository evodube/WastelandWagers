#pragma once

#include "rl/Layout.h"
#include "rl/Wheel.h"

namespace fr
{
	// The roulette table in play: the one nearest you (within [Table] fActiveDistance). Its wheel turns, its dealer
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
		// a save's about to load: everything we had is gone with the old game
		void Forget();
		void OnLoadingStarts();  // a loading screen (a door, fast travel): the table's game ends, chips back
		static void Listen();    // activations and loading screens
		void OnActivated(RE::TESObjectREFR* a_ref);  // main thread: something activated by you

		// the F4SE co-save: who deals where, the settlers' purses
		static void Save(const F4SE::SerializationInterface* a_intfc);
		static void Load(const F4SE::SerializationInterface* a_intfc);
		static void Revert(const F4SE::SerializationInterface* a_intfc);

		// camera hooks (CameraHook.cpp)
		bool OnLookMouse(const RE::MouseMoveEvent* a_event);
		void BeforeFreeCameraUpdate(RE::FreeCameraState* a_free);
		void OnFreeCameraLook(RE::FreeCameraState* a_free, const RE::BSTPoint2<float>& a_before);
		void OnFreeCameraUpdate(RE::FreeCameraState* a_free);
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
			std::string          dealer;               // the dealer's name; empty: you spin
			bool                 dealerReady{ false };
			std::vector<std::string> players;          // the NPCs at the table, "PIPER  120 CAPS"
			std::vector<int>     history;              // last numbers, newest first
			std::deque<HudLine>  log;                  // the latest things that happened, oldest first
			int                  banner{ 0 };          // 0 none, 1 no more bets, 2 result (you won), 3 result (you didn't), 4 result (no bets of yours)
			std::string          bannerTitle;
			std::string          bannerSub;
			int                  bannerNumber{ -1 };
			float                bannerAge{ 0.0f };
			bool                 wheelView{ false };
			std::string          keys;                 // the key hints line
			int                  lastWin{ 0 };
		};
		HudState Hud() const;

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

		// an NPC on their way to a spot by the table (the dealer's, or a bettor's)
		struct Walker
		{
			RE::ObjectRefHandle marker;      // an XMarkerHeading on the spot: their linked ref
			bool                joinSent{ false };
			float               walked{ 0.0f };  // seconds on their way
			float               best{ 1.0e9f };  // the closest they've got
			float               stall{ 0.0f };   // seconds without getting closer
			float               getUp{ 0.0f };   // seconds before trying to get them out of a chair again
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
		};

		struct Dealer
		{
			RE::ObjectRefHandle actor;
			RE::TESFormID       id{ 0 };
			std::string         name;
			Walker              walk;
			bool                arrived{ false };
			bool                held{ false };
			bool                watching{ false };  // SetLookAt the ball
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
		void HoldPlayerView();
		void PlaceGhost(const Frame& a_frame);
		std::optional<rl::Bet> HoverBet() const;
		void UpdateHud();

		// the dealer
		void         AssignDealer();  // the settler under the crosshair deals at the nearest table (again: stops)
		void         LoadDealerFor(RE::TESObjectREFR* a_table);  // who deals at this table (the co-save's list)
		RE::Actor*   PickActor() const;
		RE::Actor*   DealerActor() const;
		bool         DealerReady() const;
		void         TickDealer(const Frame& a_frame, float a_dt);
		void         ReleaseDealer();
		RE::NiPoint3 DealerSpot(const Frame& a_frame, float& a_heading) const;

		// the NPCs betting
		void         TickSeats(const Frame& a_frame, float a_dt);
		void         FindBettor(const Frame& a_frame);
		void         ReleaseSeat(Seat& a_seat, const char* a_why);
		void         SeatBets(Seat& a_seat);
		RE::NiPoint3 SeatSpot(const Frame& a_frame, int a_spot, float& a_heading) const;
		float        HeadingOf(const Frame& a_frame, double a_dx, double a_dy) const;  // a direction in table space as a heading
		float        CeilingAbove(const Frame& a_frame, double a_x, double a_y, float a_upTo) const;
		bool         _clearNpcs{ true };  // a load: whoever the quest's collections hold, the plugin doesn't know
		static int   Purse(RE::TESFormID a_id);
		static void  AddToPurse(RE::TESFormID a_id, int a_caps);
		bool         WalkTo(RE::Actor* a_actor, Walker& a_walk, const Frame& a_frame, const RE::NiPoint3& a_spot, float a_heading, float a_dt,
					bool a_dealer, float a_timeout);  // true: there
		void         FaceTowards(RE::Actor* a_actor, float a_heading);

		// ------------------------------------------------------------ state (main thread)
		RE::ObjectRefHandle               _table;
		rl::WheelType                     _wheel{ rl::WheelType::kEuropean };
		double                            _rotorAngle{ 0.0 };
		double                            _rotorSpeed{ 0.8 };
		Phase                             _phase{ Phase::kBetting };
		float                             _phaseTime{ 0.0f };
		float                             _bettingTime{ 0.0f };
		rl::SpinPlan                      _plan;
		float                             _playT{ 0.0f };
		std::size_t                       _playIdx{ 0 };
		bool                              _hopping{ false };
		bool                              _settleHeard{ false };
		bool                              _dropSaid{ false };
		int                               _restPocket{ -1 };  // the ball's pocket between spins (-1: no ball yet)
		RE::ObjectRefHandle               _ball;
		std::vector<Stake>                _stakes;
		std::vector<std::pair<std::string, int>> _lastRound;  // your bets on the last spin, for re-betting
		std::deque<int>                   _history;
		Dealer                            _dealer;
		std::vector<Seat>                 _seats;
		float                             _seatTimer{ 0.0f };
		float                             _scanTimer{ 0.0f };
		std::chrono::steady_clock::time_point _last{};
		bool                              _haveLast{ false };
		bool                              _lifted{ false };      // BeforeSave took our things off; AfterSave puts them back
		int                               _liftedCaps{ 0 };
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
		int                                         _lastWin{ 0 };
		int                                         _hudBanner{ 0 };
		std::string                                 _bannerTitle;
		std::string                                 _bannerSub;
		int                                         _bannerNumber{ -1 };
		std::chrono::steady_clock::time_point       _bannerTime{};
		std::deque<HudLine>                         _log;
		float                                       _viewBackIn{ -1.0f };  // seconds until the camera goes back to the layout
		float                                       _roomLayout{ 1.0e6f };  // free height above the layout, and the wheel
		float                                       _roomWheel{ 1.0e6f };

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
