/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2012-2017 Kevin Lange
 *
 * Client-side Window Decoration library
 */

#include <stdint.h>
#include <math.h>
#include "lib/graphics.h"
#include "lib/yutani.h"
#include "lib/shmemfonts.h"
#include "lib/dlfcn.h"

#include "decorations.h"

uint32_t decor_top_height     = 33;
uint32_t decor_bottom_height  = 6;
uint32_t decor_left_width     = 6;
uint32_t decor_right_width    = 6;

#define TEXT_OFFSET_X 10
#define TEXT_OFFSET_Y 16

#define BORDERCOLOR rgb(60,60,60)
#define BORDERCOLOR_INACTIVE rgb(30,30,30)
#define BACKCOLOR rgb(20,20,20)
#define TEXTCOLOR rgb(230,230,230)
#define TEXTCOLOR_INACTIVE rgb(140,140,140)

void (*decor_render_decorations)(yutani_window_t *, gfx_context_t *, char *, int) = NULL;
int  (*decor_check_button_press)(yutani_window_t *, int x, int y) = NULL;

static void (*callback_close)(yutani_window_t *) = NULL;
static void (*callback_resize)(yutani_window_t *) = NULL;

static int close_enough(struct yutani_msg_window_mouse_event * me) {
	return (me->command == YUTANI_MOUSE_EVENT_RAISE &&
			sqrt(pow(me->new_x - me->old_x, 2.0) + pow(me->new_y - me->old_y, 2.0)) < 10.0);
}

static void render_decorations_simple(yutani_window_t * window, gfx_context_t * ctx, char * title, int decors_active) {

	uint32_t color = BORDERCOLOR;
	if (decors_active == DECOR_INACTIVE) {
		color = BORDERCOLOR_INACTIVE;
	}

	for (uint32_t i = 0; i < window->height; ++i) {
		GFX(ctx, 0, i) = color;
		GFX(ctx, window->width - 1, i) = color;
	}

	for (uint32_t i = 1; i < decor_top_height; ++i) {
		for (uint32_t j = 1; j < window->width - 1; ++j) {
			GFX(ctx, j, i) = BACKCOLOR;
		}
	}

	if (decors_active == DECOR_INACTIVE) {
		draw_string(ctx, TEXT_OFFSET_X, TEXT_OFFSET_Y, TEXTCOLOR_INACTIVE, title);
		draw_string(ctx, window->width - 20, TEXT_OFFSET_Y, TEXTCOLOR_INACTIVE, "✕");
	} else {
		draw_string(ctx, TEXT_OFFSET_X, TEXT_OFFSET_Y, TEXTCOLOR, title);
		draw_string(ctx, window->width - 20, TEXT_OFFSET_Y, TEXTCOLOR, "✕");
	}

	for (uint32_t i = 0; i < window->width; ++i) {
		GFX(ctx, i, 0) = color;
		GFX(ctx, i, decor_top_height - 1) = color;
		GFX(ctx, i, window->height - 1) = color;
	}
}

static int check_button_press_simple(yutani_window_t * window, int x, int y) {
	if (x >= window->width - 20 && x <= window->width - 2 && y >= 2) {
		return DECOR_CLOSE;
	}

	return 0;
}

static void initialize_simple() {
	decor_top_height     = 24;
	decor_bottom_height  = 1;
	decor_left_width     = 1;
	decor_right_width    = 1;

	decor_render_decorations = render_decorations_simple;
	decor_check_button_press = check_button_press_simple;
}

void render_decorations(yutani_window_t * window, gfx_context_t * ctx, char * title) {
	if (!window) return;
	if (!window->focused) {
		decor_render_decorations(window, ctx, title, DECOR_INACTIVE);
	} else {
		decor_render_decorations(window, ctx, title, DECOR_ACTIVE);
	}
}

void render_decorations_inactive(yutani_window_t * window, gfx_context_t * ctx, char * title) {
	if (!window) return;
	decor_render_decorations(window, ctx, title, DECOR_INACTIVE);
}

void init_decorations() {
	init_shmemfonts();

	char * tmp = getenv("WM_THEME");
	char * theme = tmp ? strdup(tmp) : NULL;

	if (!theme || !strcmp(theme, "simple")) {
		initialize_simple();
	} else {
		char * options = strchr(theme,',');
		if (options) {
			*options = '\0';
			options++;
		}
		char lib_name[100];
		sprintf(lib_name, "libtoaru-decor-%s.so", theme);
		void * theme_lib = dlopen(lib_name, 0);
		if (!theme_lib) {
			goto _theme_error;
		}
		void (*theme_init)(char *) = dlsym(theme_lib, "decor_init");
		if (!theme_init) {
			goto _theme_error;
		}
		theme_init(options);
		return;

_theme_error:
			fprintf(stderr, "decorations: could not load theme `%s`: %s\n", theme, dlerror());
			initialize_simple();
	}
}

uint32_t decor_width() {
	return decor_left_width + decor_right_width;
}

uint32_t decor_height() {
	return decor_top_height + decor_bottom_height;
}

void decor_set_close_callback(void (*callback)(yutani_window_t *)) {
	callback_close = callback;
}

void decor_set_resize_callback(void (*callback)(yutani_window_t *)) {
	callback_resize = callback;
}

static int within_decors(yutani_window_t * window, int x, int y) {
	if ((x <= decor_left_width || x >= window->width - decor_right_width) && (x > 0 && x < window->width)) return 1;
	if ((y <= decor_top_height || y >= window->height - decor_bottom_height) && (y > 0 && y < window->height)) return 1;
	return 0;
}

#define LEFT_SIDE (me->new_x <= decor_left_width)
#define RIGHT_SIDE (me->new_x >= window->width - decor_right_width)
#define TOP_SIDE (me->new_y <= decor_top_height)
#define BOTTOM_SIDE (me->new_y >= window->height - decor_bottom_height)

static yutani_scale_direction_t check_resize_direction(struct yutani_msg_window_mouse_event * me, yutani_window_t * window) {
	yutani_scale_direction_t resize_direction = SCALE_NONE;
	if (LEFT_SIDE && !TOP_SIDE && !BOTTOM_SIDE) {
		resize_direction = SCALE_LEFT;
	} else if (RIGHT_SIDE && !TOP_SIDE && !BOTTOM_SIDE) {
		resize_direction = SCALE_RIGHT;
	} else if (BOTTOM_SIDE && !LEFT_SIDE && !RIGHT_SIDE) {
		resize_direction = SCALE_DOWN;
	} else if (BOTTOM_SIDE && LEFT_SIDE) {
		resize_direction = SCALE_DOWN_LEFT;
	} else if (BOTTOM_SIDE && RIGHT_SIDE) {
		resize_direction = SCALE_DOWN_RIGHT;
	} else if (TOP_SIDE && LEFT_SIDE) {
		resize_direction = SCALE_UP_LEFT;
	} else if (TOP_SIDE && RIGHT_SIDE) {
		resize_direction = SCALE_UP_RIGHT;
	} else if (TOP_SIDE && (me->new_y < 5)) {
		resize_direction = SCALE_UP;
	}
	return resize_direction;
}

static int old_resize_direction = SCALE_NONE;

int decor_handle_event(yutani_t * yctx, yutani_msg_t * m) {
	if (m) {
		switch (m->type) {
			case YUTANI_MSG_WINDOW_MOUSE_EVENT:
				{
					struct yutani_msg_window_mouse_event * me = (void*)m->data;
					yutani_window_t * window = hashmap_get(yctx->windows, (void*)me->wid);
					if (!window) return 0;
					if (within_decors(window, me->new_x, me->new_y)) {
						int button = decor_check_button_press(window, me->new_x, me->new_y);
						if (me->command == YUTANI_MOUSE_EVENT_DOWN && me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
							if (!button) {
								/* Resize edges */
								yutani_scale_direction_t resize_direction = check_resize_direction(me, window);

								if (resize_direction != SCALE_NONE) {
									yutani_window_resize_start(yctx, window, resize_direction);
								}

								if (me->new_y < decor_top_height && resize_direction == SCALE_NONE) {
									yutani_window_drag_start(yctx, window);
								}
								return DECOR_OTHER;
							}
						}
						if (me->command == YUTANI_MOUSE_EVENT_MOVE) {
							if (!button) {
								/* Resize edges */
								yutani_scale_direction_t resize_direction = check_resize_direction(me, window);
								if (resize_direction != old_resize_direction) {
									if (resize_direction == SCALE_NONE) {
										yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESET);
									} else {
										switch (resize_direction) {
											case SCALE_UP:
											case SCALE_DOWN:
												yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESIZE_VERTICAL);
												break;
											case SCALE_LEFT:
											case SCALE_RIGHT:
												yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESIZE_HORIZONTAL);
												break;
											case SCALE_DOWN_RIGHT:
											case SCALE_UP_LEFT:
												yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESIZE_UP_DOWN);
												break;
											case SCALE_DOWN_LEFT:
											case SCALE_UP_RIGHT:
												yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESIZE_DOWN_UP);
												break;
										}
									}
									old_resize_direction = resize_direction;
								}
							}
						}
						if (me->command == YUTANI_MOUSE_EVENT_CLICK || close_enough(me)) {
							/* Determine if we clicked on a button */
							switch (button) {
								case DECOR_CLOSE:
									if (callback_close) callback_close(window);
									break;
								case DECOR_RESIZE:
									if (callback_resize) callback_resize(window);
									break;
								default:
									break;
							}
							return button;
						}
					} else {
						if (old_resize_direction != SCALE_NONE) {
							yutani_window_show_mouse(yctx, window, YUTANI_CURSOR_TYPE_RESET);
							old_resize_direction = 0;
						}
					}
				}
				break;
		}
	}
	return 0;
}
