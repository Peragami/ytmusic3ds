// icons.h - the app's icon set: Google's Material icons (the same
// icon family the YouTube Music app uses), converted offline to
// 32x32 alpha masks (icons_data.h) and uploaded at startup as one
// GPU texture per colour the UI uses (v3.8: baked colours instead
// of image tinting, which rendered solid squares).
#ifndef ICONS_H
#define ICONS_H

#include <citro2d.h>

#include "icons_data.h"

// Build the GPU textures. Call after C2D_Init.
void icons_init(void);

// Draw one icon with its top-left corner at (x, y), drawn
// size x size pixels, tinted to `color` (a C2D_Color32 value).
void draw_icon(int id, float x, float y, float size, u32 color);

#endif // ICONS_H
