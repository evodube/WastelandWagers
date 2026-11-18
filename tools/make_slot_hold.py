"""The slot machine's sitting animations without their idle flavours: the DLC's sit-down (EnterFromStand.hkx) and
sitting loop (PoseA_Idle1.hkx), from DLCworkshop03 - Main.ba2, each with its IdleFlavor annotation renamed. IdleFlavor
(the sit-down's end, 5.3 s into each loop) is what has the furniture pick a flavour, most of them a pull of the lever:
renamed, whoever's sat at a machine only pulls when the plugin plays the pull on them (you: when you pull). Same
length, so nothing else in the files moves.

    python3 tools/make_slot_hold.py [<Data dir>]

Writes, as loose files over the DLC's own, Meshes\\Actors\\DLC06\\Character\\Animations\\SlotMachine\\EnterFromStand.hkx and
PoseA_Idle1.hkx (every slot machine sitter's, the DLC's own machines' too: no flavours there either); and
Meshes\\Actors\\Character\\Animations\\fo4roulette\\SlotMachineHold.hkx (the loop again, fo4rouletteSlotHoldIdle's).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def renamed(name: str) -> bytes:
    with open(os.path.join(HERE, "source_slots", name), "rb") as f:
        d = f.read()
    old, new = b"IdleFlavor\x00", b"SlotHoldOn\x00"
    assert d.count(old) == 1 and len(old) == len(new)
    return d.replace(old, new)


def write(path: str, data: bytes) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def main(data: str) -> None:
    dlc = os.path.join(data, "Meshes", "Actors", "DLC06", "Character", "Animations", "SlotMachine")
    write(os.path.join(dlc, "EnterFromStand.hkx"), renamed("EnterFromStand.hkx"))
    write(os.path.join(dlc, "PoseA_Idle1.hkx"), renamed("PoseA_Idle1.hkx"))
    write(os.path.join(data, "Meshes", "Actors", "Character", "Animations", "fo4roulette", "SlotMachineHold.hkx"),
          renamed("PoseA_Idle1.hkx"))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "dist", "Data"))
