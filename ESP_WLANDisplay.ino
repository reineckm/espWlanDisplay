#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <EasyDDNS.h>
#include "configPage.h"

/* Anzahl Sekunden, welche bei der Verbindung mit einem WLAN gewartet wird */
#define TIMEOUTWLAN 15
/* Anzahl Sekunden, welche die Konfig Seite aktiv ist */
#define TIMEOUTCONFIG 60
/* Timeout für die Antwort des Dienstes den das Display anfragt */
#define TIMOUTCLIENTMILLIS 200
/* Sekunden zwischen Update Zyklen */
#define UPDATEDELAY 30
/* Anzahl Displayzeilen  - configPage.h muss bislang händisch angepasst werden*/
#define ZEILEN 4
/* Anzahl Zeichen pro Zeile + 2 falls Dienst Antwort in " setzt */
#define ZEICHEN 32
/* OLED RESET PIN, nicht zwingend nötig */
#define OLED_RESET 0  // GPIO0

/* Dies ist der Buffer, welcher auf das Display geschrieben wird */
char tbuffer[ZEILEN][ZEICHEN];

/* Konfigurationsdaten, werden im EEPROM gespeichert */
typedef struct {
  int valid;
  char ap[31];
  char pass[31];
  /* die URLs sind in Host und Endpoint getrennt */
  char urls[ZEILEN][2][64];
} configData_t;
configData_t cfg;

Adafruit_SSD1306 OLED(OLED_RESET);
ESP8266WebServer server(80);

/* Konfiguration im EEPROM speichern */
void saveCfg() {
  EEPROM.begin(sizeof(cfg));
  EEPROM.put( 0, cfg );
  delay(200);
  EEPROM.commit();
  EEPROM.end();
}

/* Konfiguration vom EEPROM laden */
void loadCfg() {
  EEPROM.begin(580);
  EEPROM.get( 0, cfg );
  EEPROM.end();
}

/**
   Ruft die URL mit Index idx auf und kopiert die ersten x zeichen des Body
   in den tbuffer mit Index idx
   gibt im erfolgsfall true ansonsten false zurück
*/
boolean copyText(int idx) {
  WiFiClient client;
  client.setTimeout(TIMOUTCLIENTMILLIS);
  const int httpPort = 80;

  if (!client.connect(cfg.urls[idx][0], httpPort)) {
    return false;
  }

  client.print(String("GET ") + cfg.urls[idx][1] + " HTTP/1.1\r\n" +
               "Host: " + cfg.urls[idx][0] + "\r\n"
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      client.stop();
      return false;
    }
  }

  String line;
  // Skip headers
  while (client.available() && line.length() != 1) {
    line = client.readStringUntil('\r');
  }

  // Body
  while (client.available()) {
    line = client.readStringUntil('\r');
    line.trim();
    strncpy(tbuffer[idx], line.c_str(), ZEICHEN);
    tbuffer[idx][ZEICHEN] = 0;
    /* Wir entfernen die Symbole " und | */
    for (int i = 0; i < ZEICHEN; i++) {
      if (tbuffer[idx][i] == '"')
        tbuffer[idx][i] = '|';
      if (tbuffer[idx][i] == '|') {
        strncpy(tbuffer[idx] + i, tbuffer[idx] + i + 1, ZEICHEN - i);
        i--;
      }
    }
  }
  return true;
}

/*
   Verbindung zu Access Point aufbauen
*/
boolean connectAP() {
  if (cfg.valid == 1) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ap, cfg.pass);
    unsigned long timeout = millis() + TIMEOUTWLAN * 1000;
    while ((WiFi.status() != WL_CONNECTED) && (millis() < timeout)) {
      delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
      delay(1000);
      return true;
    } else {
      return false;
    }
  }
  return false;
}

/*
   Antwort verarbeiten und im EEPROM ablegen
*/
void handleConfigChange() {
  /* Sofort antworten damit Browser nicht in Timeout läuft */
  server.send(200, "text/plain", "OK.");
  server.arg("AP").toCharArray(cfg.ap, 31);
  server.arg("PASSWORD").toCharArray(cfg.pass, 31);
  for (int i = 0; i < ZEILEN; i++)
    server.arg("HOST" + String(i)).toCharArray(cfg.urls[i][0], 63);
  for (int i = 0; i < ZEILEN; i++)
    server.arg("URL" + String(i)).toCharArray(cfg.urls[i][1], 63);
  cfg.valid = 1;
  saveCfg();
}

/*
   Zeigt Konfig Page und verarbeitet Antwort vom client
*/
void handleRoot() {
  /*
      Konfiguration findet statt also ist die aktuelle
      Konfig nicht mehr valide
  */
  cfg.valid = 0;
  if (server.hasArg("AP")) {
    handleConfigChange();
  }
  server.send(200, "text/html", CONFIG_HTML);
}

/*
   Antworte auf alles mit 200, damit Hostsysteme möglichst denken,
   dies ist ein guter Hotspot mit Internetverbindung.
*/
void handleNotFound() {
  server.send(200, "text/plain", "HI.");
}

/*
   Display aktivieren und Farbe/ wrapping einstellen
*/
void startDisplay() {
  OLED.begin();
  OLED.clearDisplay();
  OLED.setTextWrap(false);
  OLED.setTextColor(WHITE);
}

/*
   Info Bildschirm Nach verbindung
*/
void showSplashScreenConnected() {
  OLED.clearDisplay();
  OLED.setCursor(0, 0);
  OLED.println("Mini Text Display");
  OLED.setCursor(0, 8);
  OLED.println("reineckm v0.2");
  OLED.setCursor(0, 16);
  OLED.println("verbunden");
  OLED.display();
}

/*
   Info Bildschirm mit Informationen zur Konfiguration
*/
void showConfigInfo() {
  OLED.clearDisplay();
  OLED.setCursor(0, 0);
  OLED.println("Konfiguration ueber");
  OLED.setCursor(0, 8);
  OLED.println("WLAN: Display");
  OLED.setCursor(0, 16);
  OLED.println("IP:   191.168.4.1");
  OLED.display();
}

/*
   Info Bildschirm mit Verbindungsdaten
*/
void showAPInfo() {
  OLED.clearDisplay();
  OLED.setCursor(0, 0);
  OLED.println("Access Point");
  OLED.setCursor(0, 8);
  OLED.println(cfg.ap);
  OLED.setCursor(0, 16);
  OLED.println(cfg.pass);
  OLED.setCursor(0, 24);
  OLED.println("verbinden");
  OLED.display();
}

/**
   Baut einen Server für die Konfiguration auf.
   Wartet dann TIMEOUTCONFIG Sekunde, ob ein nutzer
   versucht das Display zu konfigurieren.
   Ist TIMEOUTCONFIG abgelaufen oder hat eine Konfiguration
   stattgefunden, verbinde mit AP un zeige Splashscreen falls
   verbunden.
*/
void setup()   {
  delay(1000);
  WiFi.softAP("Display");
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();
  loadCfg();
  startDisplay();
  showConfigInfo();
  for (int i = 0; i < TIMEOUTCONFIG; i++) {
    delay(1000);
    server.handleClient();
  }
  while (cfg.valid == 0) {
    delay(1000);
    server.handleClient();
  }
  showAPInfo();
  WiFi.softAPdisconnect(true);
  while (!connectAP()) {}
  showSplashScreenConnected();
  EasyDDNS.service("duckdns");
  EasyDDNS.client("prodigy84","0072992b-63c8-43c1-9951-c8855e38d2f9");
}

/*
   Powersave modus
*/
void powerWifiOff() {
  WiFi.mode( WIFI_OFF );
  WiFi.forceSleepBegin();
  delay( 1 );
}

/*
   Powersave aus und neu connecten
*/
void powerWifiOn() {
  WiFi.forceSleepWake();
  delay( 1 );
  while (!connectAP()) {}
}

/*
   Services ansprechen und Antworten auf Display schreiben
*/
void updateDisplay() {
  for (int i = 0; i < ZEILEN; i++)
    copyText(i);
  OLED.clearDisplay();
  for (int i = 0; i < ZEILEN; i++) {
    OLED.setCursor(0, i * 8);
    OLED.println(tbuffer[i]);
  }
  OLED.display();
}

/*
   Für Immer:
   1. Lese alle URLs
   2. Schreibe alle Zeilen auf Display
   3. Wifi aus um Strom zu sparen
   4. Warte UPDATEDELAY Sekunden
   5. Wifi wieder an
*/
void loop() {
  EasyDDNS.update(10000); 
  updateDisplay();
  powerWifiOff();
  for (int i = 0; i < UPDATEDELAY; i++)
    delay(1000);
  powerWifiOn();
}
