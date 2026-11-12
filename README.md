# UNDERCRYPT — TI-84 Plus CE dungeon game

UNDERCRYPT is a room-by-room action dungeon for the TI-84 Plus CE. The title screen has a main menu, Story and Infinite modes, a saved records page, and a field guide. Story mode has three guardian fights. Infinite mode loops through new floors and increases enemy and boss difficulty as you descend.

Arrow keys move, 2ND fires, and ALPHA dashes with a decoy afterimage. Movement is faster than the earlier build and the dash has a cooldown meter. The three small enemies have different behaviors: a chaser, a ranged gunner, and a blinking ambusher. Four room layouts rotate through each floor, with cover, healing sigils, and a telegraphed floor hazard. Stone cover blocks both enemy fire and the player's bolts, so its placement matters.

Bosses have substantially more health and three escalating attack phases. The Bone Regent sends marked volleys, the Glass Widow marks and sweeps laser lanes, and the Hollow Engine creates rotating rings with a visible safe gap. Beating a guardian opens the exit; walk through it to advance. Each guardian restores one health point and improves fire rate. Records track runs, rooms, enemies, guardians, dashes, best floor, and best score. On calculator, these records are saved in the `UCRSTAT` AppVar.

All graphics are drawn from small colored shapes in the program. There are no external sprite or data files.

## Controls

- Arrow keys: move
- 2ND: fire; hold to fire repeatedly
- ALPHA: dash and leave a decoy afterimage
- CLEAR: pause; ENTER resumes, CLEAR returns to the main menu
- ENTER: confirm a menu option; after a run, return to the main menu
- CLEAR at the main menu: quit

## Build an 8XP in GitHub

1. Create a GitHub repository and upload the contents of this folder to its root.
2. Commit and push. The included `.github/workflows/main.yml` builds `bin/UNDERCRY.8xp` (the on-screen title remains UNDERCRYPT; TI-84 program names are limited to eight characters).
3. Open **Actions**, download the `UNDERCRYPT-8XP` artifact, and send `UNDERCRY.8xp` to the calculator with TI Connect CE.

The workflow rejects a program larger than **76,800 bytes (75 KiB)**. It uses CEdev's C toolchain; no AppVars are required.
