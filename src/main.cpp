// #include <TimeLib.h>
#include <DCF77.h>
#include "Adafruit_SHT4x.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <sys/time.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <esp_pm.h>
#include "driver/periph_ctrl.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET 4

#define DCF_TCO 10
#define LED 1
#define TOUCH 3
#define SHT 20
#define DCF_PON 21

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
volatile DCF77 DCF = DCF77(DCF_TCO, DCF_TCO);

volatile float temperature;
volatile float humidity;
volatile bool isInitialized = false;
volatile bool isTimeReceived = false;
volatile bool isRadioShown = false;

volatile bool displayOn = true;
volatile unsigned long displayOnSince = 0;
const unsigned long displayAutoOffDelay = 15000;

TaskHandle_t clockTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;
TaskHandle_t temperatureTaskHandle = NULL;
SemaphoreHandle_t touchSem;

int intensity = 0;
int maxIntensity = 3;
unsigned long lastTouchTime = 0;
const unsigned long debounceDelay = 300;

uint64_t boot_timer_us = 0;
uint64_t boot_epoch_s = 0;

void getDateTime(int &h, int &m, int &s, int &weekday, int &day, int &month, int &year)
{
   uint64_t elapsed_s = (esp_timer_get_time() - boot_timer_us) / 1000000;
   time_t now_s = (time_t)(boot_epoch_s + elapsed_s);

   // gmtime breaks Unix timestamp into date+time components
   struct tm *t = gmtime(&now_s);

   h = t->tm_hour;
   m = t->tm_min;
   s = t->tm_sec;
   day = t->tm_mday;
   month = t->tm_mon;        // tm_mon is 0-based
   year = t->tm_year + 1900; // tm_year is years since 1900
   weekday = t->tm_wday;     // tm_wday is 0-based, Sunday = 0
}

void setTimeFromDCF(uint64_t dcf77_seconds)
{
   boot_epoch_s = dcf77_seconds;
   boot_timer_us = esp_timer_get_time();
}

void readTemperatureOnce()
{
   Adafruit_SHT4x sht4 = Adafruit_SHT4x();
   digitalWrite(SHT, HIGH);
   delay(100);
   if (sht4.begin())
   {
      sht4.setPrecision(SHT4X_HIGH_PRECISION);
      sht4.setHeater(SHT4X_NO_HEATER);
      sensors_event_t temp;
      sensors_event_t humid;
      sht4.getEvent(&humid, &temp);
      temperature = temp.temperature;
      humidity = humid.relative_humidity;
   }
   digitalWrite(SHT, LOW);
}

void clockTask(void *parameter)
{
   DCF.Start();
   digitalWrite(DCF_PON, LOW);

   for (;;)
   {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      isRadioShown = isRadioShown ? false : true;
      time_t dcfTime = DCF.getTime();
      // settimeofday
      // struct timeval tv;

      // gettimeofday(&tv, nullptr);
      // settimeofday()

      // Serial.println(bitRead(DCF.runningBuffer, DCF.bufferPosition - 1));
      if (dcfTime != 0)
      {
         if (!isInitialized)
            displayOnSince = millis();
         isInitialized = true;
         isTimeReceived = true;
         isRadioShown = false;

         // struct timeval tv = {.tv_sec = dcfTime, .tv_usec = 0};
         // settimeofday(&tv, nullptr);
         // setTime(dcfTime);
         setTimeFromDCF(dcfTime);

         DCF.Stop();
         digitalWrite(DCF_PON, HIGH);
         clockTaskHandle = NULL;
         vTaskDelete(NULL);
      }
   }
}

void startClockSyncTask()
{
   if (clockTaskHandle == NULL)
      xTaskCreate(clockTask, "ClockTask", 4096, NULL, 1, &clockTaskHandle);
}

void IRAM_ATTR touchISR()
{
   BaseType_t woken = pdFALSE;
   xSemaphoreGiveFromISR(touchSem, &woken);
   if (woken)
      portYIELD_FROM_ISR();
}

void displayTask(void *parameter)
{
   bool oledOff = false;
   for (;;)
   {
      // Auto-off after timeout (only when fully initialized)
      if (isInitialized && displayOn && (millis() - displayOnSince > displayAutoOffDelay))
      {
         displayOn = false;
      }

      // Turn off LCD when display is off and initialized
      if (!displayOn && isInitialized)
      {
         if (!oledOff)
         {
            display.clearDisplay();
            display.display();
            display.ssd1306_command(SSD1306_DISPLAYOFF);
            oledOff = true;
            intensity = 0;
            ledcDetachPin(LED);
            digitalWrite(LED, LOW);
            Wire.end();
         }
         // Block indefinitely — touch ISR will notify us via touchTask
         ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
         continue;
      }

      if (oledOff)
      {
         Wire.begin();
         display.ssd1306_command(SSD1306_DISPLAYON);
         oledOff = false;
         readTemperatureOnce();
      }

      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.cp437(true);

      int h, m, s, weekday, day, month, year;
      getDateTime(h, m, s, weekday, day, month, year);

      // Top section: status or time/date
      if (!isInitialized)
      {
         display.setTextSize(1);
         const char *label = "Getting time";
         int labelWidth = strlen(label) * 6;
         display.setCursor((128 - labelWidth) / 2, 4);
         display.print(label);

         char bufferStr[8];
         sprintf(bufferStr, "%d/60+", DCF.bufferPosition);
         int bufferWidth = strlen(bufferStr) * 6;
         display.setCursor((128 - bufferWidth) / 2, 14);
         display.print(bufferStr);
      }
      else
      {
         // time
         int timeY = 0;
         display.setTextSize(2);
         char timeStr[9];
         sprintf(timeStr, "%02d:%02d:%02d", h, m, s);
         int timeWidth = strlen(timeStr) * 12;
         display.setCursor((128 - timeWidth) / 2, timeY);
         display.print(timeStr);

         // date
         int dateY = 17;
         display.setTextSize(1);
         char dateStr[20];
         const char *dayNames[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
         const char *monthNames[] = {"Jan", "Feb", "M\x84r", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"};
         sprintf(dateStr, "%s, %d %s %04d", dayNames[weekday], day, monthNames[month], year);
         int dateWidth = strlen(dateStr) * 6;
         display.setCursor((128 - dateWidth) / 2, dateY);
         display.print(dateStr);
      }

      // divider
      int dividerY = 27;
      display.drawLine(0, dividerY, display.width() - 1, dividerY, WHITE);

      // inside temp
      int thermometerX = 20;
      int thermometerY = 33;
      display.drawRect(thermometerX - 1, thermometerY, 3, 10, WHITE);
      display.fillCircle(thermometerX, thermometerY + 12, 3, WHITE);
      int tempX = thermometerX + 4 + 4;
      int tempY = 33;
      display.setTextSize(1);
      display.setCursor(tempX, tempY);
      display.print("TEMP");
      display.setCursor(tempX, tempY + 9);
      display.printf("%.1f", temperature);
      display.write(0xF8);
      display.print("C");

      // humidity
      int dropletX = 80;
      int dropletY = 33;
      int dropletWidth = 5;
      display.fillCircle(dropletX + 2, dropletY + 6, 2, WHITE);
      display.fillTriangle(dropletX, dropletY + 4, dropletX + 4, dropletY + 4, dropletX + 2, dropletY, WHITE);
      int humidityX = dropletX + dropletWidth + 4;
      int humidityY = 33;
      display.setCursor(humidityX, humidityY);
      display.print("HUM");
      display.setCursor(humidityX, humidityY + 9);
      display.printf("%.0f%%", humidity);

      // radio
      int radioX = 128 - 11 - 2;
      int radioY = 63;
      if (isRadioShown)
      {
         display.drawLine(radioX, radioY - 2, radioX, radioY - 8, WHITE);
         display.drawPixel(radioX + 1, radioY - 1, WHITE);
         display.drawPixel(radioX + 1, radioY - 9, WHITE);

         display.drawLine(radioX + 2, radioY - 3, radioX + 2, radioY - 7, WHITE);
         display.drawPixel(radioX + 3, radioY - 2, WHITE);
         display.drawPixel(radioX + 3, radioY - 8, WHITE);

         display.drawRect(radioX + 4, radioY - 6, 3, 3, WHITE);
         display.drawLine(radioX + 5, radioY, radioX + 5, radioY - 3, WHITE);

         display.drawLine(radioX + 10, radioY - 2, radioX + 10, radioY - 8, WHITE);
         display.drawPixel(radioX + 9, radioY - 1, WHITE);
         display.drawPixel(radioX + 9, radioY - 9, WHITE);

         display.drawLine(radioX + 8, radioY - 3, radioX + 8, radioY - 7, WHITE);
         display.drawPixel(radioX + 7, radioY - 2, WHITE);
         display.drawPixel(radioX + 7, radioY - 8, WHITE);
      }

      display.display();

      vTaskDelay(1000 / portTICK_PERIOD_MS);
   }
}

void temperatureTask(void *parameter)
{
   for (;;)
   {
      // Block indefinitely while display is off — notified by touchTask on wake
      if (!displayOn)
      {
         ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
         continue;
      }

      Adafruit_SHT4x sht4 = Adafruit_SHT4x();
      digitalWrite(SHT, HIGH);
      bool status = false;
      do
      {
         status = sht4.begin();
         if (!status)
            vTaskDelay(1000 / portTICK_PERIOD_MS);
      } while (!status);

      sht4.setPrecision(SHT4X_HIGH_PRECISION);
      sht4.setHeater(SHT4X_NO_HEATER);

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

      digitalWrite(SHT, LOW);
      vTaskDelay(10000 / portTICK_PERIOD_MS);
   }
}

void touchTask(void *parameter)
{
   for (;;)
   {
      // Sleep until the touch ISR fires
      xSemaphoreTake(touchSem, portMAX_DELAY);

      unsigned long currentTime = millis();
      if (currentTime - lastTouchTime < debounceDelay)
         continue;
      lastTouchTime = currentTime;

      if (!displayOn)
      {
         displayOn = true;
         displayOnSince = currentTime;
         // Wake display and temperature tasks
         xTaskNotifyGive(displayTaskHandle);
         xTaskNotifyGive(temperatureTaskHandle);
      }
      else
      {
         displayOnSince = currentTime;
         intensity++;
         if (intensity > maxIntensity)
            intensity = 0;
         int brightness = (intensity * 1023) / maxIntensity;
         analogWrite(LED, brightness);
      }
   }
}

void dailyTask(void *parameter)
{
   for (;;)
   {
      vTaskDelay(1000 * 60 * 60 * 8 / portTICK_PERIOD_MS);
      isTimeReceived = false;
      startClockSyncTask();
   }
}

void setup()
{

   Serial.begin(9600);

   pinMode(DCF_TCO, INPUT);
   pinMode(TOUCH, INPUT);
   pinMode(LED, OUTPUT);
   pinMode(SHT, OUTPUT);
   pinMode(DCF_PON, OUTPUT);

   digitalWrite(SHT, LOW);
   digitalWrite(DCF_PON, HIGH);

   // Unused GPIO pins pulled low to prevent floating inputs drawing current
   pinMode(5, INPUT_PULLDOWN);
   pinMode(6, INPUT_PULLDOWN);
   pinMode(7, INPUT_PULLDOWN);

   // Disable WiFi and Bluetooth radios — not used, major power drain
   esp_wifi_stop();
   esp_wifi_deinit();
   esp_bt_controller_disable();
   esp_bt_controller_deinit();

   // Gate clocks of all unused peripherals
   periph_module_disable(PERIPH_UART1_MODULE);
   periph_module_disable(PERIPH_SPI2_MODULE);
   periph_module_disable(PERIPH_I2S1_MODULE);
   periph_module_disable(PERIPH_UHCI0_MODULE);
   periph_module_disable(PERIPH_RMT_MODULE);
   periph_module_disable(PERIPH_TWAI_MODULE);
   periph_module_disable(PERIPH_SARADC_MODULE);
   periph_module_disable(PERIPH_RSA_MODULE);
   periph_module_disable(PERIPH_AES_MODULE);
   periph_module_disable(PERIPH_SHA_MODULE);
   periph_module_disable(PERIPH_HMAC_MODULE);
   periph_module_disable(PERIPH_DS_MODULE);

   // Dynamic frequency scaling: auto-reduce CPU clock when idle, no sleep
   esp_pm_config_esp32c3_t pm_config = {
       .max_freq_mhz = 10,
       .min_freq_mhz = 10,
       .light_sleep_enable = false,
   };
   esp_pm_configure(&pm_config);

   if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
   {
      while (1)
         delay(1);
   }

   // Start display and touch tasks (full interface shown immediately)
   touchSem = xSemaphoreCreateBinary();
   xTaskCreate(displayTask, "DisplayTask", 4096, NULL, 1, &displayTaskHandle);
   xTaskCreate(touchTask, "TouchTask", 2048, NULL, 2, NULL);
   attachInterrupt(digitalPinToInterrupt(TOUCH), touchISR, RISING);

   // Start DCF time sync, temperature, and daily re-sync tasks
   xTaskCreate(temperatureTask, "TemperatureTask", 4096, NULL, 1, &temperatureTaskHandle);
   startClockSyncTask();
   xTaskCreate(dailyTask, "DailyTask", 4096, NULL, 1, NULL);
}

void loop()
{
}
