#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include "ArduinoOTA.h"    //https://github.com/esp8266/Arduino/tree/master/libraries/ArduinoOTA
#define LILYGO_T5_V213
#include <boards.h>
#include <GxEPD.h>
#include <GxDEPG0213BN/GxDEPG0213BN.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>

#include "fonts/arial5pt7b.h"
#include "fonts/arial6pt7b.h"
#include "fonts/arial7pt7b.h"
#include "fonts/arial8pt7b.h"
#include "fonts/arial9pt7b.h"
#include "fonts/arial13pt7b.h"
#include <DateTime.h>
//#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

//#include "rssRead.hpp"
#include <TinyXML.h>
//static rssRead rss;



#define SHOW_PERCENT_VOLTAGE true

#define VOLTAGE_DIVIDER_RATIO 6.907 //7.0 Varies from board to board
#define FG_COLOR GxEPD_BLACK
#define BG_COLOR GxEPD_WHITE

#define DEFALUT_FONT arial6pt7b
#define BIG_FONT arial9pt7b
#define SMALL_FONT arial6pt7b
#define BIGGEST_FONT arial13pt7b

#define EPD_BUSY  4  // to EPD BUSY
#define EPD_CS    5  // to EPD CS
#define EPD_RST  16  // to EPD RST
#define EPD_DC   17  // to EPD DC
#define EPD_SCK  18  // to EPD CLK
#define EPD_MISO -1  // MISO is not used, as no data from display
#define EPD_MOSI 23  // to EPD DIN

// #define USING_SOFT_SPI      //Uncomment this line to use software SPI
#if defined(USING_SOFT_SPI)
GxIO_Class io(EPD_SCLK, EPD_MISO, EPD_MOSI,  EPD_CS, EPD_DC,  EPD_RSET);
#else
GxIO_Class io(SPI,  EPD_CS, EPD_DC,  EPD_RSET);
#endif

GxEPD_Class display(io, /*RST=*/ EPD_RSET, /*BUSY=*/ EPD_BUSY); // default selection of (9), 7

#define DEBUG_ON 1

#ifdef DEBUG_ON
#define PRINTLN(...) Serial.println(__VA_ARGS__);
#define PRINT(...) Serial.print(__VA_ARGS__);
#else
#define PRINTLN(...) ;
#define PRINT(...) ;
#endif

#define FLASH_CS_PIN 11
#define BUILTIN_LED_PIN 19
#define VOLTAGE_PIN 35

#define SLEEP_DURATION_MIN  720  // 360 = 6 hours, 720 = 12 hours, 1440 == 24 hours. Sleep time in minutes
RTC_DATA_ATTR int bootCount = 0;

enum AlignmentType
{
  LEFT,
  RIGHT,
  CENTER
};

typedef struct {
  int x;
  int y;
  int h;
  int w;
} Bounds;

const char* host = "www.tidetimes.org.uk";
const int httpsPort = 443;
const char *url = "/teignmouth-approaches-tide-times.rss";

TinyXML    xml;
uint8_t    buffer[1024]; // For XML decoding

byte MonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; //specify days in each month
String  time_str; 
String  date_str; // strings to hold time and date
int CurrentHour = 0;
int CurrentMin = 0;
int CurrentSec = 0;
long StartTime = 0;

String TideString="";
String TideArray[4];

void setup_pins()
{
  PRINT("INFO: Setup pins... ");
  pinMode(BUILTIN_LED_PIN, OUTPUT);
  pinMode(FLASH_CS_PIN, OUTPUT);
  pinMode(VOLTAGE_PIN, INPUT);
  PRINTLN("OK");
}

void display_background()
{
  display.fillScreen(BG_COLOR);
  display.setTextColor(FG_COLOR);
  display.setFont(&DEFALUT_FONT);
  display.drawLine(0, 17, display.width(), 17, FG_COLOR);
  draw_string(display.width()-1, 10, "eTideMonitor", RIGHT);
  draw_battery(1, 15);
  draw_string(display.width() / 2, 10, date_str, CENTER);
  draw_string(display.width()-1, display.height()-5, WiFi.localIP().toString(), RIGHT);
  draw_string(1,display.height()-5,"Boot Count " + String(bootCount),LEFT);
}

void display_init()
{
  PRINT("INFO: Begin display initialisation ...");
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
  display.init();
  display.setRotation(1); // Use 1 or 3 for landscape modes
  display_background();
  display.setFont(&BIG_FONT);
  draw_string(display.width() / 2, display.height() / 2 + 12, "CONNECTING", CENTER);
  display.update();
  PRINTLN("DONE");
}
 
void setup() {
  StartTime = millis();
  ++bootCount;
  PRINTLN("----------------------");
  PRINTLN("Boot Number " + String(bootCount));
  setup_pins();
  display_init();
  
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  // it is a good practice to make sure your code sets wifi mode how you want it.

  // put your setup code here, to run once:
  Serial.begin(115200);
  
  //WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wm;

  // reset settings - wipe stored credentials for testing
  // these are stored by the esp library
  //wm.resetSettings();

  // Automatically connect using saved credentials,
  // if connection fails, it starts an access point with the specified name ( "AutoConnectAP"),
  // if empty will auto generate SSID, if password is blank it will be anonymous AP (wm.autoConnect())
  // then goes into a blocking loop awaiting configuration and will return success result

  bool res;
  // res = wm.autoConnect(); // auto generated AP name from chipid
  // res = wm.autoConnect("AutoConnectAP"); // anonymous ap
  res = wm.autoConnect("eInk-TideMonitor","password"); // password protected ap

  if(!res) {
      Serial.println("Failed to connect");
      draw_string(display.width() / 2, display.height() / 2 + 12, "NO WIFI", CENTER);
      display.update();
      delay(5000);
      ESP.restart();
  } 
  else {
      //if you get here you have connected to the WiFi    
      Serial.println("connected...yeey :)");
  }

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("eInk-TideMonitor");
  
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  PRINTLN("Ready OTA8");
  PRINT("IP address: ");
  PRINTLN(WiFi.localIP());

  display.setTextColor(FG_COLOR);
  display.setFont(&DEFALUT_FONT);
  draw_string(display.width()-1, display.height()-5, WiFi.localIP().toString(), RIGHT);

  if (setup_time() == true)
  {
    draw_string(display.width() / 2, 10, date_str, CENTER);
  }
  
  getTides();
  begin_sleep();
}

void XML_callback(uint8_t statusflags, char* tagName,
 uint16_t tagNameLen, char* data, uint16_t dataLen) {

  if((statusflags & STATUS_TAG_TEXT) && !strcasecmp(tagName, "/rss/channel/item/description")) {
    String NewData=data;
    PRINT("Tag=");
    PRINTLN(tagName);
    PRINT("Data=");
    PRINTLN(data);
    NewData.replace("&lt;","<");
    NewData.replace("&quot;","\"");
    NewData.replace("&gt;",">");
    NewData.replace("&amp;amp;","&");
    NewData.replace("&#x28;","(");
    NewData.replace("&#x29;",")");
    PRINT("NewData=");
    PRINTLN(NewData);
    int index = NewData.indexOf("<br/><br/>");
    TideString=NewData.substring(index+10,NewData.length()-5);
    PRINT("TideString=");
    PRINTLN(TideString);
      // Split the string into substrings
    int StringCount=0;
    while (TideString.length() > 0)
    {
      index = TideString.indexOf('<br/>');
      if (index == -1) // No break found
      {
        TideArray[StringCount++] = TideString;
        break;
      }
      else
      {
        TideArray[StringCount++] = TideString.substring(0, index-4);
        TideString = TideString.substring(index+1);
      }
    }

    // Show the resulting substrings
    int y=0;
    display_background();
    display.setFont(&BIG_FONT);

    for (int i = 0; i < StringCount; i++)
    {
      Serial.print(i);
      Serial.print(": \"");
      Serial.println(TideArray[i]);
      Serial.println("\"");
      if (i==0) { y=38;}
      if (i==1) { y=58;}
      if (i==2) { y=78;}
      if (i==3) { y=98;}
      //Serial.print("Y=");
      //Serial.println(y);
      draw_string(25, y, TideArray[i], LEFT);
    }

  }
}


void getTides() {
    
    WiFiClientSecure client;

    PRINT("connecting to ");
    PRINTLN(host);

    client.setInsecure();
    if (!client.connect(host, httpsPort)) {
      PRINTLN("connection failed");
      draw_string(display.width() / 2, display.height() / 2 + 12, "NO INTERNET", CENTER);
      display.update();
      delay(5000);
      ESP.restart();
      return;
    }

    PRINT("requesting URL: ");
    PRINTLN(url);

    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "User-Agent: ESP8266\r\n" +
                 "Connection: close\r\n\r\n");

    PRINTLN("request sent");
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      //PRINTLN(line);
      if (line == "\r") {
        PRINTLN("headers received");
        break;
      }
    }

  uint32_t t;
  int      c;
  uint8_t  b = 0;
  boolean  timedOut;
  #define READ_TIMEOUT 15 // Cancel query if no data received (seconds)
  
  xml.init((uint8_t *)buffer, sizeof(buffer), &XML_callback);
  t = millis(); // Start time
  timedOut = false;
  while(client.connected()) {
    //if(!(b++ & 0x40)) refresh(); // Every 64 bytes, handle displays
    if((c = client.read()) >= 0) {
      xml.processChar(c);
      t = millis(); // Reset timeout clock
    } else if((millis() - t) >= (READ_TIMEOUT * 1000)) {
      Serial.println("---Timeout---");
      timedOut = true;
      break;
    }
  }
      
    client.stop();

    //display_background();
    //display.setTextColor(FG_COLOR);
    //display.setFont(&DEFALUT_FONT);
    //int y=20;
    //PRINTLN("First Y="+y);
   // for (int b = 0; b < 4; b++)
   // {
      //PRINTLN(TideArray[b]);
   //   if (b==0) { y=20;}
   //   if (b==2) { y=60;}
   //   if (b==3) { y=100;}
   //   if (b==4) { y=120;}
   //   PRINTLN("Y="+y);
   //   draw_string(1, y, TideArray[b], LEFT);
    //}
    
    display.update();
}


float read_battery_voltage()
{
  PRINT("INFO: Read battery voltage... ");
  float voltageRaw = 0;
  for (int i = 0; i < 10; i++)
  {
    voltageRaw += analogRead(VOLTAGE_PIN);
    delay(10);
  }
  voltageRaw /= 10;
  PRINTLN("DONE");
  return voltageRaw;
}

void draw_battery(const int x, int y)
{
  float minLiPoV = 3.4;
  float maxLiPoV = 4.1;
  float percentage = 1.0;
  // analog value = Vbat / 2
  float voltageRaw = read_battery_voltage();
  // voltage = divider * V_ref / Max_Analog_Value
  float voltage = VOLTAGE_DIVIDER_RATIO * voltageRaw / 4095.0;
  if (voltage > 1)
  { // Only display if there is a valid reading
    PRINTLN("INFO: Voltage Raw = " + String(voltageRaw));
    PRINTLN("INFO: Voltage = " + String(voltage));
    
    if (voltage >= maxLiPoV)
      percentage = 1;
    else if (voltage <= minLiPoV)
      percentage = 0;
    else
      percentage = (voltage - minLiPoV) / (maxLiPoV - minLiPoV);
      
    display.drawRect(x, y - 12, 19, 10, FG_COLOR);
    display.fillRect(x + 2, y - 10, 15 * percentage, 6, FG_COLOR);
    display.setFont(&DEFALUT_FONT);
    if (SHOW_PERCENT_VOLTAGE)
    {
      draw_string(x + 21, y - 4, String(percentage * 100, 0) + "%", LEFT);
    } else {
      draw_string(x + 21, y - 4, String(voltage, 1) + "v", LEFT);
    }
  }
}

Bounds draw_string(int x, int y, String text, AlignmentType alignment)
{
  int16_t x1, y1; //the bounds of x,y and w and h of the variable 'text' in pixels.
  uint16_t w, h;
  display.setTextWrap(false);
  display.getTextBounds(text, x, y, &x1, &y1, &w, &h);
  if (alignment == RIGHT) 
  {
    x = x - w;
    x1 = x1 - w;
  }
  if (alignment == CENTER)
  {
    x = x - w / 2;
    x1 = x1 - w / 2;
  }
  display.setCursor(x, y);
  Bounds b;
  b.x = x1;
  b.y = y1;
  b.w = w;
  b.h = h;
  display.print(text);
  return b;
}

bool update_local_time()
{
  PRINT("INFO: Updating local time... ");
  struct tm timeinfo;
  char time_output[30], day_output[30], update_time[30];
  while (!getLocalTime(&timeinfo, 5000))
  { // Wait for 5-sec for time to synchronise
    PRINTLN("ERROR: Failed to obtain time");
    return false;
  }
  CurrentHour = timeinfo.tm_hour;
  CurrentMin = timeinfo.tm_min;
  CurrentSec = timeinfo.tm_sec;
  //See http://www.cplusplus.com/reference/ctime/strftime/
  PRINT(&timeinfo, "%a %b %d %Y %H:%M"); // Displays: Saturday, June 24 2017 14:05:49

  strftime(day_output, sizeof(day_output), "%a %b-%d-%Y", &timeinfo); // Creates  'Sat May-31-2019'
  strftime(update_time, sizeof(update_time), "%r", &timeinfo);        // Creates: '@ 02:05:49pm'
  sprintf(time_output, "%s", update_time);

  date_str = day_output;
  time_str = time_output;
  PRINTLN(" DONE");
  return true;
}


bool setup_time()
{
  PRINT("INFO: Setup time... ");
  configTime(0, 3600, "0.uk.pool.ntp.org", "time.nist.gov"); //(gmtOffset_sec, daylightOffset_sec, ntpServer)
  setenv("TZ", "GMT", 1);                       //setenv()adds the "TZ" variable to the environment with a value TimeZone, only used if set to 1, 0 means no change
  tzset();                                                              // Set the TZ environment variable
  delay(100);
  PRINTLN("DONE");
  return update_local_time();
}

void begin_sleep()
{
  display.powerDown();
  long SleepTimer = (SLEEP_DURATION_MIN * 60 - ((CurrentMin % SLEEP_DURATION_MIN) * 60 + CurrentSec)); //Some ESP32 are too fast to maintain accurate time
  esp_sleep_enable_timer_wakeup((SleepTimer + 20) * 1000000LL);                                        // Added 20-sec extra delay to cater for slow ESP32 RTC timers

  PRINTLN("INFO: Entering " + String(SleepTimer) + "-secs of sleep time");
  PRINTLN("INFO: Awake for : " + String((millis() - StartTime) / 1000.0, 3) + "-secs");
  PRINTLN("INFO: Starting deep-sleep period...");

  gpio_deep_sleep_hold_en();
  esp_wifi_stop();
  esp_deep_sleep_start(); // Sleep for e.g. 30 minutes
}

void loop() {
    // put your main code here, to run repeatedly:   
}
