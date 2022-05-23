# The Dark Mod VR

![The Dark Mod VR][logo]

This is a VR mod for the free game "The Dark Mod", a Thief series inspired first-person stealth game.

The mod is still a work in progress. It is currently a seated keyboard+mouse (or [Gamepad](https://github.com/fholger/thedarkmodvr/wiki/Gamepad-support)) experience only, without any roomscale or motion controller support. It is recommended to experienced VR players only, and familiarity with the base game in non-VR play is going to be helpful.

A full room-scale experience is the long-term goal, but there's still a long way to go :)
Also, I am currently spending most of my free time on [Half-Life 2 VR](https://halflife2vr.com), so
development on The Dark Mod VR is effectively on hold. I do intend to return to this project as soon as
I have time :)

Please have a look at the [Wiki](https://github.com/fholger/thedarkmodvr/wiki) for more information and advanced settings.

## Installation

TheDarkMod has an installer that allows you to directly install the VR version. Follow the instructions in the Wiki page: [Installation](https://github.com/fholger/thedarkmodvr/wiki/Installation)

If you already have the current [version 2.10](https://www.thedarkmod.com/posts/the-dark-mod-2-10-has-been-released/) of the base game installed, you can also head over to the [releases](https://github.com/fholger/thedarkmodvr/releases) and grab the latest version of the VR build. Extract it to your Dark Mod directory and run the VR executable.

If you're feeling brave, you can also download a nightly build. Head to [AppVeyor](https://ci.appveyor.com/project/fholger/thedarkmodvr), select either the Visual Studio 2022 (for Windows) or Ubuntu 18.04 (for Linux) job, then go to the Artifacts tab and download the build.

## Selecting the VR backend

The Dark Mod VR uses OpenXR as its VR API, which means it can run natively on any PCVR headset. Make sure
the preferred runtime for your headset (SteamVR, Oculus or WMR) is set as the default OpenXR runtime, then
just launch TheDarkModVRx64.exe.

## Calibrate seated position

The game requests the seated reference position from your runtime, so it's important that you have it properly calibrated in your runtime, or else your view point will misalign with the actual player position and you may be stuck in a wall/ceiling, or the UI overlay will not be visible! To do so, once you've assumed your seating position:

* SteamVR: choose "reset seated position". There's a button for it in the SteamVR overlay, alternatively on your desktop click on the settings icon for SteamVR, and the option should be available there in the menu.
* Oculus: press and hold the Oculus button to recenter your view.

[logo]: https://github.com/fholger/thedarkmodvr/raw/master/thedarkmodvr.png
