# Work list

The prioritized list of open work, scored as [docs/workflow.md](workflow.md) describes: **score = severity weight x
reach x confidence** (S0 8, S1 5, S2 3, S3 1; reach 0-1 from `scripts/hardware_report.py` and the Steam survey;
confidence 1.0 reproduced with a log, 0.7 two or more reporters with logs, 0.4 one report or no log). Work from the
top of **Open**. A GitHub issue stays the full record; this list is the order.

How to use it:

- **Pick:** take the highest-scored row with no owner that you can reproduce on your hardware, claim it in Discord
  #dev-work, and put your name in *Owner* in the same PR that starts the work (or a one-line PR).
- **Add:** a new report becomes a row in **Open** with its three parts filled in, so the score can be checked. Link the
  issue or the Discord thread. One row per problem: if a row already covers it, add the link to that row and raise its
  confidence instead of adding a second row.
- **Move:** when a PR merges, move the row to **Fixed, waiting for a release** with the PR; when a release ships it,
  delete the row (the release notes are the record). A row nobody can reproduce for two weeks, with logs asked for,
  goes to **Parked**.
- Keep each table sorted by score, highest first. Re-score when new logs or hardware numbers arrive.

Agents may add and re-score rows; a person reviews the change like any other PR.

## Open

| Score | Problem | S | Reach | Conf. | Owner | Links |
|---:|---|---|---:|---:|---|---|
| 3.0 | Shader programs that still do not translate (TLD multisample bit 78, LDG descriptor without `.E`, programs using the lowering scratch registers, uniform-addressed global loads) | S2 | 1.0 | 1.0 | | `--prepare-shaders` log; Legion Go 2 log in #bug-reporting |
| 2.1 | Camera turns in Kokiri Forest stutter: 46 fps minimum, dips to 38 (14700KF); goal is a 60 fps minimum | S2 | 1.0 | 0.7 | Joey-Magnus | Reddit report; #dev-work claim |
| 2.0 | Antivirus flags HookshotBuilder.exe (Defender `Wacatac.H!ml` / `Wacatac.B!ml`, Chrome on v2.0.12): new unsigned exe, no reputation. Waiting on the SignPath open-source signing application; then sign in `windows-builder.yml` and publish the signed files | S1 | 0.57 | 0.7 | NotRexed (SignPath) | #1003, #525, #1620; checksums: #1307 |
| 1.6 | Stops after the first cutscene (Navi, Deku Tree): black screen, or the PC freezes or crashes, with no error in the logs; AMD (RX 9060 XT, RX 6700 XT, RX 6600, 5700G, Z1 Extreme) and one NVIDIA card (1080 Ti) reported; Linux too | S0 | 0.51 | 0.4 | | #bug-reporting "black screen after a first cutscene", "Crashes entire computer" (one log is from v2.0.7, the other not read yet: confidence goes up with a current log) |
| 1.2 | Link keeps playing a jump animation in dialogue after the Gohma fight (gameplay fine) | S2 | 1.0 | 0.4 | | #bug-reporting "My link is jumping forever" |
| 1.2 | Ground looks smeared, without detail: Kokiri Forest / Lost Woods, and also Dodongo's Cave and Hyrule Field on RX 9070 XT (driver 26.10.2, 1440p, no mods; log received, clears after a restart) | S2 | 1.0 | 0.4 | | #bug-reporting "Ground design is not loading properly" |
| 1.2 | Trees float above the ground (Kakariko, Hyrule Field): reported as already fixed, check which change and that it shipped | S2 | 1.0 | 0.4 | | tracker #994 |
| 0.6 | Zora's Domain textures missing in the adult timeline (points to VRAM pressure, not the dump) | S2 | 0.3 | 0.7 | | #bug-reporting "Zora Domain textures missing?" |
| 0.5 | Sacred Forest Meadow fairy fountain grotto: floor and walls invisible (RTX 2070 Super) | S2 | 0.4 | 0.4 | | #bug-reporting thread |
| 0.4 | Voices in the wrong language | S3 | 1.0 | 0.4 | | #1460 (needs a save past Gohma) |
| 0.29 | Amd RX 580 (Polaris, no mesh shaders): hair renders white and bright, and the game crashes when Link moves; a second RX 580 8 GB user reports the same | S1 | 0.145 | 0.4 | | #bug-reporting "Amd rx 580 crashes" (no log yet) |
| 0.24 | Handheld with no keyboard (Asus ROG Xbox Ally X, controller only): fullscreen and the F1 options menu cannot be reached; no log yet | S1 | 0.12 | 0.4 | | #bug-reporting "Xinput devices cannot enter fullscreen or menu" |
| 0.12 | AMD RX 9070 XT: texture glitch that appears after some play (since v2.0.5); same as the tracked AMD texture artifact | S2 | 0.10 | 0.4 | | #bug-reporting "texture glitch on 9070 XT aris"; #57, #1006 |
| 0.11 | Intel GPU: black screen after loading a save (or on the Deku Tree), sound only, very low fps; three reports, no logs yet; tracked in the GitHub issue linked in #bug-reporting replies (#999) | S0 | 0.035 | 0.4 | | #bug-reporting "Intel G3E. Title and intro loads...", "intel arc b580 problems", "game gets stuck on a black screen when loading a save (intel gpu... gulp...)" |
| 0.11 | Crash at the door after the owl intro (Navi wakes Link, 2.0.13): access violation in `igvk64.dll` at start-up; GPU not given yet, reach uses Intel's 3.5% until it is | S0 | 0.035 | 0.4 | | #bug-reporting "2.0.13 Crash after you see the owl during intro cutscene" (5 logs) |


## Fixed, waiting for a release

| Problem | Fixed by | Notes |
|---|---|---|
| 1D surface loads (SULD.1D) stop 16-17 compute programs from translating; fits the green glow on a Legion Go 2 (890M) | #1447 | in `main`, not in v2.0.13 (log: #bug-reporting "Bugs 2.0.13 Dodaza") |
| Integer min/max with a wide immediate (IMNMX) does not translate | #1451 | in `main`, not in v2.0.13 (log: #bug-reporting "Bugs 2.0.13 Dodaza") |
| Insert menu purple with HDR10 on (v2.0.13 GDI overlay drawn into the HDR swapchain unconverted) | `main`'s ImGui overlay (`hdr10()`) | needs the release branch merged back into `main` |

## Parked

| Problem | Why parked | Links |
|---|---|---|
| | | |
