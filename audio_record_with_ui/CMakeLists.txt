SET(SOURCES ui_Screen1.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_events.cpp
    ui_img_microphone_png.c
    ui_img_stop_red_png.c)

add_library(ui ${SOURCES})
