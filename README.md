# The Dark Mod VR

This is a VR mod for the free game "The Dark Mod", a Thief series inspired first-person stealth game.

The mod is in early development. It is currently a seated keyboard+mouse experience only, without any comfort options. It is recommended to experienced VR players only, and familiarity with the base game in non-VR play is going to be helpful. A full room-scale experience is the long-term goal, but there's a long way to go :)

## Installation

You need to install v2.08 of the base game. Head over to [The Dark Mod website](https://www.thedarkmod.com/downloads/) and download the TDM updater for your platform. Create a new directory for TDM, extract the updater into that directory and run it. The directory must be user-writable, so it's recommended to place it somewhere in your user home. Do *not* use "C:\Program Files"!

Then head over to the [releases](https://github.com/fholger/thedarkmodvr/releases) and grab the latest version of the VR build. Extract it to your Dark Mod directory and run the VR executable.

If you're feeling brave, you can also download a nightly build. Head to [AppVeyor](https://ci.appveyor.com/project/fholger/thedarkmodvr), select either the Visual Studio 2019 (for Windows) or Ubuntu 18.04 (for Linux) job, then go to the Artifacts tab and download the build.

## Selecting the VR backend

By default, this mod is using OpenVR as its backend, so will launch through SteamVR. There is an alternative OpenXR backend, which will eventually become the default and only backend. But right now, if you want to use it, you have to explicitly ask for it.

Create a file called `autoexec_vr.cfg` in your DarkMod folder and put the line `set vr_backend 1` into it. If you want to go back to OpenVR, change the `1` into a `0`.