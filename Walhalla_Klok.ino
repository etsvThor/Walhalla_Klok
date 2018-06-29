#include <Ethernet.h>
#include <TimeLib.h>
#include <utility/w5100.h>
#include "PetitFS.h"
#include "PetitSerial.h"

PetitSerial PS;
#define Serial PS // Replace standard serial calls
FATFS webFile;    // File system object
uint8_t buf[32];  // Larger buffer is faster transfer speeds, at the cost of ram

#define PWM_R     5
#define PWM_G     9
#define PWM_B     6
#define BUTTON    7
#define REF_HIGH  8
#define TRIGGER1  2
#define TRIGGER2  3
#define SD_CS     4
#define ETH_CS    10

#define NTP_PACKET_SIZE   48        // NTP time stamp is in the first 48 bytes of the message
#define LOCALPORT         80        // Local port to listen for UDP packets
#define SYNCINTERVAL      600       // Synchronisation interval in seconds
#define TIMEZONEOFFSET    1         // Set the timezone to GMT +1
#define CONNECTIONTIMOUT  10000     // Time after a connection is automatically closed

#define BOOTSITE  1
#define RGBSITE   2

// Enter a MAC address for your controller below.
// Newer Ethernet shields have a MAC address printed on a sticker on the shield
/*
  Dns naam thorclock.ele.tue.nl. met ip adres 131.155.34.128 en ethernet adres 90:a2:da:0d:0d:1c is nu geregistreerd in de DHCP en DNS server met commentaar "Clock Thor - FLX 6.152 Walhalla".
*/
byte mac[] = {0x90, 0xA2, 0xDA, 0x0D, 0x0D, 0x1C};
IPAddress timeServer(193, 92, 150, 3); 		// time.nist.gov NTP server
char gettxt[30];                          // string for fetching data from address
byte packetBuffer[NTP_PACKET_SIZE]; 			// buffer to hold incoming and outgoing packets
uint8_t clockTime[2] = {12, 0};

unsigned int nrSyncs = 0;
bool cycleStarted = false;
unsigned long oldTime = 0;
bool evenOdd = false;
bool initializingDone = false;
bool timeInitialized = false;
bool daylightSavingTime = false;

// A UDP instance to let us send and receive packets over UDP
EthernetUDP Udp;
EthernetServer server(80);
EthernetClient client;
byte socketStat[MAX_SOCK_NUM];

void setup()
{
  // deselect Ethernet chip on SPI bus
  pinMode(ETH_CS, OUTPUT);
  digitalWrite(ETH_CS, HIGH);
  pinMode(TRIGGER1, OUTPUT);
  pinMode(TRIGGER2, OUTPUT);
  pinMode(REF_HIGH, OUTPUT);
  pinMode(PWM_R, OUTPUT);
  pinMode(PWM_G, OUTPUT);
  pinMode(PWM_B, OUTPUT);
  setRGB(255, 255, 255); // Set white on boot
  digitalWrite(TRIGGER1, LOW);
  digitalWrite(TRIGGER2, LOW);
  digitalWrite(REF_HIGH, HIGH);
  pinMode(BUTTON, INPUT_PULLUP);

  Serial.begin(115200); // Only enable when debugging
  Serial.println(F("Initializing SD card"));

  if (pf_mount(&webFile)) {
    Serial.println(F("SD initialization failed!"));
    // no point in carrying on, so do nothing forevermore:
    setRGB(255, 0, 255); // Set purple on SD card error
    while (true);
  }
  Serial.println(F("SD initialization done."));

  // check for index.htm file
  if (pf_open("BOOT.HTM")) {
    Serial.println(F("ERROR - Can't find BOOT.HTM file!"));
    setRGB(255, 0, 255); // Set purple on SD card error
    while (true);
  }
  Serial.println(F("SUCCESS - Found BOOT.HTM file."));

  Serial.println(F("Initializing Ethernet"));
  if (Ethernet.begin(mac) == 0) {  // start Ethernet and UDP
    Serial.println(F("Failed to configure Ethernet using DHCP, please restart process"));
    // no point in carrying on, so do nothing forevermore:
    setRGB(255, 255, 0); // Set yellow on ethernet error
    while (true);
  }
  server.begin();
  Serial.println(F("Succeeded to configure Ethernet using DHCP"));
  Serial.print(F("IP number assigned by DHCP is: "));
  Serial.println(Ethernet.localIP());
  Serial.println(F("Waiting for activation"));
  while (!initializingDone) {
    setRGB(0, 0, 255); // Set blue when ready to initialize
    webServer(BOOTSITE); // Check if time is given via interface
    if (digitalRead(BUTTON) == 0)
    {
      initializingDone = true; // Clock is set at 12 o'clock
    }
    setRGB(0, 0, 0); // Reset leds when initializing is done
  }
  Udp.begin(LOCALPORT);
  Serial.println(F("Waiting for sync"));
  setSyncProvider(getNtpTime);
  setSyncInterval(SYNCINTERVAL);
}

time_t prevDisplay = 0; // when the digital clock was displayed

void loop()
{
  if (timeStatus() != timeNotSet) {
    if (now() != prevDisplay) { //update the display only if time has changed
      prevDisplay = now();
      digitalClockDisplay();
      ShowSockStatus();
      closeSockets();
    }
  }
  else
  {
    setRGB(255, 0, 0); // Set red on time error
  }
  clockTrigger();
  webServer(RGBSITE);
}

void clockTrigger() {
  if (clockTime[0] == hourFormat12() && clockTime[1] == minute()) {
    timeInitialized = true;
  }
  else {
    if (!cycleStarted) {
      oldTime = millis();
      cycleStarted = true;
    }
    if (cycleStarted) {
      if (millis() - oldTime <= 125) {
        if (evenOdd == 0 ) {
          digitalWrite(TRIGGER1, HIGH);
          digitalWrite(TRIGGER2, LOW);
          if (!timeInitialized)
            setRGB(255, 40, 0);
        }
        else if (evenOdd == 1) {
          digitalWrite(TRIGGER1, LOW);
          digitalWrite(TRIGGER2, HIGH);
          if (!timeInitialized)
            setRGB(0, 255, 0);
        }
      }
      else {
        digitalWrite(TRIGGER1, LOW);
        digitalWrite(TRIGGER2, LOW);
        evenOdd = !evenOdd;
        clockTime[1]++;
        timeCheck();
        analogClockDisplay();
        cycleStarted = false;
      }
      if (clockTime[0] == 4 && clockTime[1] == 0)       setRGB(255, 0, 0);
      else if (clockTime[0] == 4 && clockTime[1] == 15) setRGB(255, 55, 0);
      else if (clockTime[0] == 4 && clockTime[1] == 30) setRGB(0, 255, 0);
      else if (clockTime[0] == 6 && clockTime[1] == 30) setRGB(255, 55, 0);
      else if (clockTime[0] == 6 && clockTime[1] == 40) setRGB(255, 0, 0);
      else if (clockTime[0] == 7 && clockTime[1] == 0)  setRGB(0, 0, 0);
    }
  }
}

void ShowSockStatus()
{
  for (int i = 0; i < MAX_SOCK_NUM; i++) {
    Serial.print(F("Socket#"));
    Serial.print(i);
    uint8_t s = W5100.readSnSR(i);
    socketStat[i] = s;
    Serial.print(F(":0x"));
    Serial.print(s, 16);
    Serial.print(F(" "));
    Serial.print(W5100.readSnPORT(i));
    Serial.print(F(" D:"));
    uint8_t dip[4];
    W5100.readSnDIPR(i, dip);
    for (int j = 0; j < 4; j++) {
      Serial.print(dip[j], 10);
      if (j < 3) Serial.print(".");
    }
    Serial.print(F("("));
    Serial.print(W5100.readSnDPORT(i));
    Serial.println(F(")"));
  }
}

void webServer(uint8_t siteNumber) {
  client = server.available(); // try to get client

  if (client) { // got client?
    time_t timeout = millis() +  CONNECTIONTIMOUT;
    // an http request ends with a blank line
    boolean currentLineIsBlank = true;
    boolean canEndConnection = false;
    uint8_t idx = 0;
    while (client.status() != 0) { //client.connected() is not reliable apparently, use client.status() != 0 instead
      if (client.available()) { // client data available to read
        char c = client.read(); // read 1 byte (character) from client
        //Serial.write(c);

        if (idx < sizeof(gettxt)) {
          gettxt[idx] = c;
          idx++;
        }

        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) {

          // Here is where the POST data is
          char post[20] = {};
          for (int i = 0; client.available() && i < 20; i++)
          {
            post[i] = client.read();
            Serial.write(post[i]);
          }
          Serial.println();

          int res;
          switch (siteNumber)
          {
            case BOOTSITE:
              int H, M, T;
              res = sscanf_P(post, PSTR("H=%d&M=%d&T=%d"), &H, &M, &T); // For example: H=6&M=57&T=0
              if (res == 3)
              {
                clockTime[0] = H;
                clockTime[1] = M;
                daylightSavingTime = T;
                evenOdd = M % 2;
                initializingDone = true;
              }
              break;
            case RGBSITE:
              int R, G, B;
              res = sscanf_P(post, PSTR("R=%d&G=%d&B=%d"), &R, &G, &B); // For example: R=1&G=2&B=3
              if (res == 3)
              {
                setRGB(R, G, B);
              }
              break;
          }

          if (!memcmp_P(&gettxt[5], PSTR("favicon"), 7)) {
            pf_open("HTTPFAV.TXT");
            writeFile();
            pf_open("FAVICON.ICO");
          }
          else if (!memcmp_P(&gettxt[5], PSTR("style"), 5)) {
            pf_open("HTTPCSS.TXT");
            writeFile();
            pf_open("STYLE.CSS");
          }
          else {
            // send a standard http response header
            pf_open("HTTP.TXT");
            writeFile();

            // send web page
            switch (siteNumber)
            {
              case BOOTSITE:
                pf_open("BOOT.HTM");        // open web page file
                break;
              case RGBSITE:
                pf_open("INDEX.HTM");        // open web page file
                break;
            }
          }
          writeFile();

          canEndConnection = true;
        }
        else if (c == '\n') {
          // you're starting a new line
          currentLineIsBlank = true;
        }
        else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
      if (canEndConnection || millis() > timeout)
      {
        while (client.read() > 0); // client.stop() can misbehave if the rx buffer isn't empty
        client.stop();
        Serial.println(F("CONNECTION STOPPED!!"));
      }
    }
  }
}

void closeSockets() {
  uint8_t sockCounter = 0;
  // Check how many sockets are stuck
  for (int i = 0; i < 4; i++) {
    if (W5100.readSnSR(i) == 0x17) {
      sockCounter++;
    }
  }
  // Force close them all if enough are stuck
  if (sockCounter >= 3) {
    for (int i = 0; i < 4; i++) {
      if (W5100.readSnSR(i) == 0x17) {
        W5100.writeSnCR(i, Sock_CLOSE);
      }
    }
  }
}

void writeFile() {
  uint16_t len;
  while (true) {
    pf_read(buf, sizeof(buf), &len);
    if (len == 0) break;
    client.write(buf, len);
  }
}

void timeCheck() {
  if (clockTime[1] >= 60) {
    clockTime[1] = 0;
    clockTime[0]++;
  }
  if (clockTime[0] >= 13)
    clockTime[0] = 1;
}

void digitalClockDisplay() {
  // digital clock display of the time
  Serial.print(F("Digital clock time: "));
  Serial.print(hourFormat12());
  printDigits(minute());
  printDigits(second());
  Serial.print(F(" "));
  Serial.print(day());
  Serial.print(F(" "));
  Serial.print(month());
  Serial.print(F(" "));
  Serial.print(year());
  Serial.print(F(", Nr of syncs: "));
  Serial.print(nrSyncs);
  Serial.println();
}

void analogClockDisplay() {
  // analog clock display of the time
  Serial.print(F("Analog clock time: "));
  Serial.print(clockTime[0]);
  printDigits(clockTime[1]);
  Serial.println();
}

void printDigits(int digits) {
  // utility for digital clock display: prints preceding colon and leading 0
  Serial.print(F(":"));
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

time_t getNtpTime() {
  nrSyncs++;
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println(F("Transmit NTP Request"));

  Serial.print(F("time.nist.gov = "));
  Serial.println(timeServer);
  sendNTPpacket(timeServer); 	// send an NTP packet to a time server

  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println(F("Receive NTP Response"));
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      // convert four bytes starting at location 40 to a long integer
      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
      unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      // combine the four bytes (two words) into a long integer
      // this is NTP time (seconds since Jan 1 1900):
      time_t secsSince1900 = highWord << 16 | lowWord;
      // convert to epoch time by adding 70 years
      time_t secsSince1970 = secsSince1900 - 2208988800UL;
      if (daylightSavingTime)
        return secsSince1970 + TIMEZONEOFFSET * SECS_PER_HOUR + SECS_PER_HOUR ;
      else
        return secsSince1970 + TIMEZONEOFFSET * SECS_PER_HOUR;

    }
  }
  Serial.println(F("No NTP Response :-("));
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress & address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;	  // LI, Version, Mode
  packetBuffer[1] = 0;	   // Stratum, or type of clock
  packetBuffer[2] = 6;	   // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:

  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void setRGB(uint8_t R, uint8_t G, uint8_t B)
{
  analogWrite(PWM_R, 255 - R);
  analogWrite(PWM_G, 255 - G);
  analogWrite(PWM_B, 255 - B);
}
