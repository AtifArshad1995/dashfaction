#pragma once

#include "../rf/bmpman.h"

namespace rf
{
    struct Object;
}

void graphics_init();
void graphics_after_game_init();
void graphics_draw_fps_counter();
int get_default_font_id();
void set_default_font_id(int font_id);
bool gr_render_to_texture(int bm_handle);
void gr_render_to_back_buffer();
void gr_delete_texture(int bm_handle);

template<typename F>
void run_with_default_font(int font_id, F fun)
{
    int old_font = get_default_font_id();
    set_default_font_id(font_id);
    fun();
    set_default_font_id(old_font);
}
