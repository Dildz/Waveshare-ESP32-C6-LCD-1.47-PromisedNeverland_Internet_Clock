/*********************************************************************************************************
* Waveshare ESP32-C6-LCD-1.47 Internet Clock with Weather Monitoring Project
*
* Description:
* This code displays time, weather data, and system information on the Waveshare ESP32-C6 1.47" LCD.
* The project combines an internet clock & weather monitoring in a compact LVGL interface using
* Squareline Studio with a "The Promised Neverland" theme.
*
* Key Features:
*  - Accurate timekeeping with NTP synchronization (every 2 hours)
*  - Geo, temperature, humidity & UV index data updates from OpenWeatherMap (updated every 10 minutes)
*  - JSON files on the SD Card used to store Geo & Weather data
*  - Colour-coded weather values based on current conditions (temperature & UV index values)
*  - Date display in DD/MM/YY format
*  - LVGL-based graphical interface with FPS counter
*  - Wi-Fi status monitoring with colour-coded signal strength (RSSI)
*  - IP address display with segmented digits
*  - Random 'character ID' number generation every minute
*  - System timers using millis showing count down/up to next NTP/OWM updates
*  - NeoPixel LED rainbow effect
*
* How It Works:
*  1. Time Synchronization: Gets current time via NTP and maintains with ESP32 RTC
*  2. Weather Updates:
      - First fetches Lat/Lon co-ordinates using the city name OWM Geocoding API,
      - Uses stored co-ordinates to fetch current conditions from OWM One Call API
*  3. UI Updates: Continuously refreshes time, weather, and other information
*  4. System Monitoring: Tracks Wi-Fi status, FPS, and maintains update counters
*
* Notes:
*  - Requires OpenWeatherMap One Call account (requires credit card but free for up to 1000 calls per day).
*    This code makes an API call every 10min, which means 144 calls per day - well under the free limit.
*  - Edit configuration section for your Wi-Fi credentials, OWM API key, physical location, choice of
*    metric/imperial, and timezone offset
*  - City names can use normal spaces (automatically URL encoded)
*  - Backlight brightness is set to 75% by default (can be changed in setup)
*  - Had to edit line 4 to [#include "lv_conf.h"] and comment out line 5 in LVGL_Driver.h
*  - Added JSON file handling functionality to the SD_Card helpers
*  - Exposed NeoPixel RGB values in NeoPixel helpers & inverted RGB channels to match colours (not used)
*  - SLS export path will need to get updated in the project settings
*  - When exporting in SLS & replacing project files, you NEED to edit the ui.h file on line 30:
*      #include "screens/ui_MainScreen.h"
*      >TO<
*      #include "ui_MainScreen.h"
*      (Remove 'screens/' - not sure why SLS is exporting this way as flat export option is selected.)
*
**********************************************************************************************************/

/*************************************************************
******************* INCLUDES & DEFINITIONS *******************
**************************************************************/

#include <ESP32Time.h> // https://github.com/fbiego/ESP32Time
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "Display_ST7789.h"
#include "LVGL_Driver.h"
#include "NeoPixel.h"
#include "SD_Card.h"
#include "ui.h"

#define BOOT_BTN 9 // (may do something with this later)

// Time configuration
ESP32Time rtc(0);
const char* ntpServer = "pool.ntp.org";

/******************************
*** >> EDIT THIS SECTION << ***
*******************************/
const char* ssid = "MSI-GF75";                           // replace with your Wi-Fi SSID name
const char* password = "Uitenhage1";                     // replace with your Wi-Fi password
const char* owmAPI = "0f9cae9b97bc31029c22c4c227cfb2d6"; // replace with your OWM API key
const char* city = "Port Elizabeth";                     // replace with your city/town
const char* countryCode = "ZA";                          // replace with your country code
bool useMetric = true;                                   // true for metric (°C), false for imperial (°F)
int tzOffset = 2;                                        // replace with your time zone offset (mine is GMT +2)

// Global Variables
unsigned long lastWiFiUpdate = 0;
const int wifiUpdateInterval = 5000; // 5 seconds
String ipAddress;

const char* const geoDataPath = "/geo_data.json";
const char* const owmDataPath = "/owm_data.json";

const char* geoURL = "http://api.openweathermap.org/geo/1.0/direct?q=%s,%s&limit=1&appid=%s";
const char* oneCallURL = "https://api.openweathermap.org/data/3.0/onecall?lat=%f&lon=%f&exclude=minutely,hourly,daily&units=%s&appid=%s";

float temp = 0;
float humi = 0;
float uvIndex = 0;

int ntpSyncInterval = 7200;        // NTP sync interval (2 hours in seconds)
int owmSyncInterval = 600;         // OWM update interval (10 minutes in seconds)
int ntpCounter = ntpSyncInterval;  // starts at ntpSyncInterval and counts down to 0
int owmCounter = 0;                // starts at 0 and counts up to owmSyncInterval

unsigned long lastFPSTime = 0;
unsigned int frameCount = 0;
int currentFPS = 0;
unsigned long lastRefreshTime = 0;

int lastSecond = 0;
unsigned long lastSecondUpdate = 0;
int currentSeconds = 0;


/*************************************************************
********************** HELPER FUNCTIONS **********************
**************************************************************/

// Function to connect to Wi-Fi
bool connectWifi() {
  WiFi.setHostname("ESP32-C6-LCD-1.47");
  WiFi.begin(ssid, password);
  
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 10) { // 10 second timeout
    delay(1000);
    timeout++;
  }
  return (WiFi.status() == WL_CONNECTED); // return connection status
}

// Function to set time
void setTime() {
  configTime(3600 * tzOffset, 0, ntpServer);
  struct tm timeinfo;

  if (getLocalTime(&timeinfo)) {
    rtc.setTimeStruct(timeinfo); 
  }
}

// Function to get geographic coordinates
bool geoData() {
  JsonDocument doc;
  
  // Try to load existing geo data
  if(readJSONFile(geoDataPath, doc)) {
    return true;
  }
  
  // URL encode any spaces in city names
  String encodedCity = String(city);
  encodedCity.replace(" ", "%20");
  
  char url[256];
  snprintf(url, sizeof(url), geoURL, encodedCity.c_str(), countryCode, owmAPI);
  
  HTTPClient http;
  http.begin(url);
  http.setTimeout(10000); // 10 second timeout
  http.addHeader("User-Agent", "ESP32-Weather-Display");
  
  int httpCode = http.GET();
  
  if(httpCode <= 0) {
    http.end();
    return false;
  }
  
  if(httpCode != HTTP_CODE_OK) {
    String payload = http.getString();
    http.end();
    return false;
  }
  
  String payload = http.getString();
  http.end();
  
  // Parse response
  JsonDocument geoResponseDoc;
  DeserializationError error = deserializeJson(geoResponseDoc, payload);
  
  if(error) {
    return false;
  }
  
  if(geoResponseDoc.size() == 0) {
    return false;
  }
  
  // Create geo data structure
  JsonObject geo = geoResponseDoc[0];
  doc["lat"] = geo["lat"];
  doc["lon"] = geo["lon"];
  doc["city"] = city;  // store original city name
  doc["country"] = countryCode;
  
  // Save to SD card
  if(!createJSONFile(geoDataPath, doc)) {
    return false;
  }
  return true;
}

// Function to update weather data from OpenWeatherMap
bool updateOWM() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  // Try to load geo data from SD card
  JsonDocument geoDoc;
  if (!readJSONFile(geoDataPath, geoDoc)) {
    return false;
  }

  // Check if we have valid coordinates
  if (!geoDoc["lat"].is<float>() || !geoDoc["lon"].is<float>()) {
    return false;
  }

  float lat = geoDoc["lat"];
  float lon = geoDoc["lon"];

  // Build the API URL
  char url[256];
  const char* units = useMetric ? "metric" : "imperial";
  snprintf(url, sizeof(url), oneCallURL, lat, lon, units, owmAPI);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(10000); // 10 second timeout
  http.addHeader("User-Agent", "ESP32-Weather-Display");

  int httpCode = http.GET();

  if (httpCode <= 0) {
    http.end();
    return false;
  }

  if (httpCode != HTTP_CODE_OK) {
    String payload = http.getString();
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  // Parse the response
  JsonDocument owmDoc;
  DeserializationError error = deserializeJson(owmDoc, payload);
  if (error) {
    return false;
  }

  // Extract weather data and update global variables
  temp = owmDoc["current"]["temp"];
  humi = owmDoc["current"]["humidity"];
  uvIndex = owmDoc["current"]["uvi"];

  // Add timestamp to know when data was last updated
  owmDoc["last_update"] = millis();

  // Save to SD card
  if (!writeJSONFile(owmDataPath, owmDoc)) {
    return false;
  }
  updateWeather();
  return true;
}

// Function to update weather variables
void updateWeather() {
  // Update temperature label value (truncate decimal)
  int tempInt = (int)temp;
  _ui_label_set_property(ui_Temp, _UI_LABEL_PROPERTY_TEXT, String(tempInt).c_str());
 
  // Temperature label colours (blue at 5°C or less to red at 35°C or more)
  lv_color_t tempColour;
  const lv_opa_t alpha = 225;
 
  if (tempInt <= 5) {
    // Blue (cold)
    tempColour = lv_color_mix(lv_color_hex(0x0000FF), lv_color_white(), alpha);
  }
  else if (tempInt <= 18) {
    // Blue to green transition
    uint8_t ratio = map(tempInt, 5, 18, 0, 255);
    tempColour = lv_color_mix(lv_color_hex(0x0000FF), lv_color_hex(0x00FF00), ratio);
    tempColour = lv_color_mix(tempColour, lv_color_white(), alpha);
  }
  else if (tempInt <= 25) {
    // Green to yellow transition
    uint8_t ratio = map(tempInt, 18, 25, 0, 255);
    tempColour = lv_color_mix(lv_color_hex(0x00FF00), lv_color_hex(0xFFFF00), ratio);
    tempColour = lv_color_mix(tempColour, lv_color_white(), alpha);
  }
  else if (tempInt <= 30) {
    // Yellow to orange transition
    uint8_t ratio = map(tempInt, 25, 30, 0, 255);
    tempColour = lv_color_mix(lv_color_hex(0xFFFF00), lv_color_hex(0xFFA500), ratio);
    tempColour = lv_color_mix(tempColour, lv_color_white(), alpha);
  }
  else if (tempInt <= 35) {
    // Orange to red transition
    uint8_t ratio = map(tempInt, 30, 35, 0, 255);
    tempColour = lv_color_mix(lv_color_hex(0xFFA500), lv_color_hex(0xFF0000), ratio);
    tempColour = lv_color_mix(tempColour, lv_color_white(), alpha);
  }
  else {
    // Red (hot)
    tempColour = lv_color_mix(lv_color_hex(0xFF0000), lv_color_white(), alpha);
  }

  // Set colour of temperature label
  lv_obj_set_style_text_color(ui_Temp, tempColour, LV_STATE_DEFAULT);

  // Update humidity label value (truncate decimal)
  int humiInt = (int)humi;
  _ui_label_set_property(ui_Humidity, _UI_LABEL_PROPERTY_TEXT, String(humiInt).c_str());
 
  // Set colour of humidity label
  lv_color_t humiColour = lv_color_mix(lv_color_white(), lv_color_white(), alpha);
  lv_obj_set_style_text_color(ui_Humidity, humiColour, LV_STATE_DEFAULT);

  // Update UV Index label value (truncate decimal and add leading zero for values < 10)
  int uvInt = (int)uvIndex;
  char uvStr[3]; // space for 2 digits + null terminator
  snprintf(uvStr, sizeof(uvStr), "%02d", uvInt); // format with leading zero for single-digit values
  _ui_label_set_property(ui_UVIndex, _UI_LABEL_PROPERTY_TEXT, uvStr);

  // UV Index label colours
  lv_color_t uvColour;
  
  if (uvInt <= 2) {
    // Green (low)
    uvColour = lv_color_mix(lv_color_hex(0x00FF00), lv_color_white(), alpha);
  }
  else if (uvInt <= 5) {
    // Yellow (moderate)
    uvColour = lv_color_mix(lv_color_hex(0xFFFF00), lv_color_white(), alpha);
  }
  else if (uvInt <= 7) {
    // Orange (high)
    uvColour = lv_color_mix(lv_color_hex(0xFFA500), lv_color_white(), alpha);
  }
  else if (uvInt <= 10) {
    // Red (very high)
    uvColour = lv_color_mix(lv_color_hex(0xFF0000), lv_color_white(), alpha);
  }
  else {
    // Violet (extreme)
    uvColour = lv_color_mix(lv_color_hex(0xEE82EE), lv_color_white(), alpha);
  }
  // Set colour of UV Index label
  lv_obj_set_style_text_color(ui_UVIndex, uvColour, LV_STATE_DEFAULT);
}

// Function to update Wi-Fi quality indicator
void updateWiFiQuality() {
  int rssi = WiFi.RSSI();
  lv_style_selector_t selector = LV_STATE_DEFAULT;
  const lv_opa_t alpha = 200; // set alpha to 200/255

  // Create colours with alpha
  lv_color_t red = lv_color_make(0xFF, 0x00, 0x00);
  lv_color_t green = lv_color_make(0x00, 0xFF, 0x00);
  lv_color_t yellow = lv_color_make(0xFF, 0xFF, 0x00);

  if (WiFi.status() != WL_CONNECTED) {
    // Not connected (all bars red with alpha)
    lv_obj_set_style_bg_color(ui_PanelWiFiBar1, lv_color_mix(red, lv_color_white(), alpha), selector);
    lv_obj_set_style_bg_color(ui_PanelWiFiBar2, lv_color_mix(red, lv_color_white(), alpha), selector);
    lv_obj_set_style_bg_color(ui_PanelWiFiBar3, lv_color_mix(red, lv_color_white(), alpha), selector);
    return;
  }

  // Excellent signal (all bars green with alpha)
  if (rssi >= -50) {
    lv_obj_set_style_bg_color(ui_PanelWiFiBar1, lv_color_mix(green, lv_color_white(), alpha), selector);
    lv_obj_set_style_bg_color(ui_PanelWiFiBar2, lv_color_mix(green, lv_color_white(), alpha), selector);
    lv_obj_set_style_bg_color(ui_PanelWiFiBar3, lv_color_mix(green, lv_color_white(), alpha), selector);
  }
  // Good signal (two bars green, one yellow with alpha)
  else if (rssi >= -60) {
    lv_obj_set_style_bg_color(ui_PanelWiFiBar1, lv_color_mix(green, lv_color_white(), alpha), selector);
    lv_obj_set_style_bg_color(ui_PanelWiFiBar2, lv_color_mix(green, lv_color_white(), alpha), selector);
    lv_obj_set_style_bg_color(ui_PanelWiFiBar3, lv_color_mix(yellow, lv_color_white(), alpha), selector);
  }
  // Fair signal (one bar green, two yellow with alpha)
  else if (rssi >= -70) {
    lv_obj_set_style_bg_color(ui_PanelWiFiBar1, lv_color_mix(green, lv_color_white(), alpha), selector);
    lv_obj_set_style_bg_color(ui_PanelWiFiBar2, lv_color_mix(yellow, lv_color_white(), alpha), selector);
    lv_obj_set_style_bg_color(ui_PanelWiFiBar3, lv_color_mix(yellow, lv_color_white(), alpha), selector);
  }
  // Bad signal (all bars yellow with alpha)
  else if (rssi >= -80) {
    lv_obj_set_style_bg_color(ui_PanelWiFiBar1, lv_color_mix(yellow, lv_color_white(), alpha), selector);
    lv_obj_set_style_bg_color(ui_PanelWiFiBar2, lv_color_mix(yellow, lv_color_white(), alpha), selector);
    lv_obj_set_style_bg_color(ui_PanelWiFiBar3, lv_color_mix(yellow, lv_color_white(), alpha), selector);
  }
  // Poor signal (two bars yellow, one red with alpha)
  else {
    lv_obj_set_style_bg_color(ui_PanelWiFiBar1, lv_color_mix(yellow, lv_color_white(), alpha), selector);
    lv_obj_set_style_bg_color(ui_PanelWiFiBar2, lv_color_mix(yellow, lv_color_white(), alpha), selector);
    lv_obj_set_style_bg_color(ui_PanelWiFiBar3, lv_color_mix(red, lv_color_white(), alpha), selector);
  }
}

// Function to update IP address segments
void updateIPSegments() {
  IPAddress ip = WiFi.localIP();
  
  // Format each segment to 3 digits with leading zeros
  char seg1[4], seg2[4], seg3[4], seg4[4];
  snprintf(seg1, sizeof(seg1), "%03d", ip[0]);
  snprintf(seg2, sizeof(seg2), "%03d", ip[1]);
  snprintf(seg3, sizeof(seg3), "%03d", ip[2]);
  snprintf(seg4, sizeof(seg4), "%03d", ip[3]);
  
  _ui_label_set_property(ui_IPSeg1, _UI_LABEL_PROPERTY_TEXT, seg1);
  _ui_label_set_property(ui_IPSeg2, _UI_LABEL_PROPERTY_TEXT, seg2);
  _ui_label_set_property(ui_IPSeg3, _UI_LABEL_PROPERTY_TEXT, seg3);
  _ui_label_set_property(ui_IPSeg4, _UI_LABEL_PROPERTY_TEXT, seg4);
}

// Function to update seconds display using millis()
void updateSeconds() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastSecondUpdate >= 1000) {
    currentSeconds = (currentSeconds + 1) % 60;
    
    // Format seconds with leading zero
    char secStr[3];
    snprintf(secStr, sizeof(secStr), "%02d", currentSeconds);
    _ui_label_set_property(ui_Sec, _UI_LABEL_PROPERTY_TEXT, secStr);
    
    lastSecondUpdate = currentMillis;
  }
}

// Function to generate random numbers
void updateRandomNumbers(bool forceUpdate = false) {
  static unsigned long lastUpdate = 0;
  
  // Update every minute or when forced
  if (forceUpdate || (millis() - lastUpdate >= 60000)) {
    // Generate 3-digit random number (111-999)
    int num1 = random(111, 1000);
    _ui_label_set_property(ui_IDNumber1, _UI_LABEL_PROPERTY_TEXT, String(num1).c_str());
    
    // Generate 2-digit random number (11-99)
    int num2 = random(11, 100);
    _ui_label_set_property(ui_IDNumber2, _UI_LABEL_PROPERTY_TEXT, String(num2).c_str());
    
    // Force UI update if called from setup
    if (forceUpdate) {
      Timer_Loop();
      lv_refr_now(NULL);
    }
    
    lastUpdate = millis();
  }
}

// Function to update date display
void updateDate() {
  // Get the current time structure
  struct tm timeinfo = rtc.getTimeStruct();
  
  // Format the date as DD/MM/YY
  char dateStr[9]; // DD/MM/YY + null terminator
  snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%02d", timeinfo.tm_mday, 
            timeinfo.tm_mon + 1,     // tm_mon is 0-11
            timeinfo.tm_year % 100); // get last 2 digits of year
  
  _ui_label_set_property(ui_Date, _UI_LABEL_PROPERTY_TEXT, dateStr);
}

// Function to update timer displays
void updateTimers() {
  // Update NTP timer (counts down)
  char ntpStr[5];
  snprintf(ntpStr, sizeof(ntpStr), "%04d", ntpCounter);
  _ui_label_set_property(ui_NTPTimer, _UI_LABEL_PROPERTY_TEXT, ntpStr);
  
  // Update OWM timer (counts up)
  char owmStr[4];
  snprintf(owmStr, sizeof(owmStr), "%03d", owmCounter);
  _ui_label_set_property(ui_OWMTimer, _UI_LABEL_PROPERTY_TEXT, owmStr);
}


/*************************************************************
*********************** MAIN FUNCTIONS ***********************
**************************************************************/

// SETUP
void setup() {
  // Initialize hardware
  Flash_test();
  LCD_Init();
  SD_Init();

  Lvgl_Init();
  Set_Backlight(75);      // brightness 75/100
  NeoPixel_Init(128);     // brightness 128/255
  Set_Color(200, 200, 2); // black background

  pinMode(BOOT_BTN, INPUT);
  delay(500);

  // Initialize UI
  ui_init();

  // Start by hiding all text on startup screen
  lv_obj_add_flag(ui_Line0, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Line1, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Line2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Line2err, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Line3, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Line4, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_IPAdd, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Line5, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Subnet, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Line6, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Line7, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Line8, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Line9, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Line9err, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Line10, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Line11, LV_OBJ_FLAG_HIDDEN);

  // Verify SD card is mounted
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    lv_obj_clear_flag(ui_Line0, LV_OBJ_FLAG_HIDDEN); // show error message
    Timer_Loop(); // UI update

    // Halt with blinking red LED
    while(1) {
      Set_Color(0, 255, 0);
      delay(500);
      Set_Color(0, 0, 0);
      delay(500);
    }
    
  }

  // Remove any existing JSON files
  if (SD.exists(geoDataPath)) {
    SD.remove(geoDataPath);
  }
  
  if (SD.exists(owmDataPath)) {
    SD.remove(owmDataPath);
  }

  // Show connecting to Wi-Fi message
  lv_obj_clear_flag(ui_Line1, LV_OBJ_FLAG_HIDDEN);
  Timer_Loop(); // UI update

  // Connect to Wi-Fi
  bool wifiConnected = connectWifi();

  // If connection timed out / failed
  if (!wifiConnected) {
    lv_obj_clear_flag(ui_Line2err, LV_OBJ_FLAG_HIDDEN); // show error message
    Timer_Loop(); // UI update
    
    // Halt with blinking red LED
    while(1) {
      Set_Color(0, 255, 0);
      delay(500);
      Set_Color(0, 0, 0);
      delay(500);
    }
  }

  // Get network info
  IPAddress ip = WiFi.localIP();
  IPAddress subnet = WiFi.subnetMask();
  
  // Show Wi-Fi connected message
  lv_obj_clear_flag(ui_Line2, LV_OBJ_FLAG_HIDDEN);
  Timer_Loop(); // UI update
  delay(1000);
  
  // Set IP & Subnet labels
  _ui_label_set_property(ui_IPAdd, _UI_LABEL_PROPERTY_TEXT, ip.toString().c_str());
  _ui_label_set_property(ui_Subnet, _UI_LABEL_PROPERTY_TEXT, subnet.toString().c_str());

  // Show AP connection info
  lv_obj_clear_flag(ui_Line3, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_Line4, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_IPAdd, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_Line5, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_Subnet, LV_OBJ_FLAG_HIDDEN);
  Timer_Loop(); // UI update
  delay(1000);

  // Show time sync message
  lv_obj_clear_flag(ui_Line6, LV_OBJ_FLAG_HIDDEN);
  Timer_Loop(); // UI update

  setTime();
  delay(1000);
  
  // Show time synced message
  lv_obj_clear_flag(ui_Line7, LV_OBJ_FLAG_HIDDEN);
  Timer_Loop();
  delay(1000);
  
  // Show weather fetch message
  lv_obj_clear_flag(ui_Line8, LV_OBJ_FLAG_HIDDEN);
  Timer_Loop();
  
  // Make OWM geo request
  if(!geoData()) {
    lv_obj_clear_flag(ui_Line9err, LV_OBJ_FLAG_HIDDEN); // show error message
    Timer_Loop(); // UI update
    
    // Halt with blinking red LED
    while(1) {
      Set_Color(0, 255, 0);
      delay(500);
      Set_Color(0, 0, 0);
      delay(500);
    }
  }
  delay(500);

  // Make OWM weather request
  updateOWM();
  delay(500);
  
  // Show weather received message
  lv_obj_clear_flag(ui_Line9, LV_OBJ_FLAG_HIDDEN);
  Timer_Loop();
  delay(1000);

  // Show loading assets message
  lv_obj_clear_flag(ui_Line10, LV_OBJ_FLAG_HIDDEN);
  Timer_Loop();
  delay(1000);

  // Initialize random seed
  randomSeed(analogRead(0)); // use unconnected GPIO0 floating value

  // Initialize displays
  updateWiFiQuality();
  updateDate();
  updateTimers();
  updateIPSegments();
  updateRandomNumbers(true);
  
  // Show starting main display message
  lv_obj_clear_flag(ui_Line11, LV_OBJ_FLAG_HIDDEN);
  Timer_Loop();
  delay(1000);

  // Switch to main screen
  _ui_screen_change(&ui_MainScreen, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_MainScreen_screen_init);
  Timer_Loop();
  delay(500);
}

// MAIN LOOP
void loop() {
  static unsigned long lastLoopTime = millis();
  unsigned long currentTime = millis();
  unsigned long elapsedTime = currentTime - lastLoopTime;
  lastLoopTime = currentTime;

  Timer_Loop(); // call lv_timer_handler & 16ms delay via LVGL Driver (changed from 5ms)

  // Count this as a frame
  frameCount++;
  
  // Calculate FPS
  if (currentTime - lastFPSTime >= 1000) {
    currentFPS = frameCount;
    frameCount = 0;
    lastFPSTime = currentTime;
    
    static int lastDisplayedFPS = 0;
    if (currentFPS != lastDisplayedFPS) {
      _ui_label_set_property(ui_FPS, _UI_LABEL_PROPERTY_TEXT, String(currentFPS).c_str());
      lastDisplayedFPS = currentFPS;
    }
  }

  // Updates using elapsedTime (to account for the LVGL_Driver frame delay)
  static unsigned long timeAccumulator = 0;
  timeAccumulator += elapsedTime;

  // Update all time/date related UI variables once per second
  if (timeAccumulator >= 1000) {
    timeAccumulator -= 1000;
    
    lastSecond = rtc.getSecond();
    _ui_label_set_property(ui_Time, _UI_LABEL_PROPERTY_TEXT, rtc.getTime().substring(0,5).c_str());
    _ui_label_set_property(ui_Sec, _UI_LABEL_PROPERTY_TEXT, rtc.getTime().substring(6,8).c_str());
    updateDate();
    
    // Update counters
    ntpCounter--;
    owmCounter++;

    if (ntpCounter <= 0) {
      setTime();
      ntpCounter = ntpSyncInterval;
    }

    if (owmCounter >= owmSyncInterval) {
      updateOWM();
      owmCounter = 0;
    }
    
    updateTimers();
  }

  // Wi-Fi and IP updates every 5seconds (using accumulated time)
  static unsigned long wifiAccumulator = 0;
  wifiAccumulator += elapsedTime;
  if (wifiAccumulator >= wifiUpdateInterval) {
    wifiAccumulator = 0;
    updateWiFiQuality();
    
    // Only update IP if it's changed (Wi-Fi reconnected)
    String newIP = WiFi.localIP().toString();
    if (newIP != ipAddress) {
      ipAddress = newIP;
      updateIPSegments();
    }
  }
  
  // Update random numbers (using accumulated time)
  static unsigned long randomAccumulator = 0;
  randomAccumulator += elapsedTime;
  if (randomAccumulator >= 60000) { // every minute
    randomAccumulator = 0;
    updateRandomNumbers();
  }

  // Handle the NeoPixel LED
  NeoPixel_Loop(3);
}
