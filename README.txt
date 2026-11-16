BFBC Recomp - Beta 2.0
========================

Quick start
1. Put your own game files (default.xex plus the content folders) in game\.
2. Run bin\bfbc.exe. No arguments needed.
3. Keys: F5 settings, F3 stats, F4 all cvars, ` console.

No game files are included.

Notes
- Pads: Xbox (XInput) and PlayStation (SDL) work at the same time; hot-plug is
  supported and both are merged into player 1.
- F5 -> Rendering has Readback resolve and Render target path (RTV/ROV).
- RTV is the default (faster). ROV fixes smoke/transparency but may hitch or
  crash on AMD with SDK 0.10.0.
- Internal resolution, readback mode and render target path need a restart.
- The game runs at its own ~30 fps; there is no true 60 fps mode.
