#include <DS3232RTC.h>
#include <Time.h>
#include <Timezone.h>
#include <Wire.h>
#include <TimeAlarms.h>
#include <util.h>
#include <SPI.h>
#include <Ethernet.h>
#include <ArduinoLog.h>

//time
//DS3231 clock;
//US Eastern Time Zone (New York, Detroit)
TimeChangeRule myDST = {"PDT", Second, Sun, Mar, 2, -420};    //Daylight time = UTC - 4 hours
TimeChangeRule mySTD = {"PST", First, Sun, Nov, 2, -480};     //Standard time = UTC - 5 hours
Timezone myTZ(myDST, mySTD);

TimeChangeRule *tcr; 

// network
char buffer [256];
IPAddress pingAddr(8, 8, 8, 8); // ip address to ping
SOCKET pingSocket = 0;
ICMPPing ping(pingSocket, (uint16_t)random(0, 255));
EthernetUDP udp;
EthernetClient client;
//DNSClient dns;
byte mac[] = { 0x2B, 0x12, 0x45, 0x6F, 0x22, 0xCC };

void setup() {
  Serial.begin(9600);
  Log.begin(LOG_LEVEL_VERBOSE, &Serial);

  setupNetwork();
  setupClock();
}

void loop() {
  maintainNetwork();
//  digitalClockDisplay();  
  time_t local = now();
  printTime(local, tcr -> abbrev);

  delay(1000);
}

void printTime(time_t t, char *tz)
{
    sPrintI00(hour(t));
    sPrintDigits(minute(t));
    sPrintDigits(second(t));
    Serial.print(' ');
    Serial.print(dayShortStr(weekday(t)));
    Serial.print(' ');
    sPrintI00(day(t));
    Serial.print(' ');
    Serial.print(monthShortStr(month(t)));
    Serial.print(' ');
    Serial.print(year(t));
    Serial.print(' ');
    Serial.print(tz);
    Serial.println();
}


void digitalClockDisplay(void)
{
    // digital clock display of the time
    Serial.print(year()); 
    Serial.print('-');
    Serial.print(month());
    Serial.print('-');
    Serial.print(day());
    Serial.print('T');
    Serial.print(hour());
    printDigits(minute());
    printDigits(second());

    Serial.println(); 
}

void printDigits(int digits)
{
    // utility function for digital clock display: prints preceding colon and leading 0
    Serial.print(':');
    if(digits < 10)
        Serial.print('0');
    Serial.print(digits);
}


void setupNetwork() {
  int connectionStatus = 0;
  while (connectionStatus == 0) {
    Log.verbose("Requesting Ip Address via DHCP..."CR);
    connectionStatus = Ethernet.begin(mac);
    if (connectionStatus == 0) {
      Log.warning("Failed to configure Ethernet using DHCP. Will retry in 5 seconds."CR);
      delay(5000);
    }
  }
  printIPAddress();
}

void maintainNetwork() {

  switch (Ethernet.maintain())
  {
    case 1:
      //renewed fail
      Log.error("Error: DHCP renewal failed");
      break;

    case 2:
      //renewed success
      Log.verbose("DHCP renewal succeeded");

      //print your local IP address:
      printIPAddress();
      break;

    case 3:
      //rebind fail
      Log.error("Error: DHCP rebind fail");
      break;

    case 4:
      //rebind success
      Log.verbose("Rebind success");

      //print your local IP address:
      printIPAddress();
      break;

    default:
      //nothing happened
      break;
  }

}

void printIPAddress()
{
  IPAddress ip = Ethernet.localIP();
  Log.notice("Assigned IP Address: %d.%d.%d.%d"CR, ip[0], ip[1], ip[2], ip[3]);
  IPAddress dnsIp = Ethernet.dnsServerIP();
  Log.trace("DNS Server Address: %d.%d.%d.%d"CR, dnsIp[0], dnsIp[1], dnsIp[2], dnsIp[3]);
}

void setupClock() {
  unsigned long unixTime = ntpUnixTime(udp);
  if (unixTime == 0) {
    Log.warning("Unable to set time with NTP");
  } else {
    Log.notice("got ntp response: %l"CR, unixTime);
    RTC.set(unixTime);
    setTime(myTZ.toLocal(unixTime, &tcr));
  }
  setSyncProvider(getLocalTime);
}

time_t getLocalTime() {
  time_t utc = RTC.get(); 
  time_t local = myTZ.toLocal(utc, &tcr);
  return local;
}

unsigned long ntpUnixTime (UDP &udp) {
  static int udpInited = udp.begin(123); // open socket on arbitrary port

  const char timeServer[] = "pool.ntp.org";  // NTP server

  // Only the first four bytes of an outgoing NTP packet need to be set
  // appropriately, the rest can be whatever.
  const long ntpFirstFourBytes = 0xEC0600E3; // NTP request header

  // Fail if WiFiUdp.begin() could not init a socket
  if (! udpInited)
    return 0;

  // Clear received data from possible stray received packets
  udp.flush();

  // Send an NTP request
  if (! (udp.beginPacket(timeServer, 123) // 123 is the NTP port
         && udp.write((byte *)&ntpFirstFourBytes, 48) == 48
         && udp.endPacket()))
    return 0;       // sending request failed

  // Wait for response; check every pollIntv ms up to maxPoll times
  const int pollIntv = 150;   // poll every this many ms
  const byte maxPoll = 15;    // poll up to this many times
  int pktLen;       // received packet length
  for (byte i = 0; i < maxPoll; i++) {
    if ((pktLen = udp.parsePacket()) == 48)
      break;
    delay(pollIntv);
  }
  if (pktLen != 48)
    return 0;       // no correct packet received

  // Read and discard the first useless bytes
  // Set useless to 32 for speed; set to 40 for accuracy.
  const byte useless = 40;
  for (byte i = 0; i < useless; ++i)
    udp.read();

  // Read the integer part of sending time
  unsigned long time = udp.read();  // NTP time
  for (byte i = 1; i < 4; i++)
    time = time << 8 | udp.read();

  // Round to the nearest second if we want accuracy
  // The fractionary part is the next byte divided by 256: if it is
  // greater than 500ms we round to the next second; we also account
  // for an assumed network delay of 50ms, and (0.5-0.05)*256=115;
  // additionally, we account for how much we delayed reading the packet
  // since its arrival, which we assume on average to be pollIntv/2.
  time += (udp.read() > 115 - pollIntv / 8);

  // Discard the rest of the packet
  udp.flush();

  return time - 2208988800ul;   // convert NTP time to Unix time
}

//Print an integer in "00" format (with leading zero).
//Input value assumed to be between 0 and 99.
void sPrintI00(int val)
{
    if (val < 10) Serial.print('0');
    Serial.print(val, DEC);
    return;
}

//Print an integer in ":00" format (with leading zero).
//Input value assumed to be between 0 and 99.
void sPrintDigits(int val)
{
    Serial.print(':');
    if(val < 10) Serial.print('0');
    Serial.print(val, DEC);
}

