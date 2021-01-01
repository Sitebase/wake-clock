/* User timer settings: { hours, minutes } */
int SLEEP_TIME[2] = { 19, 15 };
int DOZE_TIME[2] = { 6, 30 };
int WAKE_TIME[2] = { 7, 0 };
int DAY_TIME[2] = { 7, 30 };
 
/* Includes */
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <Timezone.h>
#include <ArduinoOTA.h>
#include "credentials.h"

/* Pins */
#define LED_RED_PIN 4
#define LED_GREEN_PIN 14
#define MODE_BUTTON 12

/* State machine definitions */
#define DAY   0
#define SLEEP 1
#define DOZE  2
#define WAKE  3

/* Modes */
#define PROGRAM 0
#define CLOCK   1

/* Initial light state */
uint8_t currentState = DAY;

/* Set these WiFi details in the credentials.h file so they don't get included in the git repo 

Content should look like:
#define STASSID "YOURSSID"
#define STAPSK "YOURPASSWORD"
*/
const char* ssid     = STASSID;
const char* password = STAPSK;

int modus = CLOCK;
int buttonState = 0;

//UDP stuff for NTP internet time lookup
//https://www.geekstips.com/arduino-time-sync-ntp-server-esp8266-udp/
unsigned int localPort = 2390;      // local port to listen for UDP packets
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "2.north-america.pool.ntp.org";
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

//Timezone stuff for central time
//https://github.com/JChristensen/Timezone/blob/master/examples/Clock/Clock.ino
// US Eastern Time Zone (New York, Detroit)
TimeChangeRule myDST = {"CDT", Second, Sun, Mar, 2, +120};    // Daylight time = UTC - 5 hours
TimeChangeRule mySTD = {"DST", First, Sun, Nov, 2, +60};     // Standard time = UTC - 6 hours
Timezone myTZ(myDST, mySTD);
TimeChangeRule *tcr;        // pointer to the time change rule, use to get TZ abbrev

void setup() {
  Serial.begin(115200);
  
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(MODE_BUTTON, INPUT_PULLUP);

  set_light_state(currentState); // will turn all the lights off

  wifi_connect();

  // set device in program mode if button is high while starting
  if (digitalRead(MODE_BUTTON) == LOW) {
    modus = PROGRAM;
    Serial.print("Enable program mode");
    digitalWrite(LED_GREEN_PIN, HIGH);
    digitalWrite(LED_RED_PIN, HIGH);
  }

  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());

  setTime(myTZ.toUTC(compileTime()));

  ArduinoOTA.setHostname("wake-clock");
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    digitalWrite(LED_GREEN_PIN, HIGH);
    digitalWrite(LED_RED_PIN, LOW);
    ESP.restart(); // ensures we can reupload program again without having to manual reset ESP
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_RED_PIN, HIGH);
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("Mode: ");
  Serial.println(modus);
  Serial.println(digitalRead(MODE_BUTTON));
}

void loop() {
  if (modus == PROGRAM) {
    ArduinoOTA.handle();
  } else {
    run();
  }
  //buttonState = digitalRead(MODE_BUTTON);
  //Serial.print("Button state: ");
  //Serial.println(buttonState);
}

void run() {

  unsigned long myUTC;
  while (1) {
    myUTC = getUTC();
    if (myUTC == 0) {
      delay(2000);
      Serial.println("Retrying...");
    }
    else break;
  }
  setTime(myUTC);
  
  while(1) {

    time_t utc = now();
    time_t local = myTZ.toLocal(utc, &tcr);
    Serial.println();
    printDateTime(utc, "UTC");
    printDateTime(local, tcr -> abbrev);

    int rightNow[] = { hour(local), minute(local) };
    int rn = big_time(rightNow);
    Serial.print("hour = ");
    Serial.println(hour(local));
    Serial.print("rightNow = ");
    Serial.println(rn);

    Serial.print("DAY_TIME = ");
    Serial.println(big_time(DAY_TIME));
    Serial.print("DOZE_TIME = ");
    Serial.println(big_time(DOZE_TIME));
    Serial.print("WAKE_TIME = ");
    Serial.println(big_time(WAKE_TIME));
    Serial.print("SLEEP_TIME = ");
    Serial.println(big_time(SLEEP_TIME));

    // Day
    //FIXME: This assumes SLEEP will start at night and not in the early morning (eg: 00:12 for 12:12am would trip this up)
    if ((rn >= big_time(DAY_TIME)) && (rn < big_time(SLEEP_TIME)) && (currentState != DAY))
      set_light_state(DAY);
      
    // Doze
    if ((rn >= big_time(DOZE_TIME)) && (rn < big_time(WAKE_TIME)) && (currentState != DOZE))
      set_light_state(DOZE);
    
    // Wake
    if ((rn >= big_time(WAKE_TIME)) && (rn < big_time(DAY_TIME)) && (currentState != WAKE))
      set_light_state(WAKE);

    // Sleep
    if (((rn >= big_time(SLEEP_TIME)) || (rn < big_time(DOZE_TIME))) && (currentState != SLEEP))
      set_light_state(SLEEP);

    // if no LED has to be on we can go to deep sleep
    if (currentState == SLEEP || currentState == DAY) {
      ESP.deepSleep(300e6); // 5 minutes sleep
    } else {
      delay(30000);
    }
  }
}

int big_time(int hoursmins[2]) {
  return (hoursmins[0]*60) + hoursmins[1];
}

void wifi_connect() {
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void set_light_state(uint8_t state) {
  Serial.print("change light state to = ");
  Serial.println(state);
  currentState = state;

  if (state == DAY) {
    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_RED_PIN, LOW);
  }
  if (state == SLEEP) {
    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_RED_PIN, LOW);
  }
  if (state == DOZE) {
    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_RED_PIN, HIGH);
  }
  if (state == WAKE) {
    digitalWrite(LED_RED_PIN, LOW);
    digitalWrite(LED_GREEN_PIN, HIGH);
  }
}

unsigned long getUTC(void) {
  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP); 

  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  // wait to see if a reply is available
  delay(1000);
  
  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
    return 0;
  }
  else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    // if NTP packet received we can go to sleep
    WiFi.forceSleepBegin();
    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);


    // print the hour, minute and second:
    Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
    Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
    Serial.print(':');
    if ( ((epoch % 3600) / 60) < 10 ) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
    Serial.print(':');
    if ( (epoch % 60) < 10 ) {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.println(epoch % 60); // print the second
    return epoch;
  }
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

// Function to return the compile date and time as a time_t value
time_t compileTime()
{
    const time_t FUDGE(10);     // fudge factor to allow for compile time (seconds, YMMV)
    const char *compDate = __DATE__, *compTime = __TIME__, *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char chMon[4], *m;
    tmElements_t tm;

    strncpy(chMon, compDate, 3);
    chMon[3] = '\0';
    m = strstr(months, chMon);
    tm.Month = ((m - months) / 3 + 1);

    tm.Day = atoi(compDate + 4);
    tm.Year = atoi(compDate + 7) - 1970;
    tm.Hour = atoi(compTime);
    tm.Minute = atoi(compTime + 3);
    tm.Second = atoi(compTime + 6);
    time_t t = makeTime(tm);
    return t + FUDGE;           // add fudge factor to allow for compile time
}

// format and print a time_t value, with a time zone appended.
void printDateTime(time_t t, const char *tz)
{
    char buf[32];
    char m[4];    // temporary storage for month string (DateStrings.cpp uses shared buffer)
    strcpy(m, monthShortStr(month(t)));
    sprintf(buf, "%.2d:%.2d:%.2d %s %.2d %s %d %s",
        hour(t), minute(t), second(t), dayShortStr(weekday(t)), day(t), m, year(t), tz);
    Serial.println(buf);
}
