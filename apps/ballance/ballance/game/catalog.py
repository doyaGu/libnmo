"""The names the game reads a level by (docs/ballance-level-format.md):
groups, element kinds, markers, and the mechanisms the PH templates make."""

from __future__ import annotations

import re
from typing import NamedTuple

# The classes the original level files hold.
LEVEL_CLASSES = {"CK3dObject", "CKMesh", "CKMaterial", "CKTexture", "CKGroup"}

# The markers: start, checkpoints, reset points, balloon.
MARKERS = ["PS_FourFlames", "PC_TwoFlames", "PR_Resetpoint", "PE_Balloon"]

# Levelinit's PH_Groups: the element kinds, each a template in 3D Entities/PH.
ELEMENT_KINDS = [
    "P_Extra_Life", "P_Extra_Point", "P_Trafo_Paper", "P_Trafo_Stone", "P_Trafo_Wood",
    "P_Ball_Paper", "P_Ball_Stone", "P_Ball_Wood", "P_Box", "P_Dome",
    "P_Modul_01", "P_Modul_03", "P_Modul_08", "P_Modul_17", "P_Modul_18", "P_Modul_19",
    "P_Modul_25", "P_Modul_26", "P_Modul_29", "P_Modul_30", "P_Modul_34", "P_Modul_37",
    "P_Modul_41",
]

SOUND_GROUPS = [f"Sound_{kind}ID_0{i}" for i in (1, 2, 3) for kind in ("Roll", "Hit")]
# Groups the game looks up by name; without one it logs a single error.
NAMED_GROUPS = ["Phys_FloorRails", "Phys_FloorStopper", "Shadow"] + SOUND_GROUPS

# The groups a generated level makes.  Levelinit iterates the element groups
# (P_*) and the depth test groups by name too; a missing one costs a single
# load-time error, as in the original levels, so only the used ones are made.
LEVEL_GROUPS = [
    "PS_Levelstart",
    "PE_Levelende",
    "PC_Checkpoints",
    "PR_Resetpoints",
    "Phys_Floors",
    "Phys_FloorRails",
    "Phys_FloorStopper",
    "Shadow",
    "DepthTestCubes",
    "Sound_RollID_01",
    "Sound_HitID_01",
    "Sound_RollID_02",
    "Sound_HitID_02",
    "Sound_RollID_03",
    "Sound_HitID_03",
]

# A stone floor: physics, stone rolling and hit sounds, the ball's shadow.
STONE_FLOOR_GROUPS = ["Phys_Floors", "Sound_RollID_01", "Sound_HitID_01", "Shadow"]
# A wood floor: physics, wood rolling and hit sounds, the ball's shadow.
WOOD_FLOOR_GROUPS = ["Phys_Floors", "Sound_RollID_02", "Sound_HitID_02", "Shadow"]
# Rails: physics and the metal sounds, no shadow, as the originals' have.
RAIL_GROUPS = ["Phys_FloorRails", "Sound_RollID_03", "Sound_HitID_03"]
# Railings ring as the ball hits them but make no rolling sound, as Level_01's
# A01_Rail_04 has it.
RAILING_GROUPS = ["Phys_FloorRails", "Sound_HitID_03"]
# Stoppers collide with the ball only, and make no sound.
STOPPER_GROUPS = ["Phys_FloorStopper"]


def element_kind(name: str) -> str | None:
    """The element kind of an object's name: P_Box_03 -> P_Box,
    P_Modul_18_07 -> P_Modul_18."""
    for kind in ELEMENT_KINDS:
        if re.fullmatch(re.escape(kind) + r"_\d+\w*", name):
            return kind
    return None


class Mechanism(NamedTuple):
    key: str  # the group, or for a marker the start of its name ("PS_")
    name: str
    colour: tuple[int, int, int]
    note: str  # how it plays, as far as known


# The mechanisms, elements and markers, as the level views name them.
MECHANISMS = [
    Mechanism("P_Modul_01", "pusher", (255, 120, 60),
              "stone key in a groove under the road and a frame with a target; "
              "woken near the ball"),
    Mechanism("P_Modul_03", "gate", (190, 140, 255), "walls and a gate on a floor of its own"),
    Mechanism("P_Modul_08", "swing", (120, 220, 120), "a swing hung from a fixed frame"),
    Mechanism("P_Modul_17", "rotating arms", (255, 90, 160), "arms turning over the track"),
    Mechanism("P_Modul_18", "fan", (80, 200, 255), "lifts a paper ball 21 up"),
    Mechanism("P_Modul_19", "flaps", (230, 200, 90), "flaps on an axle"),
    Mechanism("P_Modul_25", "drawbridge", (255, 170, 60),
              "plank the ball pushes over a gap 10 long"),
    Mechanism("P_Modul_26", "sack", (200, 120, 70), "swings across the track"),
    Mechanism("P_Modul_29", "plates", (140, 200, 200), "nine plates"),
    Mechanism("P_Modul_30", "seesaw", (170, 230, 70), "a seesaw"),
    Mechanism("P_Modul_34", "stone on box", (180, 180, 220),
              "stands on a box in a lower channel; drops flush when the box is pushed out"),
    Mechanism("P_Modul_37", "switch", (255, 60, 60),
              "a stone ball on the target tips the plank across the cross track's gap"),
    Mechanism("P_Modul_41", "slabs", (60, 220, 200),
              "tip under a wood or stone ball; paper crosses"),
    Mechanism("P_Trafo_Paper", "to paper", (245, 245, 245), "ball changer"),
    Mechanism("P_Trafo_Stone", "to stone", (150, 150, 160), "ball changer"),
    Mechanism("P_Trafo_Wood", "to wood", (190, 130, 70), "ball changer"),
    Mechanism("P_Ball_Paper", "paper ball", (235, 235, 210), "loose ball to push"),
    Mechanism("P_Ball_Stone", "stone ball", (120, 120, 130), "loose ball to push"),
    Mechanism("P_Ball_Wood", "wood ball", (170, 110, 50), "loose ball to push"),
    Mechanism("P_Box", "box", (150, 95, 50), "box to push"),
    Mechanism("P_Dome", "dome", (90, 170, 90), "dome"),
    Mechanism("P_Extra_Point", "extra point", (250, 220, 40), "points"),
    Mechanism("P_Extra_Life", "extra life", (250, 120, 200), "a life"),
    Mechanism("PS_", "start", (230, 40, 40), "the level starts here"),
    Mechanism("PC_", "checkpoint", (230, 40, 40), "ends a sector"),
    Mechanism("PR_", "reset point", (240, 110, 110), "the ball comes back here"),
    Mechanism("PE_", "balloon", (230, 40, 40), "the level ends here"),
]


def mechanism_of(name: str, groups: list[str]) -> Mechanism | None:
    """The mechanism, element or marker an object is, by its groups or, for a
    marker, its name."""
    for mechanism in MECHANISMS:
        key = mechanism.key
        if key.endswith("_") and name.startswith(key) or key in groups:
            return mechanism
    return None
