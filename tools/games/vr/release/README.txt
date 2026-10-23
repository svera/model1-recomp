VIRTUA RACING RECOMP (preview)
==============================

An unofficial fan project, not made, approved or supported by SEGA.
No game files are included. You need your own MAME "vr" set (vr.zip) and the Model 1 I/O
board's program (model1io.zip).

FIRST LAUNCH
Start Virtua Racing Recomp. It asks for your vr.zip, checks it and copies it into its user-data
folder, so you only do this once. Put model1io.zip next to your vr.zip first: it is copied too.
The wheel and pedals go through the I/O board, so without model1io.zip the game starts but you
can't steer or accelerate.

On Steam Deck / Linux, put vr.zip and model1io.zip in the package's roms
folder instead: Game Mode has no file picker. Then add VirtuaRacing.sh as a Non-Steam Game and set
Start In to the extracted VirtuaRacing folder.

Keep your ROMs in a folder of your own? Add  roms_folder = <your folder>  to settings.ini, or
start the game with  --roms "<your folder>".

The macOS preview is not notarized; if Gatekeeper blocks it, Control-click the app, choose Open,
then confirm. Windows may show SmartScreen for the same reason.

CONTROLS
Open the menu with Esc or View/Select, then Controls > Player 1. Keyboard: Left/Right steer,
Up accelerates, Down brakes, X/Z shift up/down, C cycles the four cameras. Gamepad: left stick X
steers, right trigger accelerates, left trigger brakes, A / Cross shifts up, B / Circle
shifts down, Y / Triangle cycles cameras, and D-pad Up inserts a coin in Arcade mode.
The same bindings can be changed there; the game starts with
the configured Start control.
For manual transmission, hold Start on the course selection screen while confirming with Accelerator.

Display settings, antialiasing, sound, and saves are available from the menu. All wide picture
shapes, including 16:9 and 16:10, widen the view without stretching the HUD. Off-center HUD
elements follow the screen edges. Picture shape changes do not restart the game; the app
keeps the arcade's MONITOR TYPE at 4:3 NORMAL.
Display > LOD defaults to Off for full-detail models at every distance. Turn it On for the
arcade's simpler distant models and better performance. Draw distance controls scenery visibility.
Virtua Racing recompiles your ROM's V60 program to native code at startup. Use --interp to run on the
Model 1 interpreter instead.

CREDITS
Built on MAME's work on the Model 1 (the V60 CPU, TGP, sound and video), Musashi, ymfm, sljit,
SDL, RmlUi and FreeType: see LICENSES.txt.
