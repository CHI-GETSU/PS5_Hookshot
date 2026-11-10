# Tested configurations

What has actually been run, where, and how it went: one row per test run, newest first. The README's
[status table](../readme.md#project-status) sums this up by vendor; this page keeps the runs themselves, so a
regression can be traced to a build and a driver, and so the next tester knows what is already covered.

Add a row for your own runs (a PR, or paste it in an issue and a maintainer adds it). A run that worked is as useful as
one that did not. Keep it to facts you saw: the build (`hookshot vX.Y.Z-N-gHASH` from the first line of
`hookshot_log.txt`), the hardware and driver, the settings that matter, where in the game, and what you measured.
Frame rates come from `hookshot_log.txt` ("N presents, X fps") at a named spot, standing still unless the row says
otherwise.

## Runs

| Date | Build | OS / desktop | GPU / driver | CPU / RAM | Settings | Where | Result |
|---|---|---|---|---|---|---|---|
| 2026-11-10 | main b2bb214 (+ #1447 #1451 #1520 #1660 #1619 #1448 #1654 #1612 #1600, unmerged then) | Ubuntu 26.04.1, KDE Plasma 6.6.6 Wayland | RTX 4070 Ti 12 GB, 595.99.02 | Ryzen 7 9800X3D, 32 GB | 3840x2160 output, DLSS Quality, no mods | Hyrule Castle road (child) | 48-50 fps. Same spot at 2560x1440 output: 56.3 fps. No device lost, saves written |
| 2026-11-10 | main b2bb214 + #1459 (Dear ImGui menu) | as above | as above | as above | as above, windowed | Title, Hyrule Castle road | F1 menu, help box, prompts, the game's font and the FPS badge all correct; 60 fps |
| 2026-11-10 | main b2bb214 | as above | as above | as above | HDR on | start-up | KDE offers an HDR10 surface (`vk: HDR: an HDR10 surface (format 58)`) |
| 2026-11-09 | main 7eac5bf | as above | as above | as above | 3840x2160, DLSS Quality, no mods | Hyrule Castle road | 52.8 fps (replay submit 17.5 ms, memory sync 4.8 ms, GPU 12.8 ms) |
| 2026-11-09 | main 7eac5bf | as above | as above | as above | fullscreen (`HOS_FULLSCREEN=1`) | Hyrule Castle road | fullscreen works, no device lost |
| 2026-11-09 | v2.0.11-142 (7dbdcdc) | as above | as above | as above | 3840x2160, DLSS Quality | Water Temple 1F, Hyrule Castle road, Deku Tree 2F | Water Temple walls missing with the 23.3 GB dump, drawn with stand-in textures (#1308) or a complete dump. Slingshot and bow aiming: no fps change (51-52 fps, #1305). Pause / unpause: no freeze |

## Not covered yet on this hardware

- Real mouse aiming with Mouse camera = Native after #1462 (the camera states were traced, but not tested with a real mouse).
- Frame generation at 120 Hz or above (the displays here top out at 75 Hz).
- HDR output on an HDR monitor (the surface is offered, the picture was not judged on one).
