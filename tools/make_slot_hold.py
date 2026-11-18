"""You sat still at a slot machine: the DLC's plain sitting loop (PoseA_IdleFlavor's base, PoseA_Idle1.hkx from
DLCworkshop03 - Main.ba2) with its one annotation that matters renamed. Its IdleFlavor (5.3 s in) is what has the
furniture pick a flavour (most of them a pull of the lever): renamed, the loop just loops. Same length, so nothing else
in the file moves.

    python3 tools/make_slot_hold.py [<Data dir>]

Writes Meshes\\Actors\\Character\\Animations\\fo4roulette\\SlotMachineHold.hkx (fo4rouletteSlotHoldIdle's animation).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main(data: str) -> None:
    with open(os.path.join(HERE, "source_slots", "PoseA_Idle1.hkx"), "rb") as f:
        d = f.read()
    old, new = b"IdleFlavor\x00", b"SlotHoldOn\x00"
    assert d.count(old) == 1 and len(old) == len(new)
    d = d.replace(old, new)
    out = os.path.join(data, "Meshes", "Actors", "Character", "Animations", "fo4roulette")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "SlotMachineHold.hkx"), "wb") as f:
        f.write(d)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "dist", "Data"))
