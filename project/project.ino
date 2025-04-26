#include <Arduino.h>
#include <esp_task_wdt.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc_cal.h"
#include <SPI.h>
#include "pin_config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>

// Forecast temp for Karlskrona
//const String apiURL = "https://opendata-download-metfcst.smhi.se/api/category/pmp3g/version/2/geotype/point/lon/15.6885/lat/56.199/data.json";

const String cities[5] = {"Karlskrona", "Stockholm", "Goteborg", "Malmo", "Kalmar"};
const String latitudes[5] = {"56.1990", "59.3327", "57.7089", "55.6050", "56.6634"};
const String longitudes[5] = {"15.6885", "18.0656", "11.9746", "13.0038", "16.3568"};
const String stations[5] = {"65090", "97400", "71420", "52350", "66420"};

int cityIndex = 0;
void menu();
void settings();
String buildApiURL();
String buildHistoricUrl();
void selectCity(int index);
void drawSymbols(int symbolType, int x, int y);
// Remember to remove these before commiting in GitHub
String ssid = "-";
String password = "-";

// "tft" is the graphics libary, which has functions to draw on the screen
TFT_eSPI tft = TFT_eSPI();

// Display dimentions
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 170

WiFiClient wifi_client;

/**
 * Setup function
 * This function is called once when the program starts to initialize the program
 * and set up the hardware.
 * Carefull when modifying this function.
 */
void setup() {
  // Initialize Serial for debugging
  Serial.begin(115200);
  // Wait for the Serial port to be ready
  while (!Serial);
  Serial.println("Starting ESP32 program...");
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  pinMode(PIN_BUTTON_1, INPUT_PULLUP);
  pinMode(PIN_BUTTON_2, INPUT_PULLUP);

  // Connect to WIFI
  WiFi.begin(ssid, password);

  // Will be stuck here until a proper wifi is configured
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Connecting to WiFi...", 10, 10);
    Serial.println("Attempting to connect to WiFi...");
  }

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Connected to WiFi", 10, 10);
  Serial.println("Connected to WiFi");
  // Add your code bellow 
  configTzTime("CET-1CEST,M3.5.0/02,M10.5.0/03", "pool.ntp.org");

}
int symbols[24];
String temperatureLabels[24];
int scrollIndex = 0;
const int entriesPerPage = 6;
/**
 * This is the main loop function that runs continuously after setup.
 * Add your code here to perform tasks repeatedly.
 */
void loop() {
  StaticJsonDocument<64> filter;
  filter["list"][0]["t"] = true;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Verion: 1.0.0", 10, 10);
  tft.drawString("Group 10", 10, 40);
  
  delay(3000);
  menu();
}

/**
 * Starting Menu
 */
void menu() {
  scrollIndex = 0;
  bool menuActive = true;

  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Start Screen", 10, 10);

  // Buttons
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.fillRect(250, 0, 70, 25, TFT_WHITE);
  tft.drawString("Settings", 260, 10);
  tft.fillRect(250, 145, 70, 25, TFT_WHITE);
  tft.drawString("Down", 260, 155);

  fetchTemperature();
  drawTemperaturePage();

  delay(500);  // Initial debounce

  while (menuActive) {
    checkForMenuShortcut();  
    int downButton = digitalRead(PIN_BUTTON_1);
    int settingsButton = digitalRead(PIN_BUTTON_2);

    if (settingsButton == LOW) {
      delay(200);  // Debounce
      settings(0);
      return;
    }

    if (downButton == LOW) {
      scrollIndex += entriesPerPage;
      if (scrollIndex >= 24) {
        scrollIndex = 0;  // Loop back to start
      }

      drawTemperaturePage();
      delay(200);  // Debounce
    }
  }
}

/**
 * Settings screen. Lets the user scroll through their different options and select one of them.
 */
void settings(int selectedOption){
  tft.fillScreen(TFT_BLACK);
  int min = 0;
  int max = 2;
  String options[max+1] = {"Exit Settings", "Select City", "Historic Data"};
  tft.setTextSize(1);
  tft.drawString("Settings", 10, 10);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.fillRect(250, 0, 70, 25, TFT_WHITE);
  tft.drawString("Select", 260, 10);
  tft.fillRect(250, 145, 70, 25, TFT_WHITE);
  tft.drawString("Down", 260, 155);
  tft.setTextSize(3);
  if (selectedOption == min){
    String exit = "Exit Settings";
    tft.drawString(options[selectedOption], 40, 80);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(options[selectedOption+1], 40, 110);
    tft.drawString(options[max], 40, 60);
  } else if (selectedOption == max){
    tft.drawString(options[selectedOption], 40, 80);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString(options[min], 40, 110);
    tft.drawString(options[selectedOption-1], 40, 60);
  } else {
    tft.drawString(options[selectedOption], 40, 80);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString(options[selectedOption+1], 40, 110);
    tft.drawString(options[selectedOption-1], 40, 60);
  }
  delay(500);
  while(true){
    checkForMenuShortcut();  
    int downButton = digitalRead(PIN_BUTTON_1);
    int selectButton = digitalRead(PIN_BUTTON_2);
    if (downButton == 0){
      if (selectedOption == max){
        settings(0);
      } else {
        settings(selectedOption + 1);
      }
    } else if (selectButton == 0){
      if (selectedOption == 0){
        menu();
      } else if (selectedOption == 1){
        selectCity(0);
      } 
      else if (selectedOption == 2) {  // Historic Data
        viewHistoricData();
      }
    }
  }
}

/**
 * Not finished.
 */
void selectCity(int index){
  tft.fillScreen(TFT_BLACK);
  int min = 0;
  int max = 4;
  tft.setTextSize(1);
  tft.drawString("City Selection", 10, 10);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.fillRect(250, 0, 70, 25, TFT_WHITE);
  tft.drawString("Select", 260, 10);
  tft.fillRect(250, 145, 70, 25, TFT_WHITE);
  tft.drawString("Down", 260, 155);
  tft.setTextSize(3);
  if (index == min){
    tft.drawString(cities[index], 40, 80);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(cities[index + 1], 40, 110);
    tft.drawString(cities[max], 40, 60);
  } else if (index == max){
    tft.drawString(cities[index], 40, 80);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString(cities[min], 40, 110);
    tft.drawString(cities[index - 1], 40, 60);
  } else {
    tft.drawString(cities[index], 40, 80);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString(cities[index + 1], 40, 110);
    tft.drawString(cities[index - 1], 40, 60);
  }
  delay(500);
  while(true){
    checkForMenuShortcut();  
    int downButton = digitalRead(PIN_BUTTON_1);
    int selectButton = digitalRead(PIN_BUTTON_2);
    if (downButton == 0){
      if (index == max){
        selectCity(0);
      } else {
        selectCity(index + 1);
      }
    } else if (selectButton == 0){
      cityIndex = index;
      menu();
    }
  }
}

//Retrives temeperature and time data for a location
void fetchTemperature() {
  HTTPClient http;
  http.useHTTP10(true);
  http.begin(buildApiURL());
  http.GET();
  JsonDocument doc;
  deserializeJson(doc, http.getStream());
  JsonArray timeSeries = doc["timeSeries"];
  if (!timeSeries.isNull() && timeSeries.size() > 0) {
    for (int i = 0; i < 24 && i < timeSeries.size(); i++) {
      JsonObject ts = timeSeries[i];
      JsonArray parameters = ts["parameters"];
      
      for (JsonObject param : parameters) {
        const char* name = param["name"];
        if (strcmp(name, "t") == 0) {
          float tempVal = param["values"][0];
          String temperature = String(tempVal, 2);  // 2 decimal places
          temperatureLabels[i] = String(i) + "h: " + temperature + "C";
        }
        if (strcmp(name, "Wsymb2") == 0) {
          symbols[i] = param["values"][0];
          break;
        }
      }
    }
  } else {
    Serial.println("No timeSeries data available.");
  }
  http.end();
}

void drawTemperaturePage() {
  tft.fillRect(0, 40, 240, 130, TFT_BLACK);  // Clear display area
  tft.setTextSize(2);
  for (int i = 0; i < entriesPerPage; i++) {
    int idx = scrollIndex + i;
    if (idx < 24) {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(temperatureLabels[idx], 10, 40 + i * 20);
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      drawSymbol(symbols[idx], 210, 45 + i * 20);
    }
  }
}

void drawSymbol(int symbolType, int x, int y){
  if (symbolType <= 2){ //Clearsky
    tft.fillCircle(x, y, 4, TFT_ORANGE);
  } else if (symbolType <= 4){ //Halfclear
    tft.fillCircle(x, y, 4, TFT_ORANGE);
    tft.fillCircle(x, y+5, 3, TFT_LIGHTGREY);
    tft.fillCircle(x+4, y+1, 3, TFT_LIGHTGREY);
    tft.fillCircle(x-3, y+2, 2, TFT_LIGHTGREY);
  } else if (symbolType <= 6){ //Cloudy
    tft.fillCircle(x, y, 3, TFT_LIGHTGREY);
    tft.fillCircle(x, y+5, 3, TFT_LIGHTGREY);
    tft.fillCircle(x+4, y+1, 3, TFT_LIGHTGREY);
    tft.fillCircle(x-3, y+2, 2, TFT_LIGHTGREY);
  } else if (symbolType == 7){ //Fog
    tft.drawString("~", x, y);
    tft.drawString("~", x+3, y+3);
    tft.drawString("~", x-2, y-2);
  } else if (symbolType <= 10 || symbolType > 17){ //Rain
    tft.fillCircle(x, y+5, 4, TFT_BLUE);
    tft.fillTriangle(x-4, y+5, x+4, y+5, x, y - 7, TFT_BLUE);
  } else if (symbolType == 11 || symbolType == 21) { //Thunder
    tft.fillTriangle(x-1, y-5, x+1, y-5, x+1, y, TFT_YELLOW);
    tft.fillTriangle(x+1, y-2, x+3, y-2, x+1, y+4, TFT_YELLOW);
  } else { //Snow or hail
    tft.drawCircle(x, y, 3, TFT_SKYBLUE);
    tft.drawLine(x -5 ,y,x +5 ,y,TFT_SKYBLUE);
    tft.drawLine(x ,y -5 ,x ,y +5,TFT_SKYBLUE);
    tft.drawLine(x-4 ,y -4 ,x+4 ,y +4,TFT_SKYBLUE);
    tft.drawLine(x-4 ,y +4 ,x+4 ,y -4,TFT_SKYBLUE);
  } 
}

void checkForMenuShortcut() {
  if (digitalRead(PIN_BUTTON_1) == LOW && digitalRead(PIN_BUTTON_2) == LOW) {
    delay(200);  // Small debounce
    menu();
  }
}


void viewHistoricData() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Display City name
  tft.drawString("City: " + cities[cityIndex], 10, 10);
  tft.drawString("Historic Data", 10, 25);

  // Get the current date
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    tft.drawString("Date: Unknown", 10, 40);
  } else {
    char currentDate[11];
    strftime(currentDate, sizeof(currentDate), "%Y-%m-%d", &timeinfo);
    tft.drawString("Date: " + String(currentDate), 10, 40);
  }

  HTTPClient http;
  http.useHTTP10(true);
  http.begin(buildHistoricUrl());
  int httpCode = http.GET();

  if (httpCode > 0) {
    JsonDocument doc;
    deserializeJson(doc, http.getStream());
    JsonArray dataArray = doc["value"];
    
    if (dataArray.size() > 0) {
      // Now list some temperatures
      tft.setTextSize(2);
      int entriesToShow = 5;
      int startIdx = dataArray.size() - 1;
      int endIdx = startIdx - entriesToShow + 1;
      if (endIdx < 0) endIdx = 0;

      for (int i = startIdx; i >= endIdx; i--) {
        JsonObject item = dataArray[i];
        String date = item["ref"].as<String>();
        float temp = item["value"];

        String displayText = date.substring(5, 10) + ": " + String(temp, 1) + "C"; // MM-DD
        tft.drawString(displayText, 10, 60 + (startIdx - i) * 20);
      }
    } else {
      tft.drawString("No historical data", 10, 60);
    }
  } else {
    tft.drawString("Failed to fetch data", 10, 60);
  }
  http.end();

  // Wait for user to press button to go back
  while (true) {
    checkForMenuShortcut();
    if (digitalRead(PIN_BUTTON_2) == LOW) {  // Button 2 to exit
      delay(200);
      settings(0);
      return;
    }
  }
}


String buildHistoricUrl(){
  return "https://opendata-download-metobs.smhi.se/api/version/latest/parameter/20/station/"
  + stations[cityIndex]
  + "/period/latest-months/data.json";
}
  
String buildApiURL() {
  return "https://opendata-download-metfcst.smhi.se/api/category/pmp3g/version/2/geotype/point/lon/"
  + longitudes[cityIndex] 
  + "/lat/" 
  + latitudes[cityIndex]
  + "/data.json";
}

// TFT Pin check
  //////////////////
 // DO NOT TOUCH //
//////////////////
#if PIN_LCD_WR  != TFT_WR || \
    PIN_LCD_RD  != TFT_RD || \
    PIN_LCD_CS    != TFT_CS   || \
    PIN_LCD_DC    != TFT_DC   || \
    PIN_LCD_RES   != TFT_RST  || \
    PIN_LCD_D0   != TFT_D0  || \
    PIN_LCD_D1   != TFT_D1  || \
    PIN_LCD_D2   != TFT_D2  || \
    PIN_LCD_D3   != TFT_D3  || \
    PIN_LCD_D4   != TFT_D4  || \
    PIN_LCD_D5   != TFT_D5  || \
    PIN_LCD_D6   != TFT_D6  || \
    PIN_LCD_D7   != TFT_D7  || \
    PIN_LCD_BL   != TFT_BL  || \
    TFT_BACKLIGHT_ON   != HIGH  || \
    170   != TFT_WIDTH  || \
    320   != TFT_HEIGHT
#error  "Error! Please make sure <User_Setups/Setup206_LilyGo_T_Display_S3.h> is selected in <TFT_eSPI/User_Setup_Select.h>"
#error  "Error! Please make sure <User_Setups/Setup206_LilyGo_T_Display_S3.h> is selected in <TFT_eSPI/User_Setup_Select.h>"
#error  "Error! Please make sure <User_Setups/Setup206_LilyGo_T_Display_S3.h> is selected in <TFT_eSPI/User_Setup_Select.h>"
#error  "Error! Please make sure <User_Setups/Setup206_LilyGo_T_Display_S3.h> is selected in <TFT_eSPI/User_Setup_Select.h>"
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
#error  "The current version is not supported for the time being, please use a version below Arduino ESP32 3.0"
#endif
