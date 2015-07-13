
/*todo
-timestamp in file https://codebender.cc/example/SdFat/Timestamp
-gyro
-read rpm from ignition
  14000 U/min
  7000 Z/min
  116,66 Z/sek
  0,11666666666 Z/ms
  8,571428571428571 ms/Z
  8571,428571428571 micros/Z
-read tps from sensor
-read conf from conf_file
http://overskill.alexshu.com/saving-loading-settings-on-sd-card-with-arduino/
-check answere from gps when setup
-timeoutcounter reset after millis => max




http://www.gpsinformation.org/dale/nmea.htm
http://www.hhhh.org/wiml/proj/nmeaxor.html
terminal functions
http://home.arcor.de/petergyger/kb/kb_char.html
http://web.mit.edu/gnu/doc/html/screen_10.html
http://wiki.bash-hackers.org/scripting/terminalcodes

chanlog:
28.6.2015 - Change Gear/Neutral/MS to 0/1
2.7.2015 - Create dynamic Filename
6.7.2015 - 0.0.5 - Create Filename from GPS DATA (DATA,NUM.csv) up to 99 files a day
6.7.2015 - 0.0.6 - Print Header when COM is up
 */

#define wb_analog 0                                                                                               // Analog Pin
#define LED 13                                                                                                    // LED on Arduino

// --------------------------------- K_Line OPTIONS ---------------------------------
#define Serial1_baud 10400                                                                                        // Baud Rate
#define TX 1                                                                                                      // TX Pin
#define t_01 6000                                                                                                 // delay before starting
#define t_02  25                                                                                                  // pulse timing
#define t_03  500                                                                                                 // timeout time
#define t_04  50                                                                                                  // read write delay 45
#define K_BUFFER_SIZE 300
#define k_str_BUFFER_SIZE 20

uint32_t time, tr_1, tr_2, tr_3;                                                                            // timer
uint8_t k_outCntr, k_inCntr, k_inByte, k_chksm, k_size, k_mode;                                                   // Counters & Buffers
boolean started_ ;                                                                                                // Start bit
char k_str[k_str_BUFFER_SIZE], K_BUFFER[K_BUFFER_SIZE];

byte K_START_COM[5] = {                                                                                           // Start Sequence
  0x81, 0x12, 0xF1, 0x81, 0x05
};

byte K_READ_ALL_SENS[7] = {                                                                                       // Request Sensor Data
  0x80, 0x12, 0xF1, 0x02, 0x21, 0x08, 0xAE
};


// --------------------------------- Engine Values ---------------------------------
uint16_t rpm1, iap;



// --------------------------------- GPS ---------------------------------
/*
Because of slow Speed i had to use AltSoftSerial, its much
faster but need to be connect on pre defined pins
See https://www.pjrc.com/teensy/td_libs_AltSoftSerial.html
*/

#define  GPS_BUFFER_SIZE 90                                             // 83
#include <AltSoftSerial.h>
AltSoftSerial gps;
char NMEA[GPS_BUFFER_SIZE];
uint8_t nmea_counter, nmea_clc_chksm, nmea_rd_chksm, NMEA_BUFFER[GPS_BUFFER_SIZE];
#define GPS_DATESIZE 7
char GPS_DATE[GPS_DATESIZE];

// --------------------------------- LOGING OPTIONS ---------------------------------
/*
SD Fat is much faster and has more options
See https://github.com/greiman/SdFat
*/

#include <SdFat.h>
#include <SdFatUtil.h>
#define chipSelect 6                                                    // Chip Select Pin for SPI
SdFat sd;
SdFile file;

char FILENAME[13];
uint8_t FILENUM = 0;




// --------------------------------- Others ---------------------------------
char asci_time[15];
char asci_afr[6];
uint16_t afr;                                                                                                     // Air fuel ratio







// --------------------------------- SETUP ---------------------------------
void setup() {
  Serial.begin(9600);                                                   // Start Serial Comunication
  delay(400);                                                           // catch Due reset problem
  pinMode(LED, OUTPUT);                                                 // Set LED to Output
  //////////////////////////////////// K_Line ////////////////////////////////////
  pinMode(TX, OUTPUT);                                                  // Set TX to Output - Needed for K
  //////////////////////////////////// GPS OPTIONS ////////////////////////////////////
  gps.begin(9600);
  delay(400);                                                           // catch Due reset problem
  // gps.write("$PMTK251,57600*2C\r\n");                                // 57600
  // gps.write("$PMTK251,38400*27\r\n");                                // 38400
  // gps.write("$PMTK251,19200*22\r\n");                                // 19200
  // gps.write("$PMTK251,14400*29\r\n");                                // 14400
  // gps.end();
  // gps.begin(38400);                                                  // Reconnect at new speed
  //  delay(1000);                                                          // Delay to let change take effect
  gps.print(F("$PMTK314,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*29\r\n"));   // RMC
  gps.print(F("$PMTK220,100*2F\r\n"));                                     // update 10HZ
  gps.print(F("$PGCMD,33,0*6D\r\n"));                                      // DISABLE ANTENNA OUTPUT

  delay(5000);                                                            // wait some seconds

  while (!read_nmea()) {
    nmea_encode();
  }

  //////////////////////////////////// LOGING OPTIONS ////////////////////////////////////
  if (!sd.begin(chipSelect, SPI_FULL_SPEED)) {
    //    Serial.println(F("No SD Card available!!"));
  }
  //  else {
  //    Serial.println(F("SD Card available!!"));
  //  }


  while (!create_filename(GPS_DATE)) {
  }
}


// --------------------------------- LOOP ---------------------------------
void loop() {
  if (Serial.available()) {                                               // Reset K_ only for debugg
    Serial.read();
    started_ = false;
  }

  if (k_mode == 1) {                                                      // Run over Sentence Request
    if (k_transmit(K_READ_ALL_SENS, 7)) {
      toggleLed();                                                        // toggle LED

      time = millis();
      memset(asci_time, 0, sizeof(asci_time));
      ultoa(time, asci_time, 10);
      strcpy(asci_time + strlen(asci_time), ",");

      memset(asci_afr, 0, sizeof(asci_afr));
      itoa(afr, asci_afr, 10);
      strcpy(asci_afr + strlen(asci_afr), ",");

      //      Serial.print(asci_time);
      //      Serial.print(asci_afr);
      //      Serial.print(K_BUFFER);
      //      Serial.println(NMEA);
      //      Serial.println();

      //////////// Write DATA //////////////
      if (sd.exists(FILENAME)) {
        if (!file.open(FILENAME, O_WRONLY | O_APPEND | O_AT_END)) {
        }
        file.print(asci_time);
        file.print(asci_afr);
        file.print(K_BUFFER);
        file.print(NMEA);
        file.println();
        file.flush();
        file.close();
      }
      //////////// Print Header //////////////
      if (!sd.exists(FILENAME)) {
        if (!file.open(FILENAME, O_WRONLY | O_CREAT | O_AT_END)) {
        }
        file.println(F("Time(ms),AFR,RPM,TPS,ECT,IAT,IAP,Gear,Fuel 1,Fuel 2,Fuel 3,Fuel 4,IGN 1,IGN 2,IGN 3,IGN 4,STVP,Pair,Clutch,Map,Neutral,NMEA Type,Time,Status,Latitude,North South,Longitude,East West,Speed,Track Angle,Date,Magnetic Variation,,Checksum"));
        file.flush();
        file.close();
      }
    }
  }
  if (k_mode == 0) {                                                      // Start Sequence
    if (k_transmit(K_START_COM, 5)) {
      toggleLed();                                                        // toggle LED
      k_mode++;
    }
  }
  if (nmea_encode ()) {
  }
  afr = read_wideband();
}




// --------------------------------- K_LINE ---------------------------------
boolean k_transmit(byte * function, byte num) {
  if (!started_) {
    if (Serial1.available()) {              // Empty Buffer
      Serial1.read();
    }
    Serial1.end();
    //////////////////////////////////// PULSE 25ms ////////////////////////////////////
    time = millis();                                                                // get Time
    if (!tr_3) {                                                                    // reset Timer
      tr_3 = millis();
    }
    if (tr_3 + t_01 > time) {                                                       // Switch t_01 High
      digitalWrite (TX, HIGH);
    }
    if ((tr_3 + t_01 < time) && (tr_3 + t_01 + t_02 > time)) {                      // Switch t_02 LOW
      digitalWrite (TX, LOW);
    }
    if ((tr_3 + t_01 + t_02 < time) && (tr_3 + t_01 + t_02 + t_02 > time)) {        // Switch t_02 High
      digitalWrite (TX, HIGH);
    }
    //////////////////////////////////// START SERIAL COM ////////////////////////////////////
    if (tr_3 + t_01 + t_02 + t_02 < time) {                                           // Start Serial Comunication
      Serial1.begin(Serial1_baud);
      memset(K_BUFFER, 0, sizeof(K_BUFFER));
      k_mode = 0;
      k_outCntr = 0;
      k_inCntr = 0;
      k_inByte = 0;
      k_chksm = 0;
      k_size = 0;
      tr_1 = 0;
      tr_3 = 0;
      tr_2 = millis();
      started_ = true;
    }
  }
  else {
    //////////////////////////////////// WAIT BEFORE SENDING ////////////////////////////////////
    time = millis();
    if ((num > k_outCntr) && (time >= tr_1 + t_04)) {
      //////////////////////////////////// SEND ////////////////////////////////////
      Serial1.write(function[k_outCntr]);
      k_outCntr++;
      //      tr_4 = micros();
    }
    //////////////////////////////////// RECIEVE ////////////////////////////////////
    if (Serial1.available()) {
      tr_2 = millis();                                                                                          // Reset Timeout Timer
      k_inByte = Serial1.read();
      k_inCntr++;

      if (k_inCntr < k_outCntr) {
        memset(K_BUFFER, 0, sizeof(K_BUFFER));
      }

      memset(k_str, 0, sizeof(k_str));
      // RPM
      if (k_inCntr == 25) {
        rpm1 = 0;
        rpm1 = k_inByte * 100;
      }
      if (k_inCntr == 26) {
        itoa(k_inByte * 100 / 255 + rpm1, k_str, 10);
      }
      // TPS
      if (k_inCntr == 27) {
        itoa((k_inByte - 55) * 100 / 169, k_str, 10);
      }
      // IAP
      if (k_inCntr == 28) {
        iap = 0;
        iap = k_inByte;
      }
      // ECT
      if (k_inCntr == 29) {
        itoa(k_inByte * 100 / 160 - 30, k_str, 10);
      }
      // IAT
      if (k_inCntr == 30) {
        itoa(k_inByte * 100 / 160 - 30, k_str, 10);
      }
      // IAP
      if (k_inCntr == 31) {
        itoa(iap - k_inByte, k_str, 10);
      }
      // GPS
      if (k_inCntr == 34) {
        itoa(k_inByte, k_str, 10);
      }
      // FUEL
      if (k_inCntr == 40) {
        itoa(k_inByte, k_str, 10);
      }
      if (k_inCntr == 42) {
        itoa(k_inByte, k_str, 10);
      }
      if (k_inCntr == 44) {
        itoa(k_inByte, k_str, 10);
      }
      if (k_inCntr == 46) {
        itoa(k_inByte, k_str, 10);
      }
      // IGN
      if (k_inCntr == 49) {
        itoa(k_inByte, k_str, 10);
      }
      if (k_inCntr == 50) {
        itoa(k_inByte, k_str, 10);
      }
      if (k_inCntr == 51) {
        itoa(k_inByte, k_str, 10);
      }
      if (k_inCntr == 52) {
        itoa(k_inByte, k_str, 10);
      }
      // STVA
      if (k_inCntr == 54) {
        itoa(k_inByte * 100 / 255, k_str, 10);
      }
      // PAIR
      if (k_inCntr == 59) {
        itoa(k_inByte, k_str, 10);
      }
      // CLUTCH - MS
      if (k_inCntr == 60) {
        if ((k_inByte == 0x05) || (k_inByte == 0x04)) {
          strcpy(K_BUFFER + strlen(K_BUFFER), "0");
          strcpy(K_BUFFER + strlen(K_BUFFER), ",");
        }
        else {
          strcpy(K_BUFFER + strlen(K_BUFFER), "1");
          strcpy(K_BUFFER + strlen(K_BUFFER), ",");
        }
        if ((k_inByte == 0x04) || (k_inByte == 0x14)) {
          strcpy(K_BUFFER + strlen(K_BUFFER), "0");
          strcpy(K_BUFFER + strlen(K_BUFFER), ",");
        }
        else {
          strcpy(K_BUFFER + strlen(K_BUFFER), "1");
          strcpy(K_BUFFER + strlen(K_BUFFER), ",");
        }
        memset(k_str, 0, sizeof(k_str));
      }
      // NEUTRAL - GEAR
      if (k_inCntr == 61) {
        if (k_inByte == 0x08) {
          strcpy(K_BUFFER + strlen(K_BUFFER), "1");
          strcpy(K_BUFFER + strlen(K_BUFFER), ",");
        }
        else {
          strcpy(K_BUFFER + strlen(K_BUFFER), "0");
          strcpy(K_BUFFER + strlen(K_BUFFER), ",");
        }
        memset(k_str, 0, sizeof(k_str));
      }

      if (strlen(k_str)) {
        strcpy(K_BUFFER + strlen(K_BUFFER), k_str);
        strcpy(K_BUFFER + strlen(K_BUFFER), ",");
      }

      //////////////////////////////////// ENCODE CHECKSUM ////////////////////////////////////
      if (k_inCntr == 4 + k_outCntr) {
        k_size = k_inByte;
      }
      if ((k_inCntr > k_outCntr) && (k_inCntr < k_size + 5 + k_outCntr)) {
        k_chksm = k_chksm + k_inByte;
      }
      //////////////////////////////////// RETURN TRUE + RESET IF CHECKSUM OK////////////////////////////////////
      if (k_inCntr == k_size + 5 + k_outCntr) {
        if (k_chksm == k_inByte) {
          k_outCntr = 0;
          k_inCntr = 0;
          k_inByte = 0;
          k_chksm = 0;
          k_size = 0;
          tr_2 = millis();                                                                                       // Reset Timeouttimer
          tr_1 = millis();                                                                                       // Start Waiting before Writing
          return 1;
        }
      }
    }
    //////////////////////////////////// Timeout ////////////////////////////////////
    else {
      time = millis();
      if (time > tr_2 + t_03) {
        if (!file.open(FILENAME, O_WRONLY | O_APPEND | O_AT_END)) {
        }
        file.println(F("Timeout"));
        file.flush();
        file.close();
        started_ = false;
      }
    }
  }
  return 0;
}





// --------------------------------- GPS ---------------------------------
boolean nmea_encode () {
  if (gps.available()) {
    byte c = gps.read();
    //    Serial.write(c);
    if ((c == 13) && ((nmea_clc_chksm) && (nmea_counter) && (nmea_rd_chksm))) {                                                    // if 'new line' or 'carriage return' is received then EOT.
      nmea_rd_chksm = asci_to_byte(NMEA_BUFFER[nmea_counter - 2]) * 0x10 + asci_to_byte(NMEA_BUFFER[nmea_counter - 1]);            // Convert ASCI to Byte
      if (nmea_clc_chksm == nmea_rd_chksm) {                                                                                       // if Checksum matches
        memset(NMEA, 0, GPS_BUFFER_SIZE);                                                                                          // Clear Buffer
        memcpy (NMEA, NMEA_BUFFER,  GPS_BUFFER_SIZE);                                                                              // Copy To Buffer
        memset(NMEA_BUFFER, 0, GPS_BUFFER_SIZE);                                                                                   // Clear Buffer
        return true;
      }
    }
    if ( c == '$') {                                                                                                               // Start Newline - reset counters
      nmea_clc_chksm = 0;
      nmea_counter = 0;
      nmea_rd_chksm = 0;
    }
    if (c == '*') {                                                                                                                // End of Data
      nmea_rd_chksm++;
    }
    if ((!nmea_rd_chksm) && (!(c == 13 || c == 10 || c == 12 || c == 36 || c == 42)))                                              // Checksum Calculation
    {
      nmea_clc_chksm = nmea_clc_chksm ^ c;
    }
    if (!(c == 13 || c == 10 || c == 12)) {                                                                                        // Store to array
      NMEA_BUFFER[nmea_counter++] = c;
    }
  }
  return false;
}





// --------------------------------- ASCI NUM TO HEX ---------------------------------
byte asci_to_byte (byte asci)
{
  /////////////////////////////// HEX RANGE - ABCDEF  ///////////////////////////////
  if ((asci > 64) && (asci < 71)) {
    asci = asci - 55;
    return  asci;
  }
  /////////////////////////////// DEC RANGE - 0123456789  ///////////////////////////////
  else if ((asci > 48) && (asci < 58)) {
    asci = asci - 48;
    return  asci;
  }
  else return 0;
}





// --------------------------------- Wideband ---------------------------------
int read_wideband() {
  uint16_t  afr = map(analogRead(wb_analog), 0, 1023, 1000, 1998);
  return afr;
}

// --------------------------------- Create Filename ---------------------------------
boolean create_filename(char * DATA) {
  memset(FILENAME, 0, sizeof(FILENAME));
  //  memcpy(FILENAME + strlen(FILENAME), DATE, 4);
  //  strcpy(FILENAME + strlen(FILENAME), ".CSV");
  sprintf(FILENAME, "%s%02d.CSV", DATA, FILENUM);
  FILENUM++;
  if (!sd.exists(FILENAME)) {
    return true;
  }
  return false;
}

boolean read_nmea() {
  uint8_t parse_counter = 0;
  memset(GPS_DATE, 0, sizeof(GPS_DATE));
  if (memcmp(NMEA, "$GPRMC", 5) == 0) {
    for (int x = 0; x < strlen(NMEA); x++) {
      if (NMEA[x] == ',') {
        parse_counter++;
      }
      if (NMEA[x] != ',') {
        if (parse_counter == 9) {
          if (strlen(GPS_DATE) <= GPS_DATESIZE) {
            GPS_DATE[strlen(GPS_DATE)] = NMEA[x];
          }
          //        FILENAME[strlen(FILENAME)] = NMEA[x];
        }
      }
    }
    return true;
  }
  return false;
}


// --------------------------------- LED ---------------------------------
void toggleLed() {
  digitalWrite (LED, !digitalRead(LED));
}

