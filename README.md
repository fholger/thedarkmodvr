The Dark Mod VR
===============

This is a modification of [The Dark Mod](http://www.thedarkmod.com) which adds VR rendering via OpenVR. It is currently in a very early stage of development. Basic headtracking and stereo rendering are working, but it is currently only playable as a seated experience with keyboard and mouse. There are currently no comfort options implemented, so if you cannot handle artificial movement and rotation in VR, you should skip this experience for now. No controller support. GUI and HUD elements have not yet been adapted and are only usable via the mirror screen.

The VR mod is based on the v2.05 codebase of The Dark Mod.

## Installation
Download and install The Dark Mod v2.05 from here: http://www.thedarkmod.com/download-the-mod/
Then download the current release of this VR mod and extract all files to the directory where you installed The Dark Mod. Then run it as normal. Don't forget to reset your seated position in SteamVR.

## Known caveats
- Some shadows are rendered inconsistently between left and right eye views, leading to visual artifacts.
- HUD elements, menus and other GUI elements are not yet adapted to VR and are effectively unusable in the headset.
- Some significant modifications were done to the engine to improve performance for VR, which may result in random crashes or other bugs.
- The head tracking is completely decoupled from any game logic. So you can totally cheat and peak/walk around corners without anyone noticing you. It also means that mouse look is not synced with your actual head position and may result in awkward rotations.

## Roadmap
- Adapt all GUI elements to be usable in a seated experience.
- Potentially improve performance further.
- Potentially couple head tracking with game logic.
- Move towards room-scale support?