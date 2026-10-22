# Virtua Racing

Virtua Racing (Sega, 1992) is supported as a full single-cabinet game: attract mode, every course, qualifying,
races, results and rankings, the service menu and free play. Linked (multi-cabinet) play isn't supported. The
desktop build and release packages recompile the V60 program to native code at startup. Run with `--interp` to
use the interpreter instead.

For shared Model 1 architecture, build requirements, and repository information, see the
[root README](../../../README.md). For the port's implementation and validation history, see
[How it was made](../../../docs/HOW-IT-WAS-MADE.md).

> No game files are included. You need your own MAME `vr` set (`vr.zip`) and the Model 1 I/O-board ROM
> (`model1io.zip`). The game is an unofficial fan project,
> not made, approved, or supported by Sega.

## Run Virtua Racing

Build and run the desktop host:

```sh
make vr
build/bin/vr
```

Like Virtua Fighter, the VR host reads your MAME set itself when it starts: on first launch it asks for `vr.zip`,
checks every ROM by CRC, and copies the zip into the game's user-data folder so it only asks once. `--roms <folder>`,
`M1_ROMS`, or `roms_folder` in `settings.ini` point it at a ROM folder of your own instead. With `roms/vr.zip` in the
repository, `make vr` also assembles development regions under `build/vr/` (with `tools/romset.py`), which the host
uses when run from the repository root. The VR host has settings and saves separate from Virtua Fighter.

You also need the common Model 1 I/O-board ROM: put `model1io.zip` next to `vr.zip`; a picked `vr.zip` brings a
`model1io.zip` beside it along. The steering wheel and both pedals go through that board's analog inputs, so without
it the game starts but can't be steered or accelerated: a stand-in board answers only the digital inputs (shift,
start, coin).

Release packages include no ROMs and need no preparation step or Python. They are produced for macOS
(`VirtuaRacingRecomp-mac.zip`), Windows (`VirtuaRacingRecomp-windows.zip`), and Steam Deck / x86-64 Linux
(`VirtuaRacingRecomp-linux.tar.gz`, which reads `vr.zip` from its `roms` folder, as there is no file picker in Game Mode).
Their app icon (`assets/games/vr/icon`: `icon.png`, `VR.icns`, `VR.ico`) is drawn from scratch, not taken from the
game, by `tools/games/vr/release/make_icon.py`, in the style of Virtua Fighter's.

## Controls and display

Open the menu with `Esc` or View / Select, then use Controls to configure Player 1. On a keyboard, Left/Right
steer, Up accelerates, Down brakes, and X/Z shift up/down. On a gamepad, the defaults are left stick X for
steering, right trigger for accelerator, and left trigger for brake. Digital controls also include Start and Coin.
Press **C** or gamepad **Y / Triangle** to cycle through the four race cameras. Holding the button advances
only once; remap **Cycle camera** under Controls > Player 1 (keyboard or pad buttons).
On the initials screen, hold Left/Right (or the pad's D-pad) to adjust the selection gradually.
Release to keep the selection, then press Accelerator to confirm each initial. Analog-stick steering
remains available, and racing still uses full steering lock for the digital steering keys.

**Game > Service menu** opens the arcade's internal machine configuration and diagnostics. Use `9` to move
and `F2` to select; choose **EXIT** to return to the game.

**Game > Play style** selects **PC** (the default) or **Arcade**, as in Virtua Fighter. PC uses the arcade's
native Free Play setting and waits for Start, even when battery RAM contains credits from an earlier run.
Arcade restores the cabinet coinage and boot screen. Its coin setting is kept as `arcade_coins` in
`settings.ini` while PC mode uses Free Play; rankings and other machine settings are not cleared.
The ROM-backed regression is `build/bin/vr_gameplay_test build/vr --free-play` (or `--free-play-native`).

The Game menu also exposes the arcade's **Car color**, **Course select** (Majority vote / Beginner / Medium /
Expert), and **Difficulty** (Easy / Normal / Hard / Hardest). These edit the same EEPROM settings as the
service menu, not per-race shortcuts. Changes apply together when the menu closes and the game restarts,
and are kept in the existing EEPROM save. What the arcade does with them (from the program ROM, checked
with scripted races):

- **Course select**: Majority vote lets the player choose the course (linked cabinets take the course with
  the most votes). A fixed course is raced every time; the selection screen offers only automatic or manual gears.
- **Difficulty** sets how fast the arcade's automatic difficulty rises. In standalone play the game adds up
  the frames played in each 0x40000 frames it runs (about 76 minutes), giving a busyness of 0-7 that the
  setting maps to a level 0-3 (Easy at most 2). The level sets the starting time, 65/60/55/50 seconds,
  and the checkpoint extensions. A machine that has seen little play races at level 0 on every setting.
  Linked cabinets use the setting as the level directly.

The service menu's **Race mode** (Normal / Grand Prix) is left out of the Game menu: it only changes linked
races (the link ID set), where Grand Prix uses its own time tables with four times the starting time
(260 seconds instead of 65). A standalone race is identical in either mode.

The ROM-backed settings checks (`build/bin/vr_gameplay_test build/vr --settings`, or `--settings-native`)
verify that the arcade reads the chosen values after reboot without restoring its defaults.

The arcade normally ignores Car color for standalone (NO LINK) races. The desktop app extends this setting
to the player's standalone race car by selecting the arcade's existing colored model and texture pointers.
Car identity, starting position and handling are unchanged; linked play and attract-demo colors are left alone.
`build/bin/vr_gameplay_test build/vr --car-color` (or `--car-color-native`) checks all eight actual race liveries.

**Display > Frame rate** offers **30 FPS (Arcade)**, **60 FPS**, and **120 FPS** (the default).
30 disables interpolation and preserves the original arcade motion, which updates at approximately
29 FPS with the board's timing. 60 and 120 interpolate between original pictures, without speeding up
game logic or sound. The setting applies immediately and is saved as `fps` in VR's `settings.ini`.
The screen's refresh rate and available performance limit the visible frame rate.
Arcade presentation follows every two original video ticks rather than an independent 30 Hz timer,
so completed pictures are not discarded between presentation slots.

The VR I/O profile defines digital labels and bit positions, analog channel names, neutral values, and host-axis
defaults. If `model1io.zip` is installed, the headless runner accepts analog values from 0 to 255:

```sh
VR_WHEEL=128 VR_ACCEL=48 VR_BRAKE=48 build/bin/model1run --game vr build/vr 5200
```

The headless tools are shared with VF: `--game vr` (or a regions folder named `vr`) picks the game. The runner
also supports:

- `M1_SCALE=1..12` for render scale
- `M1_ASPECT=1.3333..3.5556` for picture shape
- `M1_AA=0..3` for edge smoothing or supersampling

VR widens active driving scenes and keeps the track-selection screen and camera-less screens at 4:3. It does not
use Virtua Fighter's camera IDs or title/select backdrop rules.

In widened driving scenes, the speed and position displays, lap time, circuit map, and SEGA logo move with
their nearest screen edge. Centered items such as the countdown timer and the car remain centered; full-screen
artwork, attract-mode ranking boards and centered prompts, and non-driving screens are unchanged.
The pre-race pit/grid sequence uses the same HUD anchors as the active race, without repositioning at the start.
The left-hand gauge is grouped separately from the large centered countdown/GO! graphics, so those prompts
do not pull it back into the 4:3 frame.
Attract-mode FREEPLAY/credit notices and the copyright follow the left and right edges respectively,
including the pit-stop sequence. Tile HUD graphics
are grouped before anchoring, and each polygon overlay moves as a whole so letters and shapes are not split.

All wide picture shapes, including **16:9** and **16:10**, use host widening and edge-pinned HUD graphics,
not a stretched frame. Changing picture shape does not require a game restart.
The app sets **MONITOR TYPE = 4:3 NORMAL** at boot, overriding an old **16:9 WIDE** setting with one additional
boot if needed. This prevents the arcade's anamorphic widescreen graphics from being combined with host widening.
The original monitor setting remains available to headless tools for arcade-mode regression tests.
`build/bin/vr_gameplay_test build/vr --monitor[-native]` checks the arcade reads both values after reboot and draws them differently.

The spinning SEGA logo stays in front of close-up cars during attract mode, with either draw distance
and motion setting. `make test-model1-video` checks the shared foreground sorting; with local regions,
`build/bin/model1_video_test build/vr` also checks actual logo visibility and unchanged pixels outside it,
and that smooth motion keeps the logo in place when the attract pit scene hands it from one car record to another,
and moves the GOAL text at the end of a race smoothly as it flies in (`--goal` runs only that check).
The same regression checks animated palms against their own scenery records rather than nearby cars;
add `--motion` to run only the motion checks, including native/interpreter comparison.

At 60/120 FPS, the camera's back-and-forth sway (the attract drive past the bridge, the in-car view) is followed
rather than smoothed, smoothing resumes from the camera's own speed afterwards, and cars the camera follows keep the
game's own screen path while the scenery's turn is smoothed; `--motion` checks that the lead car and the cars around
it never move back and forth there.

At 60/120 FPS, an active car rejected for one update by the arcade's coarse course-cell visibility mask
keeps its last geometry for that update, moved by its record position and camera from that display list.
Visibility and positions are captured while the list is built, not from RAM after the next update.
This presentation-only continuity expires after one update and is cleared at camera cuts; it does not
modify the ROM, visibility masks, display lists, car physics, or CPU work. The 30 FPS arcade mode keeps
the original visibility behaviour.

With extracted regions, `build/bin/model1_video_test build/vr --car-visibility` checks the opening
bridge car at 16:9, both draw distances, and native execution. It checks the missing frame's pixels,
one-update retention, original slot order, and unchanged CPU registers, RAM and display lists.

### Language

Menu > **Game > Language** selects **Japanese** or **English** for the game's own text.
The existing language is preserved until you change it. Close the menu to apply a changed choice;
the game restarts, and the choice is kept in VR's EEPROM save alongside the other arcade settings.
Selecting Reset game also applies a pending language change. Quitting with the menu open saves
the pending choice for the next launch.

VR calls this setting Country: Japanese selects Japan, and English selects Export. An existing U.S.
setting is already English and is left unchanged unless you switch to Japanese. Coin settings,
rankings and other assignments are not reset.

`make test-vr-gameplay` checks both EEPROM representations, checksum coverage, unrelated settings
and existing U.S. settings. With extracted regions, `build/bin/vr_gameplay_test build/vr --language`
checks that both choices survive a game reset; use `--language-native` for recompiled execution.

### CPU timing

VR runs its V60 at an average of 4.5 cycles per instruction (about 3.5 MIPS at 16 MHz) instead of
MAME's estimate of 8, which VF keeps. VR updates its cars in a loop that, at 8 cycles, ran across a
video-frame boundary at a drifting point: whether a car was updated before or after the 3D list was
built changed from picture to picture, so other cars were drawn a step behind and jumped sideways.
At 4.5 cycles every car keeps a steady update phase. CPU speed does not change who is where in the
race (from 8 to 18 cycles the attract race ends the same). The rate is `cpu_cycles_per_instruction`
in the game's profile and applies to interpreted and native runs.

### TGP rounding

The MB86233's mode register has a rounding field, and VR's TGP program selects round-toward-zero at
startup. MAME used it only for float-to-integer conversion and rounded all other float arithmetic to
nearest. The core now runs the TGP's float arithmetic in the selected mode (`make test-mb86233`).

### Attract race

At race start the game keeps some of each car's state from the previous race. Car 0 (the green
car, driven by the player's physics) reads its rank index (record offset +0x123), which only the
second attract race sets (to 15). The boot code clears battery RAM, so on a fresh board the first
medium-course attract race runs differently from every later one. SEGA AGES Virtua Racing shows the
later race from its first attract loop, and so does the port: right after boot, `gameplay.cpp` sets
car 0's rank index to 15. After a played game the attract race inherits the player's state, as on
the cabinet.

### Draw distance

Menu > **Display > Draw distance** offers **Extended** (the default) and **Arcade**. Extended adds static
scenery from all 256 cells of the 16x16 course, with no cell-distance or directional cutoff. Each cell
is 128 units wide. Extra objects are attached to the original scenery matrix as renderer-only metadata,
then drawn with the same camera, lighting and interpolation as the original scenery. Normal view-frustum
clipping still applies. This can substantially lower performance.

Extra scenery bypasses the arcade scenery polygon budget and display-list capacity without raising
the emulated budget or adding CPU commands. The renderer retains its finite geometry capacity and
reports an explicit error rather than writing past its buffers if that capacity is exceeded.

Changes apply when the next scenery list is built, without a restart or recompilation. The choice is saved as
`draw_distance = extended` or `draw_distance = arcade` in VR's own `settings.ini`. Both modes leave
the original ROM, scenery lists, visibility bits, polygon budget, car logic and emulated CPU workload
unchanged. The near-cell lists used for collision and streaming are also unchanged. Arcade submits
only the original geometry. VF does not add any renderer-only scenery.

`make test-vr-gameplay` covers all 256 camera cells, additional static objects, nearest-first ordering,
unchanged RAM and ROM, and the Arcade switch. With extracted regions,
`build/bin/vr_gameplay_test build/vr` compares CPU registers and game RAM as well as actual geometry against Arcade
over 6,000 frames; add `--native` to check recompiled execution without interpreter fallback. Set
`VR_DISTANCE_SHOTS` to an existing directory to save comparison pictures from the matching-camera frame with
the largest polygon increase.

## Native recompilation

The address-only map at `assets/games/vr/vr_map.txt` is generated from scripted coverage and contains no ROM bytes.
`make vr` builds the native host as `build/bin/vr`. The explicitly named executable remains available with:

```sh
make vr-native
```

Run it with `./build/bin/vr-native`; it recompiles the game from the `vr.zip` it loads at launch. With your own
`roms/vr.zip` in the repository, this also assembles the local regions under `build/vr/`, which it uses when run from
the repository root. No ROM data is included in the executable.

`make test-romset` checks the launch-time loader against `tools/romset.py`: every region built from your local
`roms/vf.zip` and `roms/vr.zip` must match the Python tool's byte for byte.

Run the strict interpreter/native comparison across all three courses, view-button inputs, test-menu
entry/exit, and a qualifying Beginner finish with ranking-name entry:

```sh
make vr-native-lockstep
```

The checks compare CPU registers, RAM, and rendered frames, require no interpreter fallbacks, and assert progress
through track selection, the starting grid, active racing, and course results. Course selection is checked
against the Beginner, Medium, or Expert label. The Beginner and view-button scenarios also check the
second-credit race and the return to attract rankings after game over. All six scenarios run for 26,000 frames.
The `qualifying` scenario selects EASY difficulty through the test menu, drives the full five-lap Beginner race
using only wheel, pedal and shift inputs, then selects and confirms `SDK` with the wheel and accelerator.
It checks the qualifying-finish state, results, each confirmed initial, the first-place ranking record's name
and finishing time in battery RAM, and the return to attract rankings. No race-state or ROM fixture is used.
The feedback driver reads the car state and road geometry; lockstep derives one input packet from the
interpreter and applies it unchanged to both machines. These are scripted checks, not exhaustive release
validation: qualifying finishes on Medium/Expert and at the default difficulty remain outside this coverage.

The focused ROM-backed test also checks that the ranking survives a reset and 1,000 boot frames:

```sh
make build/bin/vr_gameplay_test
build/bin/vr_gameplay_test build/vr --qualifying
build/bin/vr_gameplay_test build/vr --qualifying-native
```

The checkpoints read the game's state-machine byte at `0x501138`: track selection (`0x10`), starting grid
(`0x12`), racing (`0x14`), qualifying finish (`0x19`), results/game over (`0x1c`), and ranking-name entry
(`0x1e`). Results are accepted only after racing; attract
rankings (`0x03`) cannot satisfy that checkpoint. This avoids the unrelated changing mode byte, course-dependent
result cameras, and polygon counts that depend on draw distance.

To regenerate coverage (`build/cov/vr/`) and the candidate map (`build/gen/vr_map.txt`), run `make vr-static`;
generated artifacts stay in `build/`. `make vr-lockstep` checks the generated C++ against the interpreter. The
scenarios and frame counts are in `src/games/vr/game.mk` (`make vr-lockstep vr_LOCK_FRAMES=...` overrides one). Set
`M1_TGP_TRACE=1` with the headless runner to report whether the TGP ROM-backed core or HLE is active and how many
MB86233 cycles ran.

The scenarios are `default` (Beginner), `alternate` (Expert), `intermediate` (Medium), `views` (Beginner with
all four view buttons), `service` (test-menu entry and EXIT), and `qualifying` (EASY Beginner finish and `SDK`
ranking entry). For example, pass `--scenario intermediate`
to a headless tool to select Medium. Regenerate coverage after changing a script; the coverage targets depend
on the rebuilt tool. Promote the generated address map to `assets/games/vr/vr_map.txt` only after strict native
lockstep passes with it.

To check the shared ADC implementation, run `make test-model1io-adc`. With the optional I/O-board ROM, compare
analog controls using two runs:

```sh
mkdir -p build/vr_idle build/vr_full
M1_ASPECT=1.7778 M1_SCALE=2 M1_AA=2 M1_SCRIPT=1 VR_WHEEL=128 VR_ACCEL=48 VR_BRAKE=48 build/bin/model1run --game vr build/vr 5200 500 build/vr_idle
M1_ASPECT=1.7778 M1_SCALE=2 M1_AA=2 M1_SCRIPT=1 VR_WHEEL=128 VR_ACCEL=255 VR_BRAKE=48 build/bin/model1run --game vr build/vr 5200 500 build/vr_full
```

Compare `f05000.png` in each directory, then repeat while changing only `VR_WHEEL` or `VR_BRAKE`. These analog
checks are interpreter-only and require `model1io.zip`.
