SET(SOURCES ui_Start_Screen.c
    ui_MainScreen.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_img_255117540.c
    ui_img_thermometer_png.c
    ui_img_humidity_png.c
    ui_img_1554230089.c
    fonts/ui_font_Witka10.c
    fonts/ui_font_Witka14.c
    fonts/ui_font_Witka16.c)

add_library(ui ${SOURCES})
