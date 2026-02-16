#include <TimeLib.h>
#include <DCF77.h>
#include "Adafruit_SHT4x.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char *ssid = "Ghate";
const char *password = "mathildenplatz2025";
const char *apiKey = "985547199d5e49028a2d6349f13cfb21";
const char *lat = "49.63";
const char *lon = "8.36";
String serverPath = "http://api.openweathermap.org/data/2.5/weather?lat=" + String(lat) + "&lon=" + String(lon) + "&appid=" + String(apiKey) + "&units=metric";

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_MOSI 23
#define OLED_CLK 18
#define OLED_DC 5
#define OLED_CS 19
#define OLED_RESET 17

#define DCF_TCO 16
#define DCF_PON 4

Adafruit_SHT4x sht4 = Adafruit_SHT4x();
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
volatile DCF77 DCF = DCF77(DCF_TCO, DCF_TCO);

volatile float temperature;
volatile float humidity;
volatile bool isInitialized = false;
volatile bool isTimeReceived = false;
volatile bool isRadioShown = false;
volatile bool isWifiConnected = false;
String weatherDescription = "";
volatile float outsideTemperatire;

void connectWifi()
{
   WiFi.mode(WIFI_STA);
   WiFi.begin(ssid, password);
   Serial.print("Connecting to WiFi...");
   while (WiFi.status() != WL_CONNECTED)
   {
      isWifiConnected = false;
      delay(1000);
   }

   isWifiConnected = true;
   Serial.print("WiFi connected! ");
   Serial.println(WiFi.localIP().toString());
}

void getWeatherData()
{
   HTTPClient http;

   http.begin(serverPath.c_str());
   int httpResponseCode = http.GET();

   if (httpResponseCode > 0)
   {
      Serial.print("HTTP Reponse code: ");
      Serial.println(httpResponseCode);

      String payload = http.getString();
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (error)
      {
         Serial.print("JSON parsing failed: ");
         Serial.println(error.c_str());
         return;
      }

      weatherDescription = doc["weather"][0]["main"].as<String>();
      outsideTemperatire = doc["main"]["temp"].as<float>();
   }
   else
   {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
   }

   http.end();
}

void clockTask(void *parameter)
{
   for (;;)
   {
      delay(1000);
      isRadioShown = isRadioShown ? false : true;
      time_t dcfTime = DCF.getTime();
      // Serial.println(bitRead(DCF.runningBuffer, DCF.bufferPosition - 1));
      if (dcfTime != 0)
      {
         Serial.println("Time is updated");
         isInitialized = true;
         isTimeReceived = true;
         isRadioShown = false;
         setTime(dcfTime);

         vTaskDelete(NULL);
      }
   }
}

void displayTask(void *parameter)
{
   for (;;)
   {
      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.cp437(true);

      // time
      if (isInitialized)
      {
         int timeY = 0;
         display.setTextSize(2);
         char timeStr[9];
         sprintf(timeStr, "%02d:%02d:%02d", hour(), minute(), second());
         int timeWidth = strlen(timeStr) * 12;
         display.setCursor((128 - timeWidth) / 2, timeY);
         display.print(timeStr);

         // date
         int dateY = 17;
         display.setTextSize(1);
         char dateStr[11];
         const char *dayNames[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
         const char *monthNames[] = {"Jan", "Feb", "M\x84r", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"};
         sprintf(dateStr, "%s, %d %s %04d", dayNames[weekday() - 1], day(), monthNames[month() - 1], year());
         int dateWidth = strlen(dateStr) * 6;
         display.setCursor((128 - dateWidth) / 2, dateY);
         display.print(dateStr);
      }
      else
      {
         char initStr[16] = "Initializing...";
         display.setTextSize(1);
         int initWidth = strlen(initStr) * 6;
         display.setCursor((128 - initWidth) / 2, 0);
         display.print(initStr);

         char bufferStr[3];
         sprintf(bufferStr, "%d/60+ (120)", DCF.bufferPosition);
         int bufferWidth = strlen(bufferStr) * 6;
         display.setCursor((128 - bufferWidth) / 2, 17);
         display.print(bufferStr);
      }

      // divider
      int dividerY = 27;
      display.drawLine(0, dividerY, display.width() - 1, dividerY, WHITE);

      // inside temp
      int thermometerX = 2;
      int thermometerY = 31;
      display.drawRect(thermometerX, thermometerY, thermometerX + 1, 10, WHITE);
      display.fillCircle(thermometerX + 1, thermometerY + 12, 3, WHITE);
      int tempX = 11;
      int tempY = 31;
      display.setTextSize(1);
      display.setCursor(tempX, tempY);
      display.print("IN");
      display.setCursor(tempX, tempY + 9);
      display.printf("%.1f", temperature);
      display.write(0xF8);
      display.print("C");

      // outside temp
      int outsideTempX = 54;
      int outsideTempY = 31;
      display.setCursor(outsideTempX, outsideTempY);
      display.print("OUT");
      display.setCursor(outsideTempX, outsideTempY + 9);
      display.printf("%.1f", outsideTemperatire);
      display.write(0xF8);
      display.print("C");

      // humidity
      int dropletX = 100;
      int dropletY = 31;
      int dropletWidth = 5;
      display.fillCircle(dropletX + 2, dropletY + 6, 2, WHITE);
      display.fillTriangle(dropletX, dropletY + 4, dropletX + 4, dropletY + 4, dropletX + 2, dropletY, WHITE);
      int humidityX = dropletX + dropletWidth + 4;
      int humidityY = 31;
      display.setCursor(humidityX, humidityY);
      display.print("HUM");
      display.setCursor(humidityX, humidityY + 9);
      display.printf("%.0f%%", humidity);

      // weather
      int cloudX = 0;
      int cloudY = 54;
      int cloudWidth = 20;
      display.fillCircle(cloudX + 5, cloudY + 3, 3, WHITE);   // left circle
      display.fillCircle(cloudX + 10, cloudY + 1, 4, WHITE);  // top circle
      display.fillCircle(cloudX + 16, cloudY + 3, 3, WHITE);  // right circle
      display.fillRect(cloudX + 5, cloudY + 3, 11, 4, WHITE); // bottom rect
      int weatherX = cloudX + cloudWidth + 4;
      int weatherY = 54;
      display.setTextSize(1);
      display.setCursor(weatherX, weatherY);
      display.print(weatherDescription);

      int radioWidth = 11;
      int wifiWidth = 13;
      int wifiX = 128 - radioWidth - wifiWidth - 4 - 2;
      int wifiY = 61;
      int radioX = wifiWidth + wifiX + 4;
      int radioY = 61;

      if (isWifiConnected)
      {
         // wifi
         display.drawPixel(wifiX + 6, wifiY, WHITE); // bottom dot

         display.drawLine(wifiX + 5, wifiY - 2, wifiX + 7, wifiY - 2, WHITE); // first arc line
         display.drawPixel(wifiX + 4, wifiY - 1, WHITE);                      // first arc left pixel
         display.drawPixel(wifiX + 8, wifiY - 1, WHITE);                      // first arc right pixel

         display.drawLine(wifiX + 4, wifiY - 4, wifiX + 8, wifiY - 4, WHITE); // second arc line
         display.drawPixel(wifiX + 3, wifiY - 3, WHITE);                      // second arc left pixel 1
         display.drawPixel(wifiX + 2, wifiY - 2, WHITE);                      // second arc left pixel 2
         display.drawPixel(wifiX + 9, wifiY - 3, WHITE);                      // second arc right pixel 2
         display.drawPixel(wifiX + 10, wifiY - 2, WHITE);                     // second arc right pixel 1

         display.drawLine(wifiX + 3, wifiY - 6, wifiX + 9, wifiY - 6, WHITE); // third arc line
         display.drawPixel(wifiX + 2, wifiY - 5, WHITE);                      // third arc left pixel 1
         display.drawPixel(wifiX + 1, wifiY - 4, WHITE);                      // third arc left pixel 2
         display.drawPixel(wifiX + 0, wifiY - 3, WHITE);                      // third arc left pixel 3
         display.drawPixel(wifiX + 10, wifiY - 5, WHITE);                     // third arc right pixel 1
         display.drawPixel(wifiX + 11, wifiY - 4, WHITE);                     // third arc right pixel 2
         display.drawPixel(wifiX + 12, wifiY - 3, WHITE);                     // third arc right pixel 3
      }

      // radio
      if (isRadioShown)
      {
         display.drawLine(radioX, radioY - 2, radioX, radioY - 8, WHITE); // left big arc
         display.drawPixel(radioX + 1, radioY - 1, WHITE);                // left big arc bottom pixel
         display.drawPixel(radioX + 1, radioY - 9, WHITE);                // left big arc top pixel

         display.drawLine(radioX + 2, radioY - 3, radioX + 2, radioY - 7, WHITE); // left small arc
         display.drawPixel(radioX + 3, radioY - 2, WHITE);                        // left small arc bottom pixel
         display.drawPixel(radioX + 3, radioY - 8, WHITE);                        // left small arc top pixel

         display.drawRect(radioX + 4, radioY - 6, 3, 3, WHITE);               // middle square
         display.drawLine(radioX + 5, radioY, radioX + 5, radioY - 3, WHITE); // middle line

         display.drawLine(radioX + 10, radioY - 2, radioX + 10, radioY - 8, WHITE); // right big arc
         display.drawPixel(radioX + 9, radioY - 1, WHITE);                          // right big arc bottom pixel
         display.drawPixel(radioX + 9, radioY - 9, WHITE);                          // right big arc top pixel

         display.drawLine(radioX + 8, radioY - 3, radioX + 8, radioY - 7, WHITE); // right small arc
         display.drawPixel(radioX + 7, radioY - 2, WHITE);                        // right small arc bottom pixel
         display.drawPixel(radioX + 7, radioY - 8, WHITE);                        // right small arc top pixel
      }

      display.display();

      delay(500);
   }
}

void temperatureTask(void *parameter)
{
   for (;;)
   {
      sensors_event_t temp;
      sensors_event_t humid;
      uint32_t timestamp = millis();
      sht4.getEvent(&humid, &temp);
      timestamp = millis() - timestamp;

      // Serial.print("Temperature: ");
      // Serial.print(temp.temperature);
      // Serial.println(" degrees C");
      // Serial.print("Humidity: ");
      // Serial.print(humid.relative_humidity);
      // Serial.println("% rH");

      temperature = temp.temperature;
      humidity = humid.relative_humidity;

      // Serial.print("Read duration (ms): ");
      // Serial.println(timestamp);

      delay(100);
   }
}

void startClockSyncTask()
{
   Serial.println("CLOCK SYNC STARTED");
   xTaskCreate(clockTask, "ClockTask", 4096, NULL, 1, NULL);
}

void dailyTask(void *parameter)
{
   for (;;)
   {
      isTimeReceived = false;
      startClockSyncTask();
      vTaskDelay(1000 * 60 * 60 * 8 / portTICK_PERIOD_MS);
      // vTaskDelay((1000 * 60 * 3) / portTICK_PERIOD_MS);
   }
}

void weatherTask(void *parameter)
{
   for (;;)
   {
      getWeatherData();
      vTaskDelay(1000 * 60 * 10);
   }
}

void wifiTask(void *patameter)
{
   connectWifi();

   xTaskCreate(weatherTask, "WeatherTask", 4096, NULL, 1, NULL);

   vTaskDelete(NULL);
}

void setup()
{
   pinMode(DCF_TCO, INPUT);
   pinMode(DCF_PON, OUTPUT);

   Serial.begin(9600);
   DCF.Start();
   digitalWrite(DCF_PON, LOW);

   while (!Serial)
      delay(10); // pause until console opens

   if (!sht4.begin())
   {
      Serial.println("Couldn't find SHT4x");
      while (1)
         delay(1);
   }

   if (!display.begin(SSD1306_SWITCHCAPVCC))
   {
      Serial.println("SSD1306 allocation failed");
      while (1)
         delay(1);
   }

   Serial.println("Found SHT4x sensor");
   Serial.print("Serial number 0x");
   Serial.println(sht4.readSerial(), HEX);

   sht4.setPrecision(SHT4X_HIGH_PRECISION);
   sht4.setHeater(SHT4X_NO_HEATER);

   xTaskCreate(temperatureTask, "TemperatureTask", 4096, NULL, 1, NULL);
   xTaskCreate(displayTask, "DisplayTask", 4096, NULL, 1, NULL);
   xTaskCreate(wifiTask, "WifiTask", 4096, NULL, 1, NULL);
   startClockSyncTask();

   // xTaskCreatePinnedToCore(temperatureTask, "TemperatureTask", 10000, NULL, 1, NULL, 0);
   // xTaskCreatePinnedToCore(clockTask, "ClockTask", 10000, NULL, 0, NULL, 0);
   // xTaskCreatePinnedToCore(displayTask, "DisplayTask", 10000, NULL, 0, NULL, 1);
}

void loop()
{
}
