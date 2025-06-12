# Waveshare ESP32-C6-LCD-1.47 Internet Clock with Weather Monitoring Project

## Description:
This code displays time, weather data, and system information on the Waveshare ESP32-C6 1.47" LCD. The project combines an internet clock & weather monitoring using Squareline Studio to create the UI interface with a "The Promised Neverland" theme.

## How it works:
- Initialization: Sets up display, LVGL, and WiFi connection
- Time Synchronization: Gets current time from NTP server at startup and every 2 hours
- Weather Updates: Fetches current temperature, humidity & UV Index from OpenWeatherMap One Call API every 10min
- Count down/up timers: 10-minute OWM count-up timer displayed from 0 to 600 & NTP count-dowm timer from 7200 to 0
- Wi-Fi Monitoring: Regularly checks and displays connection quality
- LED Synchronization: Matches NeoPixel LED color with on-screen R2D2-LED panel

## Notes:
- Requires OpenWeatherMap One Call account (requires credit card but free for up to 1000 calls per day). This code makes an API call every 10min (144 calls per day - well under the free limit).
- Edit configuration section for:
  - Wi-Fi credentials
  - OWM API key
  - Physical location
  - Choice of metric/imperial
  - Timezone offset (GMT + or -)
- City names can use normal spaces (automatically URL encoded)
- Backlight brightness is set to 75% by default (can be changed in setup)
- Had to edit line 4 to `#include "lv_conf.h"` and comment out line 5 in LVGL_Driver.h
- Added JSON file handling functionality to the SD_Card helpers
- Exposed NeoPixel RGB values in NeoPixel helpers & inverted RGB channels to match colours (not used in this project)

## SLS Project Files:
- This repository includes the SquareLine Studio project files in: **'.\Waveshare-ESP32-C6-LCD-1.47-PlasmaBall-Internet_Clock\sls_files'**
- In the **'sls_files'** folder, there are 2 subfolders: **'export'** & **'project'**
- Open SquareLine_Project in the **'project'** folder with Squareline Studio to make changes to the UI.
- You may need to update the SLS project settings **Project Export Root** & **UI Files Export Path** locations to reflect where you have saved the Arduino project **before exporting**.
- Export project files to the **'export'** folder & copy all, then replace all files in the **root** of the Arduino project folder.
- **Do not export into the root Arduino project folder as SLS will erase the folder contents before exporting!**
- **NB!!** Every time a change is made in SLS, & the UI files have been replaced in the Arduino project folder - you **must** edit the ui.h file on line 30: **FROM** `#include "screens/ui_MainScreen.h"` **TO** `#include "ui_MainScreen.h"` (remove 'screens/' -- I'm not sure why SLS is exporting this way as the flat export option is selected.)
