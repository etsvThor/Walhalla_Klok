#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include "Dns.h"
#include <TimeLib.h>

#define SERIAL_ENABLE false
#define PWM_R 5
#define PWM_G 9
#define PWM_B 6
#define BUTTON 7
#define REF_HIGH 8
#define TRIGGER1 2
#define TRIGGER2 3

// Enter a MAC address for your controller below.
// Newer Ethernet shields have a MAC address printed on a sticker on the shield
/*
Dns naam thorclock.ele.tue.nl. met ip adres 131.155.34.128 en ethernet adres 90:a2:da:0d:0d:1c is nu geregistreerd in de DHCP en DNS server met commentaar "Clock Thor - FLX 6.152 Walhalla".
*/
byte mac[] = {0x90, 0xA2, 0xDA, 0x0D, 0x0D, 0x1C};
unsigned int localPort = 80;				// local port to listen for UDP packets
IPAddress timeServer(193, 92, 150, 3); 		// time.nist.gov NTP server (fallback)
const int NTP_PACKET_SIZE = 48; 				// NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; 			// buffer to hold incoming and outgoing packets
const char* host = "nsath.forthnet.gr";			// Use random servers through DNS
const long timeZoneOffset = 3600L;             // Set the timezone to GMT +1
const long processingTime = 1L;                // Compensate for processing/latency??
const int syncInterval = 10000 * 60;            // Synchronisation interval in seconds, also always 5 min.
unsigned int clockTime[2] = {0, 0};

unsigned int nrSyncs = 0;
unsigned int cycleStarted = 0;
unsigned int waitStarted = 0;
unsigned long oldTime = 0;
unsigned int evenOdd = 0;
unsigned int written = 0;

// A UDP instance to let us send and receive packets over UDP
EthernetUDP Udp;
DNSClient Dns;
IPAddress rem_add;

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
  if (SERIAL_ENABLE) {
    Serial.begin(57600); // Only enable when debugging
    while (!Serial) {
      ; // wait for serial port to connect. Needed for Leonardo only
    }
  }
  Serial.println("Serial initialized");
  delay(250);
  Serial.println("Initializing Ethernet");
  // start Ethernet and UDP
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP, please restart process");
    // no point in carrying on, so do nothing forevermore:
    while (true);
  }
  Serial.println("Succeeded to configure Ethernet using DHCP");
  Serial.print("IP number assigned by DHCP is: ");
  Serial.println(Ethernet.localIP());
  Udp.begin(localPort);
  Dns.begin(Ethernet.dnsServerIP() );
  Serial.println("Waiting for manual activation");
  while (digitalRead(BUTTON) == 1) {}
  Serial.println("Waiting for sync");
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
}

void clockTrigger() {
  if (clockTime[0] != hour12() || clockTime[1] < minute()) { // || (clockTime[1] - 15) > minute()) {
    if (cycleStarted == 0) {
      oldTime = millis();
      cycleStarted = 1;
    }
    if (cycleStarted == 1) {
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
        if (evenOdd == 0)
          evenOdd = 1;
        else if (evenOdd == 1)
          evenOdd = 0;
        clockTime[1]++;
        timeCheck();
        analogClockDisplay();
        cycleStarted = 0;
        oldTime = 0;
        written = 0;
      }
    }
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

unsigned int hour12() {
  unsigned int num = hourFormat12();
  if (num >= 12)
    num = 0;
  return num;
}

void digitalClockDisplay() {
  // digital clock display of the time
  Serial.print("Digital clock time: ");
  Serial.print(hourFormat12());
  printDigits(minute());
  printDigits(second());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(" ");
  Serial.print(month());
  Serial.print(" ");
  Serial.print(year());
  Serial.print(", Nr of syncs: ");
  Serial.print(nrSyncs);
  Serial.println();
}

void analogClockDisplay() {
  // analog clock display of the time
  Serial.print("Analog clock time: ");
  Serial.print(clockTime[0]);
  printDigits(clockTime[1]);
  Serial.println();
}

void printDigits(int digits) {
  // utility for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

time_t getNtpTime() {
  nrSyncs++;
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  if (Dns.getHostByName(host, rem_add) == 1 ) {
    Serial.println("DNS resolve...");
    Serial.print(host);
    Serial.print("  = ");
    Serial.println(rem_add);
    sendNTPpacket(rem_add);
  } else {
    Serial.println("DNS fail..., falling back to static IP");
    Serial.print("time.nist.gov = ");
    Serial.println(timeServer);	// fallback
    sendNTPpacket(timeServer); 	// send an NTP packet to a time server
  }

  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      // convert four bytes starting at location 40 to a long integer
      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
      unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      // combine the four bytes (two words) into a long integer
      // this is NTP time (seconds since Jan 1 1900):
      unsigned long secsSince1900 = highWord << 16 | lowWord;
      Serial.print("Seconds since Jan 1 1900 = " );
      Serial.println(secsSince1900);
      unsigned long epoch = secsSince1900 - 2208988800UL;
      Serial.print("Unix time = ");
      Serial.println(epoch);
      return  epoch + timeZoneOffset + processingTime;
    }
  }
  Serial.println("No NTP Response :-(");
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

