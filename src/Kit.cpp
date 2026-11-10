// The Roulette Table Kit (fo4roulette.esl, an Aid item): sold by Vadim at the Dugout Inn, and made at a chem station.
// Used from the Pip-Boy it unfolds into a roulette table in front of you, anywhere; sitting at a table set up that way,
// P packs it back into a kit. Which tables are kit tables, and whether the Dugout Inn's been stocked, go in the co-save.
// (Playable Pool's Pool Table Kit, for a roulette table.)

#include "Game.h"

#include "Forms.h"
#include "Settings.h"
#include "Util.h"

namespace fr
{
	namespace
	{
		constexpr RE::TESFormID kDugoutVendorBox = 0x001338B6;    // Fallout4.esm VendorDCDugoutInn (the container)
		constexpr RE::TESFormID kDugoutVendorChest = 0x001338B7;  // ...and its reference (ServicesDmndDugoutInn's VENC)
		constexpr float         kPlaceNear = 140.0f;  // the table's middle: where you look on the ground, this far...
		constexpr float         kPlaceFar = 450.0f;   // ...to this far from you (as Playable Pool's kit)
		constexpr float         kTurnStep = 0.2617994f;  // an arrow press turns it 15 degrees
		constexpr float         kTooClose = 300.0f;   // not on top of another roulette table

		std::string Upper(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return a_s;
		}

		std::mutex                        g_kitMutex;
		std::unordered_set<RE::TESFormID> g_kitTables;          // tables set up from a kit (created refs: FF......)
		std::uint8_t                      g_kitStocked{ 0 };    // the Dugout Inn got its first kits in this save: 1 roulette, 2 poker
		std::atomic<RE::TESFormID>        g_previewID{ 0 };       // the table following the crosshair while you set one up

		// the kits: 0 the Roulette Table Kit, 1 the Poker Table Kit
		RE::TESBoundObject* KitForm(int a_kind = 0)
		{
			return util::ResolveForm(Settings::Get().plugin, a_kind == 1 ? Forms::kPokerKitID : Forms::kKitID);
		}

		RE::TESBoundObject* KitTable(int a_kind)
		{
			return a_kind == 1 ? Forms::PokerTable() : Forms::Table();
		}

		void GiveKit(int a_kind)
		{
			if (auto* player = RE::PlayerCharacter::GetSingleton(); player && KitForm(a_kind)) {
				player->AddObjectToContainer(KitForm(a_kind), nullptr, 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);
			}
		}

		struct EquipSink final : RE::BSTEventSink<RE::TESEquipEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent& a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
			{
				for (int kind = 0; kind < 2; ++kind) {
					auto* kit = KitForm(kind);
					if (a_event.equipped && kit && a_event.baseObject == kit->GetFormID() && a_event.actor &&
						a_event.actor.get() == RE::PlayerCharacter::GetSingleton()) {
						// used from the Pip-Boy (it's gone from your inventory now): set the table up once you're back
						// in the world
						if (auto* tasks = F4SE::GetTaskInterface()) {
							tasks->AddTask([kind]() { Game::Get().BeginKitPlacement(kind); });
						}
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Game::SetUpKit()
	{
		auto* kit = KitForm();
		if (!kit) {
			log::warn("kit: fo4roulette.esl's Roulette Table Kit not found");
			return;
		}
		// the Dugout Inn's stock: one of each kit each time Vadim restocks (in memory, every launch: nothing in the esl
		// touches the vendor)
		if (auto* box = RE::TESForm::GetFormByID<RE::TESObjectCONT>(kDugoutVendorBox)) {
			box->AddObject(kit, 1, nullptr);
			if (auto* poker = KitForm(1)) {
				box->AddObject(poker, 1, nullptr);
			}
			log::info("kit: added to the Dugout Inn's stock ({:08X})", kDugoutVendorBox);
		} else {
			log::warn("kit: the Dugout Inn's vendor box {:08X} not found", kDugoutVendorBox);
		}
		static EquipSink sink;
		if (auto* source = RE::TESEquipEvent::GetEventSource()) {
			source->RegisterSink(&sink);
		}
	}

	void Game::StockKit()
	{
		// a save from before a kit existed: one in Vadim's stock now, not just after his next restock
		auto* chest = RE::TESForm::GetFormByID<RE::TESObjectREFR>(kDugoutVendorChest);
		std::scoped_lock lock(g_kitMutex);
		for (int kind = 0; kind < 2; ++kind) {
			auto*              kit = KitForm(kind);
			const std::uint8_t bit = static_cast<std::uint8_t>(1u << kind);
			if ((g_kitStocked & bit) || !kit || !chest) {
				continue;
			}
			g_kitStocked |= bit;
			if (chest->GetInventoryObjectCount(kit) == 0) {
				chest->AddObjectToContainer(kit, nullptr, 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);
				log::info("kit: a {} kit in the Dugout Inn's stock", kind == 1 ? "poker" : "roulette");
			}
		}
	}

	bool Game::IsKitTable(const RE::TESObjectREFR* a_table)
	{
		std::scoped_lock lock(g_kitMutex);
		return a_table && g_kitTables.contains(a_table->GetFormID());
	}

	bool Game::IsKitPreview(const RE::TESObjectREFR* a_ref)
	{
		const auto id = g_previewID.load();
		return a_ref && id != 0 && a_ref->GetFormID() == id;
	}

	void Game::BeginKitPlacement(int a_kind)
	{
		if (_kitPlacing || _seated || _pkSeated) {
			GiveKit(a_kind);  // (a second kit used while one's being set up, or from a table: that one back)
			return;
		}
		_kitKind = a_kind;
		_kitPlacing = true;
		_kitConfirmArmed = false;
		_kitTurn = 0.0f;
		_kitReady = false;
		_kitSign = 0;
		_kitOk = false;
		// out of the Pip-Boy and into the world: the table follows where you look until the activate key sets it down
		if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
			queue->AddMessage(RE::BSFixedString("PipboyMenu"), RE::UI_MESSAGE_TYPE::kHide);
		}
		log::info("kit: setting a table up");
	}

	std::string Game::KitPrompt() const
	{
		const std::string key = Upper(Settings::Key(_activateVk.load()));
		const std::string cancel = Upper(Settings::Key(Settings::Get().keyLeave));
		return _kitOk ? std::format("{}) SET UP THE TABLE  ·  LEFT/RIGHT TURN  ·  {}) CANCEL", key, cancel)
		              : std::format("TOO CLOSE TO ANOTHER TABLE  ·  {}) CANCEL", cancel);
	}

	void Game::TickKitPlacement()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* camera = RE::PlayerCamera::GetSingleton();
		auto* base = KitTable(_kitKind);
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!player || !camera || !camera->cameraRoot || !base || !cell) {
			return;
		}
		// where the crosshair meets the ground, between kPlaceNear and kPlaceFar away; then the ground right under that
		// spot (a slope, a step, a floor higher or lower than yours): the table stands on it
		const auto         origin = camera->cameraRoot->GetWorldTranslate();
		const float        pitch = player->data.angle.x;
		const float        yaw = player->data.angle.z;
		const RE::NiPoint3 dir{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
		const RE::NiPoint3 flat{ std::sin(yaw), std::cos(yaw), 0.0f };
		auto ray = [&](const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, RE::NiPoint3& a_hit) {
			RE::bhkPickData pick;
			pick.SetStartEnd(a_from, a_to);
			cell->Pick(pick);
			if (!pick.HasHit()) {
				return false;
			}
			a_hit = a_from + (a_to - a_from) * std::clamp(pick.GetHitFraction(), 0.0f, 1.0f);
			return true;
		};
		float reach = kPlaceFar;
		// (from past you: in third person the camera's behind you, and the ray would stop at your back)
		const float past = std::max(0.0f, (player->data.location - origin).Dot(dir)) + 40.0f;
		const auto  from = origin + dir * past;
		if (RE::NiPoint3 hit; ray(from, from + dir * (kPlaceFar * 2.0f), hit)) {
			reach = std::clamp(std::hypot(hit.x - player->data.location.x, hit.y - player->data.location.y), kPlaceNear, kPlaceFar);
		} else if (dir.z < -0.02f) {
			const float t = (origin.z - player->data.location.z) / -dir.z;
			reach = std::clamp(t * std::cos(pitch), kPlaceNear, kPlaceFar);
		}
		RE::NiPoint3 pos = player->data.location + flat * reach;
		pos.z = player->data.location.z;
		{
			const float  top = std::max(origin.z, player->data.location.z + 150.0f);
			RE::NiPoint3 ground;
			if (ray({ pos.x, pos.y, top }, { pos.x, pos.y, player->data.location.z - 400.0f }, ground)) {
				pos.z = ground.z;
			}
		}
		const float angle = yaw + 1.5707963f + _kitTurn;  // its long side towards you, turned as you like
		_kitPos = pos;
		_kitAngle = angle;

		auto preview = _kitPreview.get();
		if (!preview) {
			RE::NEW_REFR_DATA data;
			data.location = pos;
			data.direction = { 0.0f, 0.0f, angle };
			data.object = base;
			if (cell->IsInterior()) {
				data.interior = cell;
			} else {
				data.world = cell->worldSpace;
			}
			data.initializeScripts = false;
			auto* dh = RE::TESDataHandler::GetSingleton();
			_kitPreview = dh ? dh->CreateReferenceAtLocation(data) : RE::ObjectRefHandle{};
			if (auto made = _kitPreview.get()) {
				g_previewID = made->GetFormID();
				_kitMadeAngle = angle;
			}
			return;
		}
		auto* root = preview->Get3D();
		if (!root) {
			return;  // (its 3D on the way)
		}
		if (!_kitReady) {
			// nothing bumps into it while it follows you about; and which way round the 3D takes an angle (so the
			// preview turns exactly as the table set down at that angle will stand)
			RE::bhkWorld::EnableCollision(root, false, true, true);
			// the table's collision made keyframed, as Playable Pool does: left as it is, the physics holds the
			// table's 3D where it was made (the table's BSX flags have it articulated), so it never followed you
			int bodies = 0;
			RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNPCollisionObject* a_collision) {
				a_collision->SetMotionType(RE::hknpMotionPropertiesId::Preset::kKeyframed);
				++bodies;
				return RE::BSVisitControl::kContinue;
			});
			log::info("kit: preview {:08X} ready ({} collision object(s) keyframed)", preview->GetFormID(), bodies);
			RE::NiMatrix3 plus, minus;
			plus.FromEulerAnglesXYZ(0.0f, 0.0f, _kitMadeAngle);
			minus.FromEulerAnglesXYZ(0.0f, 0.0f, -_kitMadeAngle);
			const auto& w = root->world.rotate;
			auto diff = [&](const RE::NiMatrix3& a_m) {
				float d = 0.0f;
				for (int r = 0; r < 3; ++r) {
					for (int c = 0; c < 3; ++c) {
						d += std::abs(a_m.entry[r][c] - w.entry[r][c]);
					}
				}
				return d;
			};
			_kitSign = diff(plus) <= diff(minus) ? 1 : -1;
			_kitReady = true;
		}
		RE::NiMatrix3 rot;
		rot.FromEulerAnglesXYZ(0.0f, 0.0f, static_cast<float>(_kitSign) * angle);
		PlaceRoot(preview.get(), pos, rot);

		// clear of the other roulette tables
		bool crowded = false;
		util::ForEachLoadedCell([&](RE::TESObjectCELL* a_cell) {
			a_cell->ForEachReferenceInRange(pos, kTooClose, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && a_ref != preview.get() && !a_ref->IsDisabled() && (Forms::IsTable(a_ref) || Forms::IsPokerTable(a_ref))) {
					crowded = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		});
		_kitOk = !crowded;
	}

	void Game::PlaceRoot(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot)
	{
		auto* root = a_ref ? a_ref->Get3D() : nullptr;
		if (!root) {
			return;
		}
		// we know the world transform we want; the node stores it relative to its parent
		RE::NiTransform local;
		if (auto* parent = root->parent) {
			const auto& p = parent->GetWorldTransform();
			const float ps = p.scale != 0.0f ? p.scale : 1.0f;
			local.rotate = a_rot * p.rotate.Transpose();
			local.translate = (p.rotate * (a_pos - p.translate)) * (1.0f / ps);
			local.scale = 1.0f / ps;
		} else {
			local.rotate = a_rot;
			local.translate = a_pos;
			local.scale = 1.0f;
		}
		root->SetLocalTransform(local);
		RE::NiUpdateData update;
		root->Update(update);
		a_ref->data.location.x = a_pos.x;
		a_ref->data.location.y = a_pos.y;
		a_ref->data.location.z = a_pos.z;
	}

	void Game::TurnKit(int a_dir)
	{
		if (_kitPlacing) {
			_kitTurn += static_cast<float>(a_dir) * kTurnStep;
		}
	}

	void Game::CancelKit(bool a_giveBack)
	{
		if (!_kitPlacing) {
			return;
		}
		_kitPlacing = false;
		g_previewID = 0;
		Remove(_kitPreview);
		if (a_giveBack) {
			GiveKit(_kitKind);
			Notify("Put the kit away");
		}
		log::info("kit: setting up cancelled");
	}

	void Game::ConfirmKit()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* base = KitTable(_kitKind);
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!_kitPlacing || !_kitPreview.get() || !player || !base || !cell) {
			return;
		}
		if (!_kitOk) {
			Notify("Too close to another table");
			return;
		}
		const RE::NiPoint3 pos = _kitPos;
		const float        angle = _kitAngle;
		_kitPlacing = false;
		g_previewID = 0;
		Remove(_kitPreview);  // the preview goes; the real one in its place
		RE::NEW_REFR_DATA data;
		data.location = pos;
		data.direction = { 0.0f, 0.0f, angle };
		data.object = base;
		if (cell->IsInterior()) {
			data.interior = cell;
		} else {
			data.world = cell->worldSpace;
		}
		data.forcePersist = true;  // stays when the cell resets, until you pack it up
		data.initializeScripts = false;
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto  table = dh ? dh->CreateReferenceAtLocation(data).get() : nullptr;
		if (!table) {
			GiveKit(_kitKind);
			Notify("The table couldn't be set up. The kit's back in your inventory");
			return;
		}
		{
			std::scoped_lock lock(g_kitMutex);
			g_kitTables.insert(table->GetFormID());
		}
		_scanTimer = 0.0f;  // in play straight away
		log::info("kit: table {:08X} set up at ({:.0f}, {:.0f}, {:.0f})", table->GetFormID(), pos.x, pos.y, pos.z);
		Notify(std::format("{}: table set up. Sit at it and press {} to pack it away again", _kitKind == 1 ? "Poker" : "Roulette", Upper(Settings::Key(Settings::Get().keyPackUp))));
	}

	void Game::PackUpTable()
	{
		// the table you're sitting at, if it came from a kit, and once the ball's stopped
		auto table = _t.table.get();
		if (!table || !IsKitTable(table.get())) {
			return;
		}
		if (_t.phase == Phase::kSpinning) {
			Say("WAIT FOR THE BALL TO STOP", 2);
			return;
		}
		const RE::TESFormID id = table->GetFormID();
		StandUp();
		EndSession("packed up");  // every bet back to whoever made it, the players off home
		{
			std::scoped_lock lock(g_kitMutex);
			g_kitTables.erase(id);
		}
		log::info("kit: table {:08X} packed up", id);
		table->Disable();
		table->SetWantsDelete(true);
		GiveKit(0);
		Notify("Roulette: table packed up. The kit's in your inventory");
	}

	void Game::PackUpPokerTable(RE::TESObjectREFR* a_table)
	{
		if (!a_table || !IsKitTable(a_table)) {
			return;
		}
		const RE::TESFormID id = a_table->GetFormID();
		PkEnd("packed up");
		{
			std::scoped_lock lock(g_kitMutex);
			g_kitTables.erase(id);
		}
		log::info("kit: poker table {:08X} packed up", id);
		a_table->Disable();
		a_table->SetWantsDelete(true);
		GiveKit(1);
		Notify("Poker: table packed up. The kit's in your inventory");
	}

	void Game::SaveKit(const F4SE::SerializationInterface* a_intfc)
	{
		std::scoped_lock lock(g_kitMutex);
		if (!a_intfc->OpenRecord(kKitRecordType, kKitRecordVersion)) {
			return;
		}
		const std::uint8_t stocked = g_kitStocked;
		a_intfc->WriteRecordData(&stocked, sizeof(stocked));
		const auto count = static_cast<std::uint32_t>(g_kitTables.size());
		a_intfc->WriteRecordData(&count, sizeof(count));
		for (const auto id : g_kitTables) {
			a_intfc->WriteRecordData(&id, sizeof(id));
		}
	}

	void Game::LoadKit(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version)
	{
		if (a_version != kKitRecordVersion) {
			return;
		}
		std::scoped_lock lock(g_kitMutex);
		std::uint8_t  stocked = 0;
		std::uint32_t count = 0;
		a_intfc->ReadRecordData(&stocked, sizeof(stocked));
		a_intfc->ReadRecordData(&count, sizeof(count));
		g_kitStocked = stocked;
		for (std::uint32_t i = 0; i < count; ++i) {
			RE::TESFormID id = 0;
			a_intfc->ReadRecordData(&id, sizeof(id));
			if (const auto now = a_intfc->ResolveFormID(id)) {
				g_kitTables.insert(*now);
			}
		}
		log::info("kit: {} table(s) set up from kits in this save", g_kitTables.size());
	}

	void Game::RevertKit()
	{
		std::scoped_lock lock(g_kitMutex);
		g_kitTables.clear();
		g_kitStocked = 0;
	}
}
