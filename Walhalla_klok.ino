#include <SD.h>
#include <Ethernet.h>
#include <TimeLib.h>

#define PWM_R 5
#define PWM_G 9
#define PWM_B 6
#define BUTTON 7
#define REF_HIGH 8
#define TRIGGER1 2
#define TRIGGER2 3
#define SD_CS 4

// Enter a MAC address for your controller below.
// Newer Ethernet shields have a MAC address printed on a sticker on the shield
/*
Dns naam thorclock.ele.tue.nl. met ip adres 131.155.34.128 en ethernet adres 90:a2:da:0d:0d:1c is nu geregistreerd in de DHCP en DNS server met commentaar "Clock Thor - FLX 6.152 Walhalla".
*/
byte mac[] = {0x90, 0xA2, 0xDA, 0x0D, 0x0D, 0x1C};
unsigned int localPort = 80;				// local port to listen for UDP packets
IPAddress timeServer(193, 92, 150, 3); 		// time.nist.gov NTP server (fallback)
const int NTP_PACKET_SIZE = 48; 				// NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; 			// buffer to hold incoming and outgoing packets
const long timeZoneOffset = 3600L;             // Set the timezone to GMT +1
unsigned int clockTime[2] = {0, 0};
const long syncInterval = 10000L * 60;            // Synchronisation interval in seconds, also always 5 min.

unsigned int nrSyncs = 0;
bool cycleStarted = false;
unsigned int waitStarted = 0;
unsigned long oldTime = 0;
bool evenOdd = false;
unsigned int written = 0;

// A UDP instance to let us send and receive packets over UDP
EthernetUDP Udp;
EthernetServer server(80);
File webFile;

void setup()
{
  pinMode(TRIGGER1, OUTPUT);
  pinMode(TRIGGER2, OUTPUT);
  pinMode(REF_HIGH, OUTPUT);
  pinMode(PWM_R, OUTPUT);
  pinMode(PWM_G, OUTPUT);
  pinMode(PWM_B, OUTPUT);
  analogWrite(PWM_R, 255);
  analogWrite(PWM_G, 255);
  analogWrite(PWM_B, 255);
  digitalWrite(TRIGGER1, LOW);
  digitalWrite(TRIGGER2, LOW);
  digitalWrite(REF_HIGH, HIGH);
  pinMode(BUTTON, INPUT_PULLUP);

  Serial.begin(57600); // Only enable when debugging
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }

  Serial.println(F("Initializing SD card"));

  if (!SD.begin(SD_CS)) {
    Serial.println(F("SD initialization failed!"));
    // no point in carrying on, so do nothing forevermore:
    while (true);
  }
  Serial.println(F("SD initialization done."));

  // check for index.htm file
  if (!SD.exists("index.htm")) {
    Serial.println(F("ERROR - Can't find index.htm file!"));
    while (true);
  }
  Serial.println(F("SUCCESS - Found index.htm file."));


  Serial.println(F("Initializing Ethernet"));
  // start Ethernet and UDP
  if (Ethernet.begin(mac) == 0) {
    Serial.println(F("Failed to configure Ethernet using DHCP, please restart process"));
    // no point in carrying on, so do nothing forevermore:
    while (true);
  }
  server.begin();
  Serial.println(F("Succeeded to configure Ethernet using DHCP"));
  Serial.print(F("IP number assigned by DHCP is: "));
  Serial.println(Ethernet.localIP());
  Udp.begin(localPort);
  Serial.println(F("Waiting for manual activation"));
  while (digitalRead(BUTTON) == 1) {}
  Serial.println(F("Waiting for sync"));
  setSyncProvider(getNtpTime);
  //setSyncInterval(syncInterval);
}

time_t prevDisplay = 0; // when the digital clock was displayed

void loop()
{
  if (timeStatus() != timeNotSet) {
    if (now() != prevDisplay) { //update the display only if time has changed
      prevDisplay = now();
      digitalClockDisplay();
    }
  }
  clockTrigger();
  webServer();
}

void clockTrigger() {
  if (clockTime[0] != hourFormat12() || clockTime[1] < minute()) {
    if (!cycleStarted) {
      oldTime = millis();
      cycleStarted = true;
    }
    if (cycleStarted) {
      if (millis() - oldTime <= 250) {
        if (evenOdd == 0 && written == 0) {
          digitalWrite(TRIGGER1, HIGH);
          digitalWrite(TRIGGER2, LOW);
          written = 1;
        }
        else if (evenOdd == 1 && written == 0) {
          digitalWrite(TRIGGER1, LOW);
          digitalWrite(TRIGGER2, HIGH);
          written = 1;
        }
      }
      else if (millis() - oldTime >= 500 && written == 1) {
        digitalWrite(TRIGGER1, LOW);
        digitalWrite(TRIGGER2, LOW);
        written = 2;
      }
      else {
        evenOdd = !evenOdd;
        clockTime[1]++;
        timeCheck();
        analogClockDisplay();
        cycleStarted = false;
        oldTime = 0;
        written = 0;
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

void webServer() {
  EthernetClient client = server.available();
  if (client) {
    boolean currentLineIsBlank = true;
    while (client.connected()) {
      int i = 0;
      int head = 1;
      int body = 0;
      if (client.available()) {
        char c = client.read();
        Serial.write(c);

        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) {

          // Here is where the POST data is, example: R=1&G=2&B=3
          Serial.println(F("[Begin POST data]"));
          char post[16] = {};
          while (client.available())
          {
            post[i] = client.read();
            Serial.write(post[i]);
            i++;
          }
          Serial.println();
          Serial.println(F("[End POST data]"));
          Serial.println();

          int R, G, B;
          int res = sscanf(post, "R=%d&G=%d&B=%d", &R, &G, &B);

          if (res == 3)
          {
            setRGB(R, G, B);
            Serial.print(F("R="));
            Serial.println(R);
            Serial.print(F("G="));
            Serial.println(G);
            Serial.print(F("B="));
            Serial.println(B);
          }
          else
          {
            Serial.print(F("res="));
            Serial.println(res);
          }

          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");  // the connection will be closed after completion of the response
          //client.println("Refresh: 5");  // refresh the page automatically every 5 sec
          client.println();
          // send web page
          webFile = SD.open("index.htm");        // open web page file
          if (webFile) {
            while (webFile.available()) {
              client.write(webFile.read()); // send web page to client
            }
            webFile.close();
          }
          client.stop();
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
    }
    Serial.println(F("Disconnected"));
  }
}

void timeCheck() {
  if (clockTime[1] >= 60) {
    clockTime[1] = 0;
    clockTime[0]++;
  }
  if (clockTime[0] >= 12)
    clockTime[0] = 0;
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
      return secsSince1970 + timeZoneOffset;
    }
  }
  Serial.println(F("No NTP Response :-("));
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
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
