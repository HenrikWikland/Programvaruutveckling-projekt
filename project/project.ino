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
#include <EEPROM.h>

#define EEPROM_SIZE 10



// Forecast temp for Karlskrona
//const String apiURL = "https://opendata-download-metfcst.smhi.se/api/category/pmp3g/version/2/geotype/point/lon/15.6885/lat/56.199/data.json";

volatile bool menuRequested = false;
bool skipIntro = false;  

const int DAYS_TO_SHOW = 7;
const int MAX_DAYS = 120;

float fullTemps[MAX_DAYS];
String fullDates[MAX_DAYS];
int fullCount = 0;

const String cities[5] = {"Karlskrona", "Stockholm", "Goteborg", "Malmo", "Kalmar"};
const String latitudes[5] = {"56.1990", "59.3327", "57.7089", "55.6050", "56.6634"};
const String longitudes[5] = {"15.6885", "18.0656", "11.9746", "13.0038", "16.3568"};
const String stations[5] = {"65090", "97400", "71420", "52350", "66420"};
const String parameters[3] = {"20", "6", "4"};

int selectedWeatherParameter = 0; // Default to first parameter
const char *weatherParameters[] = {"Temperature", "Humidity", "Wind Speed"};
int numWeatherParameters = sizeof(weatherParameters) / sizeof(weatherParameters[0]);
int previouslySelectedWeatherParameter = -1;  


const int DEFAULT_CITY_INDEX = 0;
const int DEFAULT_SELECTED_PARAMETER = 0;

int cityIndex = 0;
void menu();
void settings();
String buildApiURL();
String buildHistoricUrl();
void selectCity(int index);
void drawSymbols(int symbolType, int x, int y);
// Remember to remove these before commiting in GitHub
String ssid = "Asus";
String password = "Wasabi10";

// "tft" is the graphics libary, which has functions to draw on the screen
TFT_eSPI tft = TFT_eSPI();

// Display dimentions
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 170

WiFiClient wifi_client;

void saveSettings() {
  EEPROM.write(0, cityIndex);                   // Save at address 0
  EEPROM.write(1, selectedWeatherParameter);    // Save at address 1
  EEPROM.commit();
}

void loadSettings() {
  cityIndex = EEPROM.read(0);
  selectedWeatherParameter = EEPROM.read(1);
}

/**
 * Setup function
 * This function is called once when the program starts to initialize the program
 * and set up the hardware.
 * Carefull when modifying this function.
 */
void setup() {
  EEPROM.begin(EEPROM_SIZE);
  loadSettings();
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
  unsigned long startTime = millis();
  const unsigned long splashDuration = 3000;

  // Wait during splash period, but allow shortcut to interrupt
  while (millis() - startTime < splashDuration) {
    checkForMenuShortcut();
    if (menuRequested) {
      skipIntro = true;
      break;  // Exit early if shortcut pressed
    }
    delay(10);  // Prevent watchdog reset
  }

  if (!skipIntro) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("Version: 1.0.0", 10, 10);
    tft.drawString("Group 10", 10, 40);
    delay(1000);  // Shorter delay after showing
  }

  menuRequested = false;
  skipIntro = false;
  menu();  // Go to menu always
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

void resetSettingsToDefault() {
  cityIndex = DEFAULT_CITY_INDEX;
  selectedWeatherParameter = DEFAULT_SELECTED_PARAMETER;
  saveSettings();

  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Settings reset", 60, 100);
  delay(1500);
}

/**
 * Settings screen. Lets the user scroll through their different options and select one of them.
 */
void settings(int selectedOption) {
  tft.fillScreen(TFT_BLACK);
  int min = 0;
  int max = 4;  // Now 5 options (Exit, Select City, Historic Data, Select Parameter, Reset)
  String options[max + 1] = {
    "Exit Settings",
    "Select City",
    "Historic Data",
    "Select Parameter",
    "Reset Settings"
  };

  tft.setTextSize(1);
  tft.drawString("Settings", 10, 10);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.fillRect(250, 0, 70, 25, TFT_WHITE);
  tft.drawString("Select", 260, 10);
  tft.fillRect(250, 145, 70, 25, TFT_WHITE);
  tft.drawString("Down", 260, 155);

  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  if (selectedOption == min) {
    tft.drawString(options[selectedOption], 40, 80);
    tft.setTextSize(2);
    tft.drawString(options[selectedOption + 1], 40, 110);
    tft.drawString(options[max], 40, 60);
  } else if (selectedOption == max) {
    tft.drawString(options[selectedOption], 40, 80);
    tft.setTextSize(2);
    tft.drawString(options[min], 40, 110);
    tft.drawString(options[selectedOption - 1], 40, 60);
  } else {
    tft.drawString(options[selectedOption], 40, 80);
    tft.setTextSize(2);
    tft.drawString(options[selectedOption + 1], 40, 110);
    tft.drawString(options[selectedOption - 1], 40, 60);
  }

  delay(500);

  while (true) {
    checkForMenuShortcut();
    if (menuRequested) {
      menuRequested = false;
      return;
    }

    int downButton = digitalRead(PIN_BUTTON_1);
    int selectButton = digitalRead(PIN_BUTTON_2);

    if (downButton == LOW) {
      selectedOption = (selectedOption + 1) % (max + 1); // Cycle through options
      break; // Break out of the while loop to re-render the screen
    } else if (selectButton == LOW) {
      switch (selectedOption) {
        case 0: menu(); return;
        case 1: selectCity(0); break;
        case 2: viewHistoricData(); break;
        case 3: selectWeatherParameter(selectedWeatherParameter); break;
        case 4: resetSettingsToDefault(); settings(0); break;
        break;
      }
    }
  }

  // Re-render the settings screen with the updated selectedOption value
  settings(selectedOption); // This ensures that the new option is displayed without recursion.
}

/**
 * Not finished.
 */
void selectCity(int index) {
  tft.fillScreen(TFT_BLACK);
  int min = 0;
  int max = 4; // Assuming there are 5 cities in total

  // Ensure index is within bounds
  if (index < min) index = min;
  if (index > max) index = max;

  tft.setTextSize(1);
  tft.drawString("City Selection", 10, 10);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.fillRect(250, 0, 70, 25, TFT_WHITE);
  tft.drawString("Select", 260, 10);
  tft.fillRect(250, 145, 70, 25, TFT_WHITE);
  tft.drawString("Down", 260, 155);
  tft.setTextSize(3);

  // Handle different index cases to ensure no out-of-bounds access
  if (index == min) {
    tft.drawString(cities[index], 40, 80);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(cities[index + 1], 40, 110);
    tft.drawString(cities[max], 40, 60);
  } else if (index == max) {
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
  delay(500);  // Debounce delay

  while (true) {
    checkForMenuShortcut();
    if (menuRequested) {
      menuRequested = false;
      return;  // Exit current view, main loop will call menu()
    }

    int downButton = digitalRead(PIN_BUTTON_1);
    int selectButton = digitalRead(PIN_BUTTON_2);

    if (downButton == LOW) {  // When DOWN button is pressed
      if (index == max) {
        index = min;  // Wrap around to the first city
      } else {
        index++;  // Move to the next city
      }
      // Redraw the city selection screen
      tft.fillScreen(TFT_BLACK);  // Clear previous screen
      selectCity(index);  // Redraw with updated index
      return;  // Exit the loop to re-render the city selection
    } else if (selectButton == LOW) {  // When SELECT button is pressed
      cityIndex = index;
      saveSettings();  // Save the selected city
      menu();  // Exit and return to the menu
      return;
    }
  }
}

//Retrives temeperature and time data for a location
void fetchTemperature() {
  // Clear previous labels and symbols
  for (int i = 0; i < 24; i++) {
    temperatureLabels[i] = "";   // Reset label strings
    symbols[i] = 0;              // Clear symbols (assuming uint8_t or int)
  }

  HTTPClient http;
  http.useHTTP10(true);
  http.begin(buildApiURL());
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.println("HTTP GET failed, code: " + String(httpCode));
    http.end();
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, http.getStream());
  if (error) {
    Serial.println("JSON deserialization failed: " + String(error.c_str()));
    http.end();
    return;
  }

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
        } else if (strcmp(name, "Wsymb2") == 0) {
          symbols[i] = param["values"][0];  // assuming this is an int or byte
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
    delay(200);  // Debounce
    menuRequested = true;
  }
}


void drawTemperatureGraph(float temps[], String dates[], int count, String unit) {
  int graphX = 10, graphY = 55;  // Graph position
  int graphWidth = 220, graphHeight = 100;

  // Determine min and max
  float minTemp = temps[0], maxTemp = temps[0];
  for (int i = 1; i < count; i++) {
    if (temps[i] < minTemp) minTemp = temps[i];
    if (temps[i] > maxTemp) maxTemp = temps[i];
  }
  if (maxTemp == minTemp) maxTemp += 1;  // Avoid divide-by-zero

  // Axes
  tft.drawRect(graphX, graphY, graphWidth, graphHeight, TFT_WHITE);

  // Plot data points and lines
  int prevX = -1, prevY = -1;
  for (int i = 0; i < count; i++) {
    int x = graphX + (i * (graphWidth / (count - 1)));
    int y = graphY + graphHeight - ((temps[i] - minTemp) / (maxTemp - minTemp)) * graphHeight;

    tft.fillCircle(x, y, 2, TFT_RED);
    if (i > 0) tft.drawLine(prevX, prevY, x, y, TFT_GREEN);

    prevX = x;
    prevY = y;

    // Date labels (MM-DD)
    if (unit == "C"){
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(dates[i].substring(5), x - 10, graphY + graphHeight + 5);
    }
    else{
      tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(dates[i], x - 10, graphY + graphHeight + 5);
    }
  }

  // Min/max labels with correct unit
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(String(maxTemp, 1) + " " + unit, graphX + graphWidth + 5, graphY);
  tft.drawString(String(minTemp, 1) + " " + unit, graphX + graphWidth + 5, graphY + graphHeight - 10);
}

void viewHistoricData() {
  int windowStart = 0;
  tft.fillScreen(TFT_BLACK);

  // Buffers for change detection (only store current window)
  float currentTemps[DAYS_TO_SHOW];
  String currentDates[DAYS_TO_SHOW];
  
  // Previously selected weather parameter
  String previousDates[DAYS_TO_SHOW] = { "", "", "", "", "", "", "" };
  float previousTemps[DAYS_TO_SHOW] = { -1, -1, -1, -1, -1, -1, -1 };

  // Fetch data only if parameter changed
  if (previouslySelectedWeatherParameter != selectedWeatherParameter) {
    fullCount = 0; // Clear full data when changing parameter
    previouslySelectedWeatherParameter = selectedWeatherParameter;
  }

  // Fetch data from the API
  JsonDocument doc;
  {
    HTTPClient http;
    http.useHTTP10(true);
    http.begin(buildHistoricUrl());
    int httpCode = http.GET();
    if (httpCode > 0) {
      deserializeJson(doc, http.getStream());
    } else {
      Serial.printf("HTTP error: %d\n", httpCode);
    }
    http.end();
    delay(200);
    yield();
  }

  JsonArray dataArray = doc["value"];
  if (dataArray.size() < DAYS_TO_SHOW) {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Not enough data", 10, 30);
    delay(2000);
    return;
  }

  String currentParameter = weatherParameters[selectedWeatherParameter];
  String currentUnit = getUnitForParameter(currentParameter);

  // Aggregate non-temperature data (e.g., rainfall, wind speed)
  if (selectedWeatherParameter != 0 && fullCount == 0) {
    float sumPerDay[MAX_DAYS] = { 0 };
    int countPerDay[MAX_DAYS] = { 0 };
    String uniqueDates[MAX_DAYS];
    int uniqueDateCount = 0;

    for (int i = dataArray.size() - 1; i >= 0; i--) {
      JsonObject item = dataArray[i];
      float value = item["value"];

      long long timestampMs = item["date"].as<long long>();
      time_t timestamp = (time_t)(timestampMs / 1000);
      struct tm* tm = localtime(&timestamp);
      char dateStr[6];
      sprintf(dateStr, "%02d-%02d", tm->tm_mon + 1, tm->tm_mday);
      String date = String(dateStr);

      int index = -1;
      for (int j = 0; j < uniqueDateCount; j++) {
        if (uniqueDates[j] == date) {
          index = j;
          break;
        }
      }

      if (index == -1 && uniqueDateCount < MAX_DAYS) {
        index = uniqueDateCount;
        uniqueDates[uniqueDateCount++] = date;
      }

      if (index != -1) {
        sumPerDay[index] += value;
        countPerDay[index]++;
      }
    }

    // Aggregate daily data into fullTemps and fullDates
    for (int i = uniqueDateCount - 1; i >= 0; i--) {
      fullTemps[fullCount] = sumPerDay[i] / countPerDay[i];
      fullDates[fullCount] = uniqueDates[i];
      fullCount++;
    }
  }

  // Initialize window
  if (selectedWeatherParameter == 0) {
    windowStart = dataArray.size() - DAYS_TO_SHOW;
  } else {
    windowStart = fullCount - DAYS_TO_SHOW;
  }
  if (windowStart < 0) windowStart = 0;

  // Main loop to handle the data display
  while (true) {
    checkForMenuShortcut();
    if (menuRequested) {
      menuRequested = false;
      return;
    }

    bool valid = false;

    // Load data into the temporary arrays
    if (selectedWeatherParameter == 0 && windowStart + DAYS_TO_SHOW <= dataArray.size()) {
      for (int i = 0; i < DAYS_TO_SHOW; i++) {
        JsonObject item = dataArray[windowStart + i];
        currentTemps[i] = item["value"];
        currentDates[i] = item["ref"].as<String>();
      }
      valid = true;
    } else if (selectedWeatherParameter != 0 && windowStart + DAYS_TO_SHOW <= fullCount) {
      for (int i = 0; i < DAYS_TO_SHOW; i++) {
        currentTemps[i] = fullTemps[windowStart + i];
        currentDates[i] = fullDates[windowStart + i];
      }
      valid = true;
    }

    if (!valid) {
      tft.fillScreen(TFT_BLACK);
      tft.drawString("Insufficient data", 10, 30);
      delay(2000);
      return;
    }

    // Detect data changes
    bool dataChanged = false;
    for (int i = 0; i < DAYS_TO_SHOW; i++) {
      if (currentTemps[i] != previousTemps[i] || currentDates[i] != previousDates[i]) {
        dataChanged = true;
        break;
      }
    }

    // Update the screen if data has changed
    if (dataChanged) {
      tft.fillScreen(TFT_BLACK);
      tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString("City: " + cities[cityIndex], 10, 10);
      tft.drawString("History: " + currentParameter, 10, 25);
      drawTemperatureGraph(currentTemps, currentDates, DAYS_TO_SHOW, currentUnit);

      // Save current data for comparison in the next iteration
      for (int i = 0; i < DAYS_TO_SHOW; i++) {
        previousTemps[i] = currentTemps[i];
        previousDates[i] = currentDates[i];
      }
    }

    // Navigation through data (previous/next)
    unsigned long tStart = millis();
    while (millis() - tStart < 1000) {
      checkForMenuShortcut();
      if (menuRequested) {
        menuRequested = false;
        return;
      }

      if (digitalRead(PIN_BUTTON_1) == LOW) {
        windowStart--;
        if (windowStart < 0) windowStart = 0;
        delay(200);
        break;
      }
      if (digitalRead(PIN_BUTTON_2) == LOW) {
        windowStart++;
        delay(200);
        break;
      }

      // If both buttons are pressed, go to settings
      if (digitalRead(PIN_BUTTON_1) == LOW && digitalRead(PIN_BUTTON_2) == LOW) {
        delay(300);
        settings(0);
        return;
      }
    }
  }
}
void selectWeatherParameter(int selectedIndex) {
  int min = 0;
  int max = numWeatherParameters - 1;

  while (true) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);
    tft.drawString("Select Parameter", 10, 10);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.fillRect(250, 0, 70, 25, TFT_WHITE);
    tft.drawString("Select", 260, 10);
    tft.fillRect(250, 145, 70, 25, TFT_WHITE);
    tft.drawString("Down", 260, 155);
    tft.setTextSize(3);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(weatherParameters[selectedIndex], 40, 80);
    tft.setTextSize(2);

    if (selectedIndex > min)
      tft.drawString(weatherParameters[selectedIndex - 1], 40, 60);
    if (selectedIndex < max)
      tft.drawString(weatherParameters[selectedIndex + 1], 40, 110);

    delay(200);

    while (true) {
      checkForMenuShortcut();
      if (menuRequested) {
        menuRequested = false;
        return;  // Exit current view, main loop will call menu()
      }

      int downButton = digitalRead(PIN_BUTTON_1);
      int selectButton = digitalRead(PIN_BUTTON_2);

      if (downButton == LOW) {
        selectedIndex = (selectedIndex + 1) % numWeatherParameters;
        break; // re-render screen
      }

      if (selectButton == LOW) {
        // Ensure the global selectedWeatherParameter is updated before moving to settings
        selectedWeatherParameter = selectedIndex;
        saveSettings();  // Save any new settings

        // Call settings() which likely transitions to another view (like the main menu)
        settings(0);
        return;
      }
    }
  }
}

String getUnitForParameter(const String& param) {
  if (param == "Wind Speed") return "m/s";
  if (param == "Humidity") return "%";
  if (param == "Temperature") return "C";
  return "";
}

String buildHistoricUrl(){
  return "https://opendata-download-metobs.smhi.se/api/version/latest/parameter/"
  + parameters[selectedWeatherParameter]
  + "/station/"
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
