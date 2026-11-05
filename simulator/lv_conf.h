#ifndef LV_CONF_H
#define LV_CONF_H

/* Desktop preview configuration.
 * LV_COLOR_DEPTH MUST match the board: rtconfig.h sets LV_COLOR_DEPTH 16 and the
 * RGB panel runs in RGB565. Rendering the preview in 32 bpp hides the colour
 * banding that the real panel shows in gradients, so do not raise this value. */
#define LV_COLOR_DEPTH 16
#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH <SDL.h>
#define LV_MEM_SIZE (1024 * 1024U)
#define LV_FONT_MONTSERRAT_16 1

#endif
