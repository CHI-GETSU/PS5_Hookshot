# Button-prompt faces: reworking their images

Every image a face shows in the game is made at the build, from the user's RomFS and Kenney's Input Prompts
(`third_party/kenney_input`), by `nx2/prompt_assets.py`. Nothing from the game is kept in git. To change an image, either change
what it is made from, or draw it by hand and put it here.

## What is made, and from what

| Image | What it is | Made from |
| --- | --- | --- |
| `OcarinaButton<A B X Y L R>_00.<pad input>.png` | the ocarina's notes: the note of that Switch button when it is on that pad input | the face's icon for the pad input (`icons` in `nx2/prompt_faces.py`, a glyph of the face's Kenney font) |
| `KeyTexFont<A B X Y>_00.<pad input>.png` | the white letters on black (the Talk ring's small buttons) | the icon's letter (its holes), or its shape when it has none |
| `Controller_Pro.png` | the controller in the pause menu's controls page (System) | the face's `drawing` in `nx2/prompt_faces.py`, drawn by `nx2/prompt_draw.py` |
| `Controller_Pro1.png`, `ControllerSh_Pro1.png` | the page's tinted copy of the controller, and its shadow | `Controller_Pro.png` (yours if you made one) |
| `ControllerGuide_Pro_0<0 1>.png` | the page's leader lines that stay, whatever the controls | the face's `parts` in `nx2/prompt_faces.py` (where each control is in the picture) and the router in `prompt_assets.py` (`GUIDE_*`) |
| `ControllerGuide_Pro_0<0 1>.<Switch button>.<pad input>.png` | the line of that Switch button's label when it is on that pad input, drawn over the lines that stay | the same |

The text icons (dialogue, the control hints, the menus) are not images: they are the face's font, the game's
`External.bfotf` with the face's Kenney glyphs, picked by `icons` in `nx2/prompt_faces.py`.

The Switch face keeps the game's own images for its default controls; only the other bindings get images.

Pad inputs: `south east west north back guide start lstick rstick lshoulder rshoulder up down left right ltrigger rtrigger`.
Switch buttons: `a b x y ls rs l r zl zr plus minus left up right down`.

## Drawing one by hand

1. Write out what the build makes (and the game's own textures, to draw over):

       python -I nx2/prompt_assets.py --romfs <romfs> --out /tmp/prompts --dump /tmp/prompts-png

   `/tmp/prompts-png/<face>/` has each image under the name above, `/tmp/prompts-png/game/` the game's texture each goes into.

2. Edit the image (keep its size and its mode: RGBA for the notes and the controller, greyscale for the letters, the
   lines and the shadow), and save it as `nx2/prompt_faces/<face>/<the same name>`.

3. Build the game (or only the prompts, as in step 1 without `--dump`, into `<game folder>/prompts`): your image takes the
   place of the made one. A controller picture of yours also makes the tinted copy and the shadow, unless you give those too.

An image of the wrong size stops the build with its name. A leader line of yours is written only where it has any of
itself: draw it on black, the lines that stay are kept under it.

When a face's picture changes, its `parts` must follow it (the lines end on them): the pictures' pixels, `(x, y, r)` for each
control, see `_parts` in `nx2/prompt_faces.py`.
