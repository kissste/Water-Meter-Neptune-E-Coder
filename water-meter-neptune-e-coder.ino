/*
  Water Meter Neptune E-Coder
  Cloned from: https://github.com/BobPrust/Water-Meter-Neptune-E-Code

Web Server, Web client, water meter reader, MicroSD card
File: DateData.txt
Circuit: Ethernet shield attached to pins 10, 11, 12, 13
SD Enable attached to pin 4
TxClk pin 7
RxData pin 6
led pin 9
Relay pin 8

*/

#include <SPI.h>
#include <Dns.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SD.h>

#define RxData 6
#define TxClock 7
#define Relay 8
///////////////////Ethernet Setup
// Enter a MAC address for your controller below.
// Newer Ethernet shields have a MAC address printed on a sticker on the shield
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
byte ip[] = {192, 168, 17, 176}; // ip in LAN
unsigned int localPort = 8888;

// local port to listen for UDP packets

IPAddress timeServer; // pool.ntp.org NTP server

const int NTP_PACKET_SIZE =
    48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming and outgoing
                                    // packets

EthernetServer server(80);
EthernetClient client;
EthernetUDP Udp; // A UDP instance to let us send and receive packets over UDP

/////////////////////////////Read Meter declarations
unsigned int dataAlign[35];

// 35 is ok Buffer for bit read data

unsigned int meterByte[35];

// 35 is ok

int count = 9;

// byte timing tuning

int bitcount = 0;
int mask = 15;

// mask 0b0000 0000 0000 1111. Strips 4 bit integer

unsigned int last = 0;
unsigned int last_A = 0;

byte set_P = 0;
byte set = 0;

// Send Data record count
// Send Data count register

// Send Data start / stop flag
// command flag in ethernet read

byte command = 0; // Data command

unsigned long previousMillis = 0; // Delay between Meter reads

// added 1.25 seconds to hourly read - Feb 1st coldest month of year - to
// correct for time slippage
unsigned long interval =
    3601250; // 1 Hour Time delay between NTP reads(3,600,000)

unsigned int Current_Date = 0;
unsigned int Current_Hour = 0;
unsigned int Current_Minute = 0;
int bitRate = 415;

// 1187hz. Seems more stable than 1200

boolean state = false;

boolean laststate = false;

void setup() {
  Serial.begin(9600);

  pinMode(RxData, INPUT_PULLUP); // Read
  pinMode(TxClock, OUTPUT);      // clock
  pinMode(Relay, OUTPUT);

  // disable w5100 while setting up SD
  pinMode(10, OUTPUT);
  digitalWrite(10, HIGH);

  Ethernet.begin(mac, ip);
  digitalWrite(10, HIGH);
  server.begin();
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis > interval) {
    previousMillis = currentMillis;
    digitalWrite(Relay, HIGH);
    delay(200);
    GetTime();

    // call GetTime function

    MeterRead();

    // call get meter data

    SDcardWrite();

    // call SD write

    return;

    // seem to need to re initialize?
  }

  // Create a client connection
  EthernetClient client = server.available();
  if (client) {
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        if (set == 1) {
          command = c;

          // set by receiving $ char
          // char following the $ char defines what to send

          set = 0;
        }

        if (c == 36)
          set = 1;

        // char $ recieved

        if (c == '\n') {

          // Null = HTTP request has ended

          client.println(F("HTTP/1.1 200 OK")); // send new page
          client.println(F("Content-Type: text"));
          client.println();

          SD.begin(4);
          File myFile = SD.open("DateData.TXT");
          delay(1);
          if (myFile) {

            while (myFile.available()) {
              char c = (myFile.read());
              if (c == 13)
                last++; // use CR as record increment count

              // All Data client.print command
              if (command == 49)
                client.write(c); // command =1

              // 1 per day All data client.print command
              if ((command == 50) && (c == 'h'))
                set_P = 0;
              if ((command == 50) && (set_P == 1))
                client.write(c);
              if ((command == 50) && (c == 'H'))
                set_P = 1; // 1 per day all days ( Written in SD string)

              // All data last 32 hours client print command
              if ((command == 51) && (last > (last_A - 33)))
                client.write(c); // send last 32

              // All data last 7 days client print command
              if ((command == 52) && (last > (last_A - 169)))
                client.write(c); // send last 7 days

              // All data 31 days client print command
              if ((command == 53) && (last > (last_A - 745)))
                client.write(c); // send last 24

              // 1 per day last 31 days at 23 hour
              if ((command == 54) && (c == 'h'))
                set_P = 0;
              if ((command == 54) && (set_P == 1))
                client.write(c);
              if ((command == 54) && (last > (last_A - 745)) && (c == 'H'))
                set_P = 1;
            }
            myFile.close();
          }
          delay(1);
          client.stop();
          digitalWrite(4, HIGH);
          delay(1);

          last_A =
              last; // cumulative SD record count (Also incremented in SD Write)
          last = 0;

          // clear 'last' record count register

          command = 0; // clear 'command' register
        }
      }
    }
  }
}

void MeterRead() {
  digitalWrite(TxClock, HIGH); // set up to put an initial low on clk line
  digitalWrite(Relay, HIGH);
  delay(200);

  // Clk until Rx line changes (Up to 10 minutes)
  state = digitalRead(RxData);
  while (state == digitalRead(RxData)) {
    digitalWrite(TxClock, HIGH); // inverted due to transistor
    delayMicroseconds(bitRate);
    if (state != digitalRead(RxData))
      break;
    digitalWrite(TxClock, LOW);
    delayMicroseconds(bitRate);
  }

  // Look for Rx line to go Low (62 - 95mS)

  // Quickly align transistion of state change
  state = digitalRead(RxData);
  while (state == digitalRead(RxData)) {
    for (int y = 0; y < 32; y++) {
      if (y == 0)
        digitalWrite(TxClock, HIGH);
      if (y == 15)
        digitalWrite(TxClock, LOW);
      delayMicroseconds(20);
      if (state != digitalRead(RxData))
        break;
    }
  }

  // Read 34 Data bytes. 316mS to 319mS per 34 bytes read.

  for (int mData = 0; mData < 34; mData++) {
    for (int bytecount = 0; bytecount < 11;
         bytecount++) { // 11 bits per byte incl. 2 stop and 1 start
      for (bitcount = 7; bitcount >= 0; bitcount--) {

        // read each bit 8 times. 4 high, 4 low

        if (bitcount == 7)
          digitalWrite(TxClock, HIGH);
        if (bitcount == 3)
          digitalWrite(TxClock, LOW);
        delayMicroseconds(96);

        // 1180 bits/Sec. 107 bytes/Sec

        laststate = !digitalRead(RxData);
        if (bitcount == 5)
          bitWrite(meterByte[mData], bytecount, laststate); // write bit state
      }

      delayMicroseconds(count); // fine tune timing. Count from 7 to 11
    }
    // dataAlign[mData] = meterByte[mData]; //align 11 bit bytes
    if (bitRead(meterByte[mData], 10) == 0)
      dataAlign[mData] = meterByte[mData]; // should be start bit
    if (bitRead(meterByte[mData], 10) > 0)
      dataAlign[mData] = meterByte[mData] >> 1; // shift right may
    correct align if ((bitRead(meterByte[mData], 5) > 0) &&
                      (bitRead(meterByte[mData], 6) > 0)) dataAlign[mData] =
        meterByte[mData] >> 1; // bit 5 & 6 should be 1
    if ((bitRead(meterByte[mData], 6) > 0) &&
        (bitRead(meterByte[mData], 7) > 0))
      dataAlign[mData] = meterByte[mData] >> 2; // align 11 bit bytes

    meterByte[mData] = dataAlign[mData] &
                       mask; // meterData is least 4 bits of masked aligned data

    // delayMicroseconds(count); // maybe more fine timing tuning
  }

  // exit read loop

  digitalWrite(TxClock, HIGH); // put low on meter

  digitalWrite(Relay, LOW); // Turn off relay before writing to SD card
}
// end data capture

// Write data to SDCardFile

void SDcardWrite() {

  pinMode(10, OUTPUT);

  // disable w5100 SPI while starting SD

  digitalWrite(10, HIGH);
  SD.begin(4);

  String dataString = "";

  // clear string

  dataString += Current_Date;
  // dataString += ",";
  dataString += "\t";

  // TAB

  dataString += Current_Hour;
  dataString += ":";
  if (Current_Minute <
      10) { // In the first 10 minutes of each hour, we'll want a leading '0'
    dataString += "0";
  }
  dataString += Current_Minute;
  // dataString += ",";
  dataString += "\t";

  // TAB

  dataString += meterByte[7];
  dataString += meterByte[8];
  dataString += meterByte[9];
  dataString += meterByte[10];

  dataString += meterByte[11];
  dataString += (".");
  dataString += meterByte[12];

  if (Current_Hour == 23) {

    // Write Hour marker

    dataString += "\t";
    dataString += "H";
  } else {
    dataString += "\t";
    dataString += "h";
  }

  dataString += "\0"; // null

  // write to SD file
  File dataFile = SD.open("DateData.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();
  }
  dataString = ""; // delete string
  digitalWrite(4, HIGH);

  // disable SD SPI

  last_A++; // increment record count
}

void GetTime() {

  pinMode(4, OUTPUT); // disable SD SPI while starting w5100
  digitalWrite(4, HIGH);

  pinMode(10, OUTPUT);
  digitalWrite(10, HIGH);

  Udp.begin(localPort);
  DNSClient dns;
  dns.begin(Ethernet.dnsServerIP());
  if (dns.getHostByName("pool.ntp.org", timeServer))
    sendNTPpacket(timeServer);

  // send an NTP packet to a time server

  // wait to see if a reply is available
  delay(4000); // was 1000
  if (Udp.parsePacket()) {

    // We've received a packet, read the data from it

    Udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);

    unsigned long secsSince1900 = highWord << 16 | lowWord;

    const unsigned long seventyYears = 2208988800UL;
    unsigned long epoch = secsSince1900 - seventyYears;

    // Current_Date = (41244 +((epoch - 1354341600) / 86400));// DST
    Current_Date = (41244 + ((epoch - 1354338000) / 86400)); // EST

    // Current_Hour = (((epoch - 14400) % 86400L) / 3600); // DST -4 Hrs from
    // UTC
    Current_Hour = (((epoch - 18000) % 86400L) / 3600); // EST -5 Hrs from UTC

    Current_Minute =
        ((epoch % 3600) / 60); // print the minute (3600 equals secs per minute)
  }
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress &address) {
  memset(packetBuffer, 0, NTP_PACKET_SIZE); // set all bytes in the buffer to 0
  packetBuffer[0] = 0b11100011;             // LI, Version, Mode
  packetBuffer[1] = 0;

  // Stratum, or type of clock

  packetBuffer[2] = 6;

  // Polling Interval

  packetBuffer[3] = 0xEC; // Peer Clock Precision
                          // 8 bytes of zero for Root Delay & Root Dispersion

  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); // NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
