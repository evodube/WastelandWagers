#!/usr/bin/env python3
"""Writes WastelandWagers.esl: the roulette table and its pieces, and the quest that walks NPCs to it.

    python tools/make_esl.py [out.esl]

Form IDs (src/Forms.h expects these):
    xx000800  ACTI WastelandWagersTable        Roulette Table: Meshes\\WastelandWagers\\RouletteTable.nif (activate: Play;
                                           workshop menu preview framed as the vanilla pool table's)
    xx000801  STAT WastelandWagersBall         Meshes\\WastelandWagers\\Ball.nif
    xx000802  STAT WastelandWagersChip1        Meshes\\WastelandWagers\\Chip1.nif   (and 5, 25, 100 at 803-805)
    xx000806  KYWD WastelandWagersLinkSpot     an NPC's linked ref: the spot by the table they're to walk to
    xx000807  PACK WastelandWagersWalkToSpot   Travel to that linked ref, walking, facing the marker at the end
    xx000808  QUST WastelandWagersQuest        priority 100, start game enabled; collection aliases 0 Dealers and 1
                                           Bettors, both carrying the package (alias packages go by quest priority,
                                           so it outranks settler sandboxes)
    xx000809  COBJ workshop_co_WastelandWagersTable  the table in the workshop menu (Furniture > Tables, as the vanilla
                                           tables): wood 12, steel 4, cloth 4, screws 2
    xx00080A  ALCH WastelandWagersTableKit     Roulette Table Kit: an Aid item; used, the plugin sets a table up in front of
                                           you (sold by Vadim at the Dugout Inn: the plugin adds it to his stock)
    xx00080B  COBJ WastelandWagersco_TableKit  the kit at a chem station (Utility): wood 12, steel 6, cloth 4, screws 2,
                                           adhesive 2
    xx000810+ STAT WastelandWagersCard_<rank><suit>  the 52 playing cards (0x810 + suit * 13 + rank; suits c d h s)
    xx000844  ACTI WastelandWagersPokerTable  Poker Table (Meshes/WastelandWagers/PokerTable.nif)
    xx000845  COBJ workshop_co_WastelandWagersPokerTable  in the workshop menu, Furniture > Tables
    xx000846  ALCH WastelandWagersPokerKit     Poker Table Kit (sold by Vadim too)
    xx000847  COBJ WastelandWagersco_PokerKit  the poker kit at a chem station (Utility)
    xx000848  KYWD WastelandWagersLinkSeat     a poker player's linked ref: their chair
    xx000849  PACK WastelandWagersSitInSeat    Sit in that linked ref (as vanilla DefaultSitLinkCustom01NoEating), carried by
                                           the quest's third collection, 2 Sitters
    xx00084A  ACTI WastelandWagersBlackjackTable  Blackjack Table (Meshes/WastelandWagers/BlackjackTable.nif)
    xx00084B  COBJ workshop_co_WastelandWagersBlackjackTable  in the workshop menu, Furniture > Tables
    xx00084C  ALCH WastelandWagersBlackjackKit  Blackjack Table Kit (sold by Vadim too)
    xx00084D  COBJ WastelandWagersco_BlackjackKit  the blackjack kit at a chem station (Utility)
    xx00084E  FURN WastelandWagersSlotMachine  Slot Machine (Meshes/WastelandWagers/SlotMachine.nif: the Vault-Tec Workshop's
                                           machine re-skinned; its sitting animations: DLCworkshop03.esm is a master)
    xx00084F  COBJ workshop_co_WastelandWagersSlotMachine  in the workshop menu
    xx000850  KYWD WastelandWagersWorkshopMenu  the workshop menu's Wasteland Wagers category (in Furniture):
                                           every table's and the slot machine's recipe is in it
    xx000851  ARTO WastelandWagersWorkshopMenuArt  the category's picture: its first item (the roulette table)
    xx000853  IDLE WastelandWagersSlotHoldIdle  you sat at a slot machine, still: its sitting loop on its own (looping),
                                           so the furniture's idle doesn't pull the lever for you now and then
    xx000854  ACTI WastelandWagersTableUsed    Used Roulette Table (Meshes/WastelandWagers/RouletteTableUsed.nif: the same table,
                                           post-war; tools/make_used.py), 855 its COBJ in the workshop menu
    xx000856  ACTI WastelandWagersPokerTableUsed  Used Poker Table, 857 its COBJ
    xx000858  ACTI WastelandWagersBlackjackTableUsed  Used Blackjack Table, 859 its COBJ
    xx00085A  FURN WastelandWagersSlotMachineUsed  Used Slot Machine (SlotMachineUsed.nif), 85B its COBJ
    xx000852  IDLE WastelandWagersSlotPullIdle  sat at a slot machine: the hand to the lever and a pull (the DLC's own
                                           pull, PoseA_IdleFlavor1: its furnitureIdleFlavor1Slave is the lever going down)

Record layouts follow Playable Pool's tools/make_esl.py (laid out as Fallout4.esm's own records).
"""

import struct
import sys

RECORD_VERSION = 131  # Fallout 4
FLAG_MASTER = 0x1
FLAG_LIGHT = 0x200
FIRST_ID = 0x800


def subrecord(sig: bytes, data: bytes) -> bytes:
    if len(data) > 0xFFFF:
        raise ValueError(f"{sig!r} too long")
    return sig + struct.pack("<H", len(data)) + data


def zstring(text: str) -> bytes:
    return text.encode("cp1252") + b"\0"


def record(sig: bytes, flags: int, form_id: int, body: bytes) -> bytes:
    # type, data size, flags, form id, version control info, form version, unknown
    return sig + struct.pack("<IIIIHH", len(body), flags, form_id, 0, RECORD_VERSION, 0) + body


def group(label: bytes, contents: bytes) -> bytes:
    # type, group size (header included), label, group type 0 (top), timestamp, vc info, unknown
    return b"GRUP" + struct.pack("<I", 24 + len(contents)) + label + struct.pack("<iHHI", 0, 0, 0, 0) + contents


def stat(form_id: int, editor_id: str, mesh: str, bound: int = 4) -> bytes:
    return record(
        b"STAT",
        0,
        form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"OBND", struct.pack("<6h", -bound, -bound, -bound, bound, bound, bound))
        + subrecord(b"MODL", zstring(mesh))
        # max angle, material (none), leaf amplitude, leaf frequency
        + subrecord(b"DNAM", struct.pack("<fIff", 90.0, 0, 1.0, 1.0)),
    )




def recipe(form_id: int, editor_id: str, created: int, components: list[tuple[int, int]], workbench: int, category: int) -> bytes:
    """A crafting recipe (COBJ), laid out as Fallout4.esm's chem station ones (co_food_CuttingFluid)."""
    return record(
        b"COBJ",
        0,
        form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"FVPA", b"".join(struct.pack("<II", c, n) for c, n in components))
        + subrecord(b"CNAM", struct.pack("<I", created))
        + subrecord(b"BNAM", struct.pack("<I", workbench))
        + subrecord(b"FNAM", struct.pack("<I", category))
        + subrecord(b"INTV", struct.pack("<HH", 1, 0)),  # makes one
    )


def potion(form_id: int, editor_id: str, name: str, desc: str, mesh: str, bounds: bytes, value: int, weight: float) -> bytes:
    """An Aid item with no effects: using it (from the Pip-Boy) is what the plugin listens for (TESEquipEvent)."""
    return record(
        b"ALCH",
        0,
        form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"OBND", bounds)
        + subrecord(b"FULL", zstring(name))
        + (subrecord(b"MODL", zstring(mesh)) if mesh else b"")
        + subrecord(b"DESC", zstring(desc))
        + subrecord(b"DATA", struct.pack("<f", weight))
        # value, flags (1: no auto-calc, the value as given), addiction (none), addiction chance, consume sound (none)
        + subrecord(b"ENIT", struct.pack("<iIIfI", value, 1, 0, 0.0, 0)),
    )


def keyword(form_id: int, editor_id: str) -> bytes:
    # colour (white), type 0 (none): as vanilla link keywords (RELinkTravel)
    return record(
        b"KYWD", 0, form_id,
        subrecord(b"EDID", zstring(editor_id)) + subrecord(b"CNAM", bytes.fromhex("ffffff00")) + subrecord(b"TNAM", struct.pack("<I", 0)),
    )


def menu_keyword(form_id: int, editor_id: str, name: str) -> bytes:
    """a workshop menu category (a recipe filter, as Fallout4.esm's WorkshopRecipeFilterFurniture02Tables): its name is
    what the menu shows. The plugin puts it in the workshop menu's Furniture menu (after Tables) when the game's data is loaded (Forms.h
    AddWorkshopMenu: not in the menu's form list here, so nothing else's menu changes are overwritten)"""
    return record(
        b"KYWD", 0, form_id,
        subrecord(b"EDID", zstring(editor_id)) + subrecord(b"CNAM", bytes.fromhex("ffffff00")) + subrecord(b"TNAM", struct.pack("<I", 9))
        + subrecord(b"FULL", zstring(name)),
    )


def menu_art(form_id: int, editor_id: str, keyword_id: int, mesh: str, preview: int, bounds: tuple) -> bytes:
    """the picture on a workshop menu tab: an art object with the tab's keyword (the menu finds a tab's art by its
    keyword). Ours is the tab's first item, the roulette table, framed as its own workshop preview is"""
    return record(
        b"ARTO", 0, form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"OBND", struct.pack("<6h", *bounds))
        + subrecord(b"PTRN", struct.pack("<I", preview))
        + subrecord(b"KSIZ", struct.pack("<I", 1))
        + subrecord(b"KWDA", struct.pack("<I", keyword_id))
        + subrecord(b"MODL", zstring(mesh))
        + subrecord(b"DNAM", struct.pack("<I", 0)),
    )


TRAVEL_TEMPLATE = 0x00002CB0  # Fallout4.esm "Travel"


def travel_package(form_id: int, editor_id: str, link_keyword: int, radius: int = 10) -> bytes:
    """A Travel package to the actor's linked ref (with link_keyword), like vanilla
    VRWorkshopShared_WorkshopSummonedByBellPackage: same template, data inputs and event blocks."""
    no_event = subrecord(b"INAM", struct.pack("<I", 0)) + subrecord(b"PDTO", bytes(8))
    body = (
        subrecord(b"EDID", zstring(editor_id))
        # general flags (0x2000: use the preferred speed), type 18 (package), interrupt override 0, preferred speed
        # 0 (walk), unknown 0x4D, interrupt flags 0xFD, unknown: as the vanilla bell package, but walking
        + subrecord(b"PKDT", struct.pack("<IBBBBHH", 0x00002000, 18, 0, 0, 0x4D, 0x00FD, 0))
        # schedule: any time
        + subrecord(b"PSDT", bytes.fromhex("00ff00ffff000000") + struct.pack("<i", 0))
        # data inputs: 4, from the Travel template, version 1
        + subrecord(b"PKCU", struct.pack("<III", 4, TRAVEL_TEMPLATE, 1))
        # Place to Travel: near linked reference (type 6) with the keyword, radius
        + subrecord(b"ANAM", zstring("Location")) + subrecord(b"PLDT", struct.pack("<IIiI", 6, link_keyword, radius, 0))
        + subrecord(b"ANAM", zstring("Bool")) + subrecord(b"CNAM", b"\x00")  # Prefer Preferred Path?
        + subrecord(b"ANAM", zstring("Bool")) + subrecord(b"CNAM", b"\x00")  # Use "soft radius"?
        + subrecord(b"ANAM", zstring("Bool")) + subrecord(b"CNAM", b"\x00")  # Ignore marker orientation? (no: face it)
        + subrecord(b"UNAM", b"\x01") + subrecord(b"UNAM", b"\x03") + subrecord(b"UNAM", b"\x05") + subrecord(b"UNAM", b"\x07")
        + subrecord(b"XNAM", b"\x08")
        + subrecord(b"POBA", b"") + no_event
        + subrecord(b"POEA", b"") + no_event
        + subrecord(b"POCA", b"") + no_event
    )
    return record(b"PACK", 0, form_id, body)


SIT_TEMPLATE = 0x00003896  # Fallout4.esm "Sit"


def sit_package(form_id: int, editor_id: str, link_keyword: int) -> bytes:
    """Sit in the actor's linked ref (with link_keyword): laid out as vanilla DefaultSitLinkCustom01NoEating (the Sit
    template: the furniture, a search radius, two bools, the target selector, and no eating)."""
    no_event = subrecord(b"INAM", struct.pack("<I", 0)) + subrecord(b"PDTO", bytes(8))
    body = (
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"PKDT", bytes.fromhex("0000000012000200ffff0000"))
        + subrecord(b"PSDT", bytes.fromhex("00ff00ffff00000000000000"))
        + subrecord(b"PKCU", struct.pack("<III", 6, SIT_TEMPLATE, 3))
        + subrecord(b"ANAM", zstring("SingleRef")) + subrecord(b"PTDA", struct.pack("<IiI", 3, link_keyword, 0))
        + subrecord(b"ANAM", zstring("Float")) + subrecord(b"CNAM", struct.pack("<f", 0.0))
        + subrecord(b"ANAM", zstring("Bool")) + subrecord(b"CNAM", b"\x00")
        + subrecord(b"ANAM", zstring("Bool")) + subrecord(b"CNAM", b"\x00")
        + subrecord(b"ANAM", zstring("TargetSelector")) + subrecord(b"PTDA", struct.pack("<IiI", 2, 0x0F, 0))
        + subrecord(b"ANAM", zstring("Bool")) + subrecord(b"CNAM", b"\x01")
        + b"".join(subrecord(b"UNAM", bytes([u])) for u in (0, 1, 2, 4, 6, 8))
        + subrecord(b"XNAM", b"\x09")
        + subrecord(b"POBA", b"") + no_event
        + subrecord(b"POEA", b"") + no_event
        + subrecord(b"POCA", b"") + no_event
    )
    return record(b"PACK", 0, form_id, body)


def wstring(text: str) -> bytes:
    data = text.encode("cp1252")
    return struct.pack("<H", len(data)) + data


def script_entry(name: str, properties: list[bytes]) -> bytes:
    # name, flags 0 (local), property count, properties
    return wstring(name) + b"\x00" + struct.pack("<H", len(properties)) + b"".join(properties)


def object_property(name: str, form_id: int, alias: int = -1) -> bytes:
    # type 1 (object), flags 1 (edited), object v2: unused, alias id (-1 none), form id
    return wstring(name) + bytes([1, 1]) + struct.pack("<HhI", 0, alias, form_id)


def quest_vmad(quest_id: int, quest_script: str, alias_properties: list[tuple[str, int]], alias_scripts: list[tuple[int, str]]) -> bytes:
    """Scripts on the quest and on its aliases, laid out as Fallout4.esm's DialogueGlowingSeaAtom: version 6, object
    format 2; the quest script (its properties pointing at its aliases); the fragments block with no fragments (03,
    count 0, an empty fragment script name); then the aliases and their scripts."""
    aliases = b"".join(
        struct.pack("<HhI", 0, alias_id, quest_id) + struct.pack("<HHH", 6, 2, 1) + script_entry(script, [])
        for alias_id, script in alias_scripts
    )
    return (
        struct.pack("<HHH", 6, 2, 1)
        + script_entry(quest_script, [object_property(name, quest_id, alias_id) for name, alias_id in alias_properties])
        + b"\x03" + struct.pack("<H", 0) + wstring("")
        + struct.pack("<H", len(alias_scripts))
        + aliases
    )




def activator(form_id: int, editor_id: str, name: str, mesh: str, bounds: tuple, verb: str, preview: int) -> bytes:
    """An activator with no script: activating it is what the plugin listens for (TESActivateEvent). preview: the
    TRNS that frames it in the workshop menu (PTRN)."""
    return record(
        b"ACTI",
        0,
        form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"OBND", struct.pack("<6h", *bounds))
        + subrecord(b"PTRN", struct.pack("<I", preview))
        + subrecord(b"FULL", zstring(name))
        + subrecord(b"MODL", zstring(mesh))
        + subrecord(b"ATTX", zstring(verb))
        + subrecord(b"FNAM", struct.pack("<H", 0)),
    )


DLC06 = 0x01000000  # DLCworkshop03.esm: our second master (its forms are 01xxxxxx here)


def slot_machine(form_id: int, editor_id: str = "WastelandWagersSlotMachine", name: str = "Slot Machine",
                 mesh: str = "WastelandWagers\\SlotMachine.nif") -> bytes:
    """The Vault-Tec Workshop's slot machine (DLC06WorkshopSlotMachineBase) as furniture of ours: our re-skinned model
    (its behaviour graph off: the plugin turns the reels), the DLC's slot machine sitting animations (AnimFurnSlotMachine,
    its furniture marker model), no workshop power, no scripts, not a relaxation object (settlers don't wander over to it
    on their own: the plugin sits them at it). Its marker data copied as it is in the DLC."""
    keywords = [0x000BA2F6,           # FurnitureCantWait (no Wait prompt sat at it)
                DLC06 | 0x001A5C]     # AnimFurnSlotMachine (not the DLC's FurnitureForce3rdPerson: you play it in
                                      # first person, your own eyes on the reels)
    return record(
        b"FURN",
        0,
        form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"OBND", struct.pack("<6h", -20, -61, 0, 20, 22, 125))
        + subrecord(b"PTRN", struct.pack("<I", DLC06 | 0x00252D))
        + subrecord(b"FULL", zstring(name))
        + subrecord(b"MODL", zstring(mesh))
        + subrecord(b"KSIZ", struct.pack("<I", len(keywords)))
        + subrecord(b"KWDA", b"".join(struct.pack("<I", k) for k in keywords))
        + subrecord(b"PNAM", struct.pack("<I", 0))
        + subrecord(b"FNAM", struct.pack("<H", 0))
        + subrecord(b"MNAM", bytes.fromhex("01000040"))
        + subrecord(b"WBDT", b"\x00")
        + subrecord(b"FNPR", bytes.fromhex("01000e00"))
        + subrecord(b"XMRK", zstring("DLC06\\Markers\\WorkshopSlotMachineMarker.nif"))
        + subrecord(b"SNAM", bytes.fromhex("dd2496bfa84637c2000000000000000000000000ffffffff")),
    )


def workshop_recipe(form_id: int, editor_id: str, created: int, components: list[tuple[int, int]],
                    category: int = 0x00118F59, sounds: tuple[int, int] = (0x000196B2, 0x000196BC)) -> bytes:
    """A workshop menu entry, laid out as Fallout4.esm's workshop_co_MetalTableRound01: pick-up and put-down sounds
    (wood), the components, no description, the object, the settlement workbench, the Tables category, makes one."""
    return record(
        b"COBJ",
        0,
        form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"YNAM", struct.pack("<I", sounds[0]))
        + subrecord(b"ZNAM", struct.pack("<I", sounds[1]))
        + subrecord(b"FVPA", b"".join(struct.pack("<II", c, n) for c, n in components))
        + subrecord(b"DESC", zstring(""))
        + subrecord(b"CNAM", struct.pack("<I", created))
        + subrecord(b"BNAM", struct.pack("<I", 0x0005B5E3))
        + subrecord(b"FNAM", struct.pack("<I", category))
        + subrecord(b"INTV", struct.pack("<HH", 1, 0)),
    )


def quest(form_id: int, editor_id: str, package: int, sit: int, priority: int = 100) -> bytes:
    """WastelandWagersQuest: start game enabled, priority 100. Collection aliases 0 'Dealers' and 1 'Bettors', each with
    the walk package. Scripts: WastelandWagersQuest on the quest (properties Dealers and Bettors), WastelandWagersGroupAlias
    on both aliases (papyrus/)."""
    def collection(alias_id: int, name: str, package: int = package) -> bytes:
        # as vanilla WorkshopPermanentActors: ALCS id, ALMI 0 (no initial fill), then the alias: optional, allow reuse
        # in quest, allow reserved (settlers are often reserved by workshop quests)
        return (
            subrecord(b"ALCS", struct.pack("<I", alias_id))
            + subrecord(b"ALMI", b"\x00")
            + subrecord(b"ALST", struct.pack("<I", alias_id))
            + subrecord(b"ALID", zstring(name))
            + subrecord(b"FNAM", struct.pack("<I", 0x0000020A))
            + subrecord(b"ALPC", struct.pack("<I", package))
            + subrecord(b"VTCK", struct.pack("<I", 0))
            + subrecord(b"ALED", b"")
        )
    body = (
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"VMAD", quest_vmad(form_id, "WastelandWagersQuest", [("Dealers", 0), ("Bettors", 1), ("Sitters", 2)],
                                        [(0, "WastelandWagersGroupAlias"), (1, "WastelandWagersGroupAlias"), (2, "WastelandWagersGroupAlias")]))
        # flags (0x10 start game enabled, 0x1 as saved by the CK for those), priority, unknown, delay 0, type 0
        + subrecord(b"DNAM", struct.pack("<HBBfI", 0x0011, priority, 0, 0.0, 0))
        + subrecord(b"NEXT", b"")
        + subrecord(b"ANAM", struct.pack("<I", 3))  # next alias id
        + collection(0, "Dealers")
        + collection(1, "Bettors")
        + collection(2, "Sitters", sit)  # (poker players in their chairs: sitting in them)
    )
    return record(b"QUST", 0, form_id, body)


def slot_idle(form_id: int, editor_id: str, event: str, clip: str) -> bytes:
    """An idle played on whoever's sat at the slot machine (PlayIdle): laid out as the DLC's own
    DLC06_SlotMachineDisappointedIdle (a dynamic idle in the machine's furniture, while sitting), with one of the DLC's
    slot machine sitting animations"""
    sitting = (struct.pack("<B3xf", 0x00, 3.0) + struct.pack("<H2x", 159)  # GetSitting == 3
               + struct.pack("<IIiIi", 0, 0, 0, 0, -1))
    return record(b"IDLE", 0, form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"CTDA", sitting)
        + subrecord(b"DNAM", zstring("actors\\Character\\Behaviors\\RaiderRootBehavior.hkx"))
        + subrecord(b"ENAM", zstring(event))
        + subrecord(b"ANAM", struct.pack("<II", 0, 0))
        + subrecord(b"DATA", bytes([0, 0, 1, 0x53, 0, 0]))
        + subrecord(b"GNAM", zstring(clip if "\\" in clip else "Actors\\DLC06\\Character\\Animations\\SlotMachine\\" + clip)))


def build() -> bytes:
    own = 0x02000000  # our records come after the two masters (Fallout4.esm, DLCworkshop03.esm)
    table = own | FIRST_ID
    acti = [activator(table, "WastelandWagersTable", "Roulette Table", "WastelandWagers\\RouletteTable.nif",
                      (-118, -62, 0, 118, 62, 81), "Play",
                      0x001A03DD)]  # the vanilla pool table's preview framing (PoolTable01's PTRN): a table of much the same size
    stats = [stat(own | (FIRST_ID + 1), "WastelandWagersBall", "WastelandWagers\\Ball.nif", bound=2)]
    for i, v in enumerate((1, 5, 25, 100)):
        stats.append(stat(own | (FIRST_ID + 2 + i), f"WastelandWagersChip{v}", f"WastelandWagers\\Chip{v}.nif", bound=3))
    link = own | (FIRST_ID + 6)
    walk = own | (FIRST_ID + 7)
    seat_link = own | (FIRST_ID + 0x48)
    sit = own | (FIRST_ID + 0x49)
    menu = own | (FIRST_ID + 0x50)  # the Wasteland Wagers workshop category: every table's and the slot machine's recipe
    keywords = [keyword(link, "WastelandWagersLinkSpot"), keyword(seat_link, "WastelandWagersLinkSeat"),
                menu_keyword(menu, "WastelandWagersWorkshopMenu", "Wasteland Wagers")]
    packages = [travel_package(walk, "WastelandWagersWalkToSpot", link), sit_package(sit, "WastelandWagersSitInSeat", seat_link)]
    quests = [quest(own | (FIRST_ID + 8), "WastelandWagersQuest", walk, sit)]
    recipes = [workshop_recipe(own | (FIRST_ID + 9), "workshop_co_WastelandWagersTable", table,
                               [(0x0001FAC2, 12), (0x0001FABD, 4), (0x001223C7, 4), (0x0003D294, 2)], category=menu)]
    # the Roulette Table Kit: sold at the Dugout Inn (the plugin adds it to Vadim's stock); used, a table in front of
    # you. No model: nothing shows in the Pip-Boy or barter previews
    kit = own | (FIRST_ID + 10)
    potions = [potion(kit, "WastelandWagersTableKit", "Roulette Table Kit",
                      "A folding roulette table: wheel, ball, layout and all. Use it to set the table up in front of you; "
                      "sit at it and press P to pack it away again.",
                      "", bytes(12), 400, 25.0)]
    # ...and made at a chem station (its Utility tab): wood 12, steel 6, cloth 4, screws 2, adhesive 2
    recipes.append(recipe(own | (FIRST_ID + 11), "WastelandWagersco_TableKit", kit,
                          [(0x0001FAC2, 12), (0x0001FABD, 6), (0x001223C7, 4), (0x0003D294, 2), (0x0001FAA5, 2)],
                          0x00102158,   # WorkbenchChemlab
                          0x0006980C))  # RecipeUtility

    # ---- poker
    # the 52 cards (xx000810 + suit * 13 + rank: 2c .. Ac, 2d .., 2h .., 2s .. As), each its own model
    for s, suit in enumerate("cdhs"):
        for r, rank in enumerate("23456789TJQKA"):
            stats.append(stat(own | (FIRST_ID + 0x10 + s * 13 + r), f"WastelandWagersCard_{rank}{suit}",
                              f"WastelandWagers\\Cards\\Card_{rank}{suit}.nif", bound=4))
    poker = own | (FIRST_ID + 0x44)
    acti.append(activator(poker, "WastelandWagersPokerTable", "Poker Table", "WastelandWagers\\PokerTable.nif",
                          (-47, -91, 0, 47, 91, 65), "Play", 0x001A03DD))
    recipes.append(workshop_recipe(own | (FIRST_ID + 0x45), "workshop_co_WastelandWagersPokerTable", poker,
                                   [(0x0001FAC2, 10), (0x001223C7, 6), (0x0003D294, 2), (0x0001FAA5, 1)], category=menu))
    pkit = own | (FIRST_ID + 0x46)
    potions.append(potion(pkit, "WastelandWagersPokerKit", "Poker Table Kit",
                          "A folding poker table, cards and all. Use it to set the table up in front of you; sit at it "
                          "and press P to pack it away again.",
                          "", bytes(12), 300, 20.0))
    recipes.append(recipe(own | (FIRST_ID + 0x47), "WastelandWagersco_PokerKit", pkit,
                          [(0x0001FAC2, 10), (0x001223C7, 6), (0x0003D294, 2), (0x0001FAA5, 2)],
                          0x00102158, 0x0006980C))

    # ---- blackjack
    bjt = own | (FIRST_ID + 0x4A)
    acti.append(activator(bjt, "WastelandWagersBlackjackTable", "Blackjack Table", "WastelandWagers\\BlackjackTable.nif",
                          (-73, -49, 0, 73, 49, 69), "Play", 0x001A03DD))
    recipes.append(workshop_recipe(own | (FIRST_ID + 0x4B), "workshop_co_WastelandWagersBlackjackTable", bjt,
                                   [(0x0001FAC2, 12), (0x001223C7, 6), (0x0003D294, 2), (0x0001FAA5, 1)], category=menu))
    bkit = own | (FIRST_ID + 0x4C)
    potions.append(potion(bkit, "WastelandWagersBlackjackKit", "Blackjack Table Kit",
                          "A folding blackjack table, shoe and all. Use it to set the table up in front of you; sit at "
                          "it and press P to pack it away again.",
                          "", bytes(12), 300, 20.0))
    recipes.append(recipe(own | (FIRST_ID + 0x4D), "WastelandWagersco_BlackjackKit", bkit,
                          [(0x0001FAC2, 12), (0x001223C7, 6), (0x0003D294, 2), (0x0001FAA5, 2)],
                          0x00102158, 0x0006980C))

    # ---- the slot machine (the Vault-Tec Workshop's)
    slots = own | (FIRST_ID + 0x4E)
    furniture = [slot_machine(slots)]
    recipes.append(workshop_recipe(own | (FIRST_ID + 0x4F), "workshop_co_WastelandWagersSlotMachine", slots,
                                   [(0x0001FABD, 4), (0x0003D294, 2), (0x0001FAB7, 3), (0x0001FAB0, 2), (0x0001FA9B, 2)],
                                   category=menu, sounds=(0x000196B6, 0x000196BD)))

    # ---- the Used ones: the same tables and machine, post-war (tools/make_used.py: their models, the clean ones with
    # worn textures). Played just the same (the plugin takes each for its clean one); salvaged, so cheaper to build
    used = [
        (0x54, "WastelandWagersTableUsed", "Used Roulette Table", "RouletteTableUsed", (-118, -62, 0, 118, 62, 81),
         [(0x0001FAC2, 8), (0x0001FABD, 2), (0x001223C7, 2), (0x0003D294, 1)]),
        (0x56, "WastelandWagersPokerTableUsed", "Used Poker Table", "PokerTableUsed", (-47, -91, 0, 47, 91, 65),
         [(0x0001FAC2, 6), (0x001223C7, 3), (0x0003D294, 1)]),
        (0x58, "WastelandWagersBlackjackTableUsed", "Used Blackjack Table", "BlackjackTableUsed", (-73, -49, 0, 73, 49, 69),
         [(0x0001FAC2, 8), (0x001223C7, 3), (0x0003D294, 1)]),
    ]
    for off, edid, name, model, bounds, parts in used:
        acti.append(activator(own | (FIRST_ID + off), edid, name, f"WastelandWagers\\{model}.nif", bounds, "Play", 0x001A03DD))
        recipes.append(workshop_recipe(own | (FIRST_ID + off + 1), "workshop_co_" + edid, own | (FIRST_ID + off), parts,
                                       category=menu))
    used_slots = own | (FIRST_ID + 0x5A)
    furniture.append(slot_machine(used_slots, "WastelandWagersSlotMachineUsed", "Used Slot Machine",
                                  "WastelandWagers\\SlotMachineUsed.nif"))
    recipes.append(workshop_recipe(own | (FIRST_ID + 0x5B), "workshop_co_WastelandWagersSlotMachineUsed", used_slots,
                                   [(0x0001FABD, 2), (0x0003D294, 1), (0x0001FAB7, 2), (0x0001FAB0, 1), (0x0001FA9B, 1)],
                                   category=menu, sounds=(0x000196B6, 0x000196BD)))

    # the Wasteland Wagers category's picture: its first item, the roulette table
    art = [menu_art(own | (FIRST_ID + 0x51), "WastelandWagersWorkshopMenuArt", menu, "WastelandWagers\\RouletteTable.nif",
                    0x001A03DD, (-118, -62, 0, 118, 62, 81))]

    # a pull at the machine (the DLC's: the hand to the lever)
    # ...and you sat still at it: its plain sitting loop, looping (a dyn_ActivationLoop: the slot machine's sitting
    # behaviour, WorkbenchFurnitureBehavior, has that and dyn_Flavor, no dyn_FlavorLooping), our copy of the loop
    # (tools/make_slot_hold.py: the DLC's has the IdleFlavor in it that has the furniture pick flavours, most of them a
    # pull). The DLC's own machines and animations untouched
    idles = [slot_idle(own | (FIRST_ID + 0x52), "WastelandWagersSlotPullIdle", "dyn_Flavor", "PoseA_IdleFlavor1.hkx"),
             slot_idle(own | (FIRST_ID + 0x53), "WastelandWagersSlotHoldIdle", "dyn_ActivationLoop",
                       "Actors\\Character\\Animations\\WastelandWagers\\SlotMachineHold.hkx")]

    count = (len(acti) + len(stats) + len(keywords) + len(packages) + len(quests) + len(recipes) + len(potions)
             + len(furniture) + len(art) + len(idles))
    header = record(
        b"TES4",
        FLAG_MASTER | FLAG_LIGHT,
        0,
        subrecord(b"HEDR", struct.pack("<fiI", 1.0, count, FIRST_ID + 0x5C))
        + subrecord(b"CNAM", zstring("evodube"))
        + subrecord(b"SNAM", zstring("Wasteland Wagers: playable roulette, poker and blackjack tables, and slot machines"))
        + subrecord(b"MAST", zstring("Fallout4.esm"))
        + subrecord(b"DATA", struct.pack("<Q", 0))
        + subrecord(b"MAST", zstring("DLCworkshop03.esm"))
        + subrecord(b"DATA", struct.pack("<Q", 0)),
    )
    # groups in the order the game's own master has them
    return (
        header
        + group(b"KYWD", b"".join(keywords))
        + group(b"ACTI", b"".join(acti))
        + group(b"STAT", b"".join(stats))
        + group(b"FURN", b"".join(furniture))
        + group(b"ALCH", b"".join(potions))
        + group(b"QUST", b"".join(quests))
        + group(b"IDLE", b"".join(idles))
        + group(b"PACK", b"".join(packages))
        + group(b"ARTO", b"".join(art))
        + group(b"COBJ", b"".join(recipes))
    )


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = args[0] if args else "WastelandWagers.esl"
    data = build()
    with open(out, "wb") as f:
        f.write(data)
    print(f"wrote {out} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
