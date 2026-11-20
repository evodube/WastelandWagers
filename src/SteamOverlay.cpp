#include "SteamOverlay.h"

#include "MenuWatch.h"
#include "Settings.h"

namespace fr::overlay
{
	namespace
	{
		// Steamworks' callback base, as steam_api64.dll has it (MSVC lays out the two Run overloads the same way the
		// SDK's own build did, from the same declaration)
		class CCallbackBase
		{
		public:
			virtual void Run(void* a_param) = 0;
			virtual void Run(void* a_param, bool a_ioFailure, std::uint64_t a_call) = 0;
			virtual int  GetCallbackSizeBytes() = 0;

			std::uint8_t flags{ 0 };
			int          callback{ 0 };
		};

		constexpr int kGameOverlayActivated = 331;  // k_iSteamFriendsCallbacks (300) + 31
		struct GameOverlayActivated
		{
			std::uint8_t active;
		};

		std::atomic<bool> g_open{ false };
		std::atomic<bool> g_weOpenedPause{ false };

		void Show(bool a_show)
		{
			if (auto* q = RE::UIMessageQueue::GetSingleton()) {
				q->AddMessage("PauseMenu", a_show ? RE::UI_MESSAGE_TYPE::kShow : RE::UI_MESSAGE_TYPE::kHide);
			}
		}

		class Listener : public CCallbackBase
		{
		public:
			void Run(void* a_param) override
			{
				const bool open = a_param && static_cast<GameOverlayActivated*>(a_param)->active != 0;
				g_open = open;
				log::info("the Steam overlay {}", open ? "opened" : "closed");
				if (!Settings::Get().pauseOnOverlay) {
					return;
				}
				if (open) {
					// (nothing over the game already: the pause menu up, the game stops under the overlay)
					if (!menus::AnyBlocking() && !menus::Open(menus::kLoading)) {
						g_weOpenedPause = true;
						Show(true);
					}
				} else if (g_weOpenedPause.exchange(false) && menus::Open(menus::kPause)) {
					Show(false);
				}
			}
			void Run(void* a_param, bool, std::uint64_t) override { Run(a_param); }
			int  GetCallbackSizeBytes() override { return sizeof(GameOverlayActivated); }
		};

		Listener g_listener;
	}

	void Listen()
	{
		static bool done = false;
		if (done) {
			return;
		}
		done = true;
		const HMODULE steam = ::GetModuleHandleW(L"steam_api64.dll");
		using register_t = void (*)(CCallbackBase*, int);
		auto* reg = steam ? reinterpret_cast<register_t>(::GetProcAddress(steam, "SteamAPI_RegisterCallback")) : nullptr;
		if (!reg) {
			log::info("Steam overlay: no Steam API here (not the Steam game?): not listening");
			return;
		}
		g_listener.callback = kGameOverlayActivated;
		reg(&g_listener, kGameOverlayActivated);
		log::info("Steam overlay: listening");
	}

	bool Open()
	{
		return g_open;
	}
}
