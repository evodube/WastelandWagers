#!/usr/bin/env python3
"""Writes fo4roulette.esl: the roulette table and its pieces, and the quest that walks NPCs to it.

    python tools/make_esl.py [out.esl]

Form IDs (src/Forms.h expects these):
    xx000800  ACTI fo4rouletteTable        Roulette Table: Meshes\\fo4roulette\\RouletteTable.nif (activate: Play)
    xx000801  STAT fo4rouletteBall         Meshes\\fo4roulette\\Ball.nif
    xx000802  STAT fo4rouletteChip1        Meshes\\fo4roulette\\Chip1.nif   (and 5, 25, 100 at 803-805)
    xx000806  KYWD fo4rouletteLinkSpot     an NPC's linked ref: the spot by the table they're to walk to
    xx000807  PACK fo4rouletteWalkToSpot   Travel to that linked ref, walking, facing the marker at the end
    xx000808  QUST fo4rouletteQuest        priority 100, start game enabled; collection aliases 0 Dealers and 1
                                           Bettors, both carrying the package (alias packages go by quest priority,
                                           so it outranks settler sandboxes)
    xx000809  COBJ workshop_co_fo4rouletteTable  the table in the workshop menu (Furniture > Tables, as the vanilla
                                           tables): wood 12, steel 4, cloth 4, screws 2

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


def keyword(form_id: int, editor_id: str) -> bytes:
    # colour (white), type 0 (none): as vanilla link keywords (RELinkTravel)
    return record(
        b"KYWD", 0, form_id,
        subrecord(b"EDID", zstring(editor_id)) + subrecord(b"CNAM", bytes.fromhex("ffffff00")) + subrecord(b"TNAM", struct.pack("<I", 0)),
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




def activator(form_id: int, editor_id: str, name: str, mesh: str, bounds: tuple, verb: str) -> bytes:
    """An activator with no script: activating it is what the plugin listens for (TESActivateEvent)."""
    return record(
        b"ACTI",
        0,
        form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"OBND", struct.pack("<6h", *bounds))
        + subrecord(b"FULL", zstring(name))
        + subrecord(b"MODL", zstring(mesh))
        + subrecord(b"ATTX", zstring(verb))
        + subrecord(b"FNAM", struct.pack("<H", 0)),
    )


def workshop_recipe(form_id: int, editor_id: str, created: int, components: list[tuple[int, int]]) -> bytes:
    """A workshop menu entry, laid out as Fallout4.esm's workshop_co_MetalTableRound01: pick-up and put-down sounds
    (wood), the components, no description, the object, the settlement workbench, the Tables category, makes one."""
    return record(
        b"COBJ",
        0,
        form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"YNAM", struct.pack("<I", 0x000196B2))
        + subrecord(b"ZNAM", struct.pack("<I", 0x000196BC))
        + subrecord(b"FVPA", b"".join(struct.pack("<II", c, n) for c, n in components))
        + subrecord(b"DESC", zstring(""))
        + subrecord(b"CNAM", struct.pack("<I", created))
        + subrecord(b"BNAM", struct.pack("<I", 0x0005B5E3))
        + subrecord(b"FNAM", struct.pack("<I", 0x00118F59))
        + subrecord(b"INTV", struct.pack("<HH", 1, 0)),
    )


def quest(form_id: int, editor_id: str, package: int, priority: int = 100) -> bytes:
    """fo4rouletteQuest: start game enabled, priority 100. Collection aliases 0 'Dealers' and 1 'Bettors', each with
    the walk package. Scripts: fo4rouletteQuest on the quest (properties Dealers and Bettors), fo4rouletteGroupAlias
    on both aliases (papyrus/)."""
    def collection(alias_id: int, name: str) -> bytes:
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
        + subrecord(b"VMAD", quest_vmad(form_id, "fo4rouletteQuest", [("Dealers", 0), ("Bettors", 1)],
                                        [(0, "fo4rouletteGroupAlias"), (1, "fo4rouletteGroupAlias")]))
        # flags (0x10 start game enabled, 0x1 as saved by the CK for those), priority, unknown, delay 0, type 0
        + subrecord(b"DNAM", struct.pack("<HBBfI", 0x0011, priority, 0, 0.0, 0))
        + subrecord(b"NEXT", b"")
        + subrecord(b"ANAM", struct.pack("<I", 2))  # next alias id
        + collection(0, "Dealers")
        + collection(1, "Bettors")
    )
    return record(b"QUST", 0, form_id, body)


def build() -> bytes:
    own = 0x01000000  # our records come after the one master
    table = own | FIRST_ID
    acti = [activator(table, "fo4rouletteTable", "Roulette Table", "fo4roulette\\RouletteTable.nif",
                      (-139, -85, 0, 139, 85, 74), "Play")]
    stats = [stat(own | (FIRST_ID + 1), "fo4rouletteBall", "fo4roulette\\Ball.nif", bound=2)]
    for i, v in enumerate((1, 5, 25, 100)):
        stats.append(stat(own | (FIRST_ID + 2 + i), f"fo4rouletteChip{v}", f"fo4roulette\\Chip{v}.nif", bound=3))
    link = own | (FIRST_ID + 6)
    walk = own | (FIRST_ID + 7)
    keywords = [keyword(link, "fo4rouletteLinkSpot")]
    packages = [travel_package(walk, "fo4rouletteWalkToSpot", link)]
    quests = [quest(own | (FIRST_ID + 8), "fo4rouletteQuest", walk)]
    recipes = [workshop_recipe(own | (FIRST_ID + 9), "workshop_co_fo4rouletteTable", table,
                               [(0x0001FAC2, 12), (0x0001FABD, 4), (0x001223C7, 4), (0x0003D294, 2)])]

    count = len(acti) + len(stats) + len(keywords) + len(packages) + len(quests) + len(recipes)
    header = record(
        b"TES4",
        FLAG_MASTER | FLAG_LIGHT,
        0,
        subrecord(b"HEDR", struct.pack("<fiI", 1.0, count, FIRST_ID + 10))
        + subrecord(b"CNAM", zstring("evodube"))
        + subrecord(b"SNAM", zstring("Real Roulette: a playable roulette table"))
        + subrecord(b"MAST", zstring("Fallout4.esm"))
        + subrecord(b"DATA", struct.pack("<Q", 0)),
    )
    # groups in the order the game's own master has them
    return (
        header
        + group(b"KYWD", b"".join(keywords))
        + group(b"ACTI", b"".join(acti))
        + group(b"STAT", b"".join(stats))
        + group(b"QUST", b"".join(quests))
        + group(b"PACK", b"".join(packages))
        + group(b"COBJ", b"".join(recipes))
    )


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = args[0] if args else "fo4roulette.esl"
    data = build()
    with open(out, "wb") as f:
        f.write(data)
    print(f"wrote {out} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
