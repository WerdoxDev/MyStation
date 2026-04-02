#include <WiFi.h>

// ── Configure your network here ──────────────────────────────────────────────
const char *TARGET_SSID = "Werdox";                  // SSID to connect to
const char *TARGET_PASSWORD = "matin123456"; // Password
// ─────────────────────────────────────────────────────────────────────────────

void setup()
{
   Serial.begin(115200);
   delay(1000);
   WiFi.mode(WIFI_STA);
   WiFi.disconnect(true);
   delay(200);
   Serial.println("=== Scanner + Auto-Connect ===");
}

void loop()
{
   // ── Scan ────────────────────────────────────────────────────────────────────
   Serial.println("\nScanning...");
   int found = WiFi.scanNetworks();
   bool targetVisible = false;

   if (found == 0)
   {
      Serial.println("No networks found.");
   }
   else
   {
      Serial.printf("Found %d network(s):\n", found);
      Serial.println("─────────────────────────────────────────────────");
      for (int i = 0; i < found; i++)
      {
         bool isTarget = WiFi.SSID(i) == TARGET_SSID;
         if (isTarget)
            targetVisible = true;
         Serial.printf("  [%2d] %-32s | Ch %2d | %4d dBm | %s%s\n",
                       i + 1,
                       WiFi.SSID(i).c_str(),
                       WiFi.channel(i),
                       WiFi.RSSI(i),
                       WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "Secured",
                       isTarget ? "  ← TARGET" : "");
      }
      Serial.println("─────────────────────────────────────────────────");
   }
   WiFi.scanDelete();

   // ── Connect ─────────────────────────────────────────────────────────────────
   if (WiFi.status() == WL_CONNECTED)
   {
      Serial.printf("[WIFI] Already connected | IP: %s | RSSI: %d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      delay(3000);
      return;
   }

   if (!targetVisible)
   {
      Serial.printf("[WIFI] \"%s\" not visible, skipping connect attempt.\n", TARGET_SSID);
      delay(3000);
      return;
   }

   Serial.printf("[WIFI] \"%s\" found! Connecting...", TARGET_SSID);
   WiFi.begin(TARGET_SSID, TARGET_PASSWORD);

   int attempts = 0;
   while (WiFi.status() != WL_CONNECTED && attempts < 20)
   {
      delay(500);
      Serial.print(".");
      attempts++;
   }

   if (WiFi.status() == WL_CONNECTED)
   {
      Serial.println("\n[WIFI] Connected!");
      Serial.printf("  IP     : %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("  RSSI   : %d dBm\n", WiFi.RSSI());
      Serial.printf("  MAC    : %s\n", WiFi.macAddress().c_str());
   }
   else
   {
      Serial.printf("\n[WIFI] Failed. Status: %d\n", WiFi.status());
      WiFi.disconnect(true);
      delay(200);
   }

   delay(3000);
}
