/*
 * Raspberry Pi internet radio project
 * Arduino based front-end using serial communication, 20x4LCD, potentiometer, encoder, 2x MSGEQ7, PT2322 board
 *
 * @author Andrey Karpov <andy.karpov@gmail.com>
 * @copyright 2013 Andrey Karpov
 */

#include <LiquidCrystal.h>
#include <Encoder.h>
#include <Button.h>
#include <Led.h>
#include <Wire.h>
#include <PT2322.h>
#include <EEPROM.h>

#define ROWS 4 // number of display rows
#define COLS 20 // number of display columns
#define NUM_READINGS 1 // number of analog reading to average it's value
#define SERIAL_SPEED 115200 // serial port speed
#define SERIAL_BUF_LEN 64 // serial reading buffer

#define MSGEQ_STROBE 6 // В6
#define MSGEQ_RESET 5 // D5
#define MSGEQ_OUT_L 2 // A2
#define MSGEQ_OUT_R 3 // A3

#define PT2322_MIN_VOLUME -79 // dB
#define PT2322_MAX_VOLUME 0 // dB
#define PT2322_MIN_TONE -14 // db
#define PT2322_MAX_TONE 14 // db

#define NUM_MODES 5 // number of app modes

#define EEPROM_ADDRESS_OFFSET 400 // address offset to start reading/wring to the EEPROM 

#define DELAY_VOLUME 3000 // volume bar show decay  
#define DELAY_ENCODER 100 // delay between sending encoder changes back to Pi 
#define DELAY_MODE 400 // mode switch debounce delay
#define DELAY_EEPROM 10000 // delay to store to the EEPROM

PT2322 audio; // PT2322 board connected to i2c bus (GND, A4, A5)
LiquidCrystal lcd(8, 9, 13, 12, 11, 10); // lcd connected to D8, D9, D13, D12, D11, D10 pins
Encoder enc(2, 3); // encoder pins A and B connected to D2 and D3 
Button btn(4, PULLUP); // encoder's button connected to GND and D4
Button btn_mode(15, PULLUP); // mode button connected to GND and A1
Led backlight(7); // LCD backlight connected to GND and D7

char buf[SERIAL_BUF_LEN]; // serial buffer
byte index = 0; // current buffer position
char sep = ':';  // incoming command separator

bool buffering = true; // buffering mode, default to On
bool need_enc = false; // need to send encoder value to the Pi backend, default to false
bool need_vol = false; // need to send volume value to the Pi backend, default to false
bool init_done = false; // boot done (pyradio service has been started and sent initial data), default to false

int station = 0; // current station value
int prev_station = 0; // previous encoder value
int max_stations = 0; // max stations value

int vol = 0; // current volume value
int prev_vol = 0; // previous volume value
int max_vol = 100; // max volume value

unsigned long last_vol = 0; // timestamp of last volume changed
unsigned long last_enc = 0; // timestamp of last encoder changed

String t[ROWS]; // LCD buffer
String prev_t[ROWS]; // LCD previous buffer

int eq_L[7] = {0,0,0,0,0,0,0}; // 7-band equalizer values for left channel
int eq_R[7] = {0,0,0,0,0,0,0}; // 7-band equalizer values for right channel

// enum with application states
enum app_mode_e {
  app_mode_station = 0,
  app_mode_eq,
  app_mode_tone_bass,
  app_mode_tone_mid,
  app_mode_tone_treble
};

int mode = app_mode_station; // mode set to default (station display)
bool mode_changed = true; // mode button has been pressed
unsigned long last_mode = 0; // timestamp of last mode changed

int tones[3]; // bass, mid, treble
int prev_tones[3]; // bass, mid, treble
bool need_store = false; // flag if we need to store something to the EEPROM
unsigned long last_tone = 0; // timestamp of last tone change

// custom LCD characters (volume bars)
byte p1[8] = { 0b10000, 0b10000, 0b10000, 0b10100, 0b10100, 0b10000, 0b10000, 0b10000 };
byte p2[8] = { 0b10100, 0b10100, 0b10100, 0b10100, 0b10100, 0b10100, 0b10100, 0b10100 };  
byte p3[8] = { 0b10101, 0b10101, 0b10101, 0b10101, 0b10101, 0b10101, 0b10101, 0b10101 };
byte p4[8] = { 0b00101, 0b00101, 0b00101, 0b00101, 0b00101, 0b00101, 0b00101, 0b00101 };
byte p5[8] = { 0b00001, 0b00001, 0b00001, 0b00101, 0b00101, 0b00001, 0b00001, 0b00001 };
byte p6[8] = { 0b00000, 0b00000, 0b00000, 0b00100, 0b00100, 0b00000, 0b00000, 0b00000 };
byte p7[8] = { 0b00000, 0b00001, 0b00011, 0b00111, 0b00111, 0b00011, 0b00001, 0b00000 };
byte p8[8] = { 0b00000, 0b10000, 0b11000, 0b11100, 0b11100, 0b11000, 0b10000, 0b00000 };

// custom LCD characters (eq bars)
byte e1[8] = { 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b11111, 0b11111 };
byte e2[8] = { 0b00000, 0b00000, 0b00000, 0b11111, 0b11111, 0b00000, 0b11111, 0b11111 };  
byte e3[8] = { 0b11111, 0b11111, 0b00000, 0b11111, 0b11111, 0b00000, 0b11111, 0b11111 };
byte e4[8] = { 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000 };

/**
 * Load custom LCD characters for volume control
 */ 
void loadVolumeCharacters() {
  lcd.createChar(1, p1);
  lcd.createChar(2, p2);
  lcd.createChar(3, p3);
  lcd.createChar(4, p4);
  lcd.createChar(5, p5);
  lcd.createChar(6, p6);
  lcd.createChar(7, p7);
  lcd.createChar(8, p8);
}

/**
 * Load custom LCD characters for graphical equalizer bars
 */
void loadEqualizerCharacters() {
  lcd.createChar(1, e1);
  lcd.createChar(2, e2);
  lcd.createChar(3, e3);
  lcd.createChar(4, e4);
}

/**
 * Arduino setup routine
 */
void setup() {

  // set A0 as input for volume regulator
  pinMode(A0, INPUT);
  
  // set default tones
  for (int i=0; i<3; i++) {
    tones[i] = 0;
    prev_tones[i] = 0;
  }
  
  // restore saved tone values from EEPROM
  restoreTones();
  
  // read volume knob position
  vol = readVolume();
  
  // init PT2322 board
  Wire.begin();
  audio.init();
  delay(100);
  
  // send volume and tones to the PT2322 board
  sendPT2322();
  
  // setup MSGEQ board
  pinMode(MSGEQ_STROBE, OUTPUT);
  pinMode(MSGEQ_RESET, OUTPUT);
  analogReference(DEFAULT);
  pinMode(A2, INPUT);
  pinMode(A3, INPUT);
  
  // setup lcd
  lcd.begin(COLS, ROWS);
  lcd.clear();
  loadVolumeCharacters();  
  
  // setup serial
  Serial.begin(SERIAL_SPEED);
  Serial.flush();
  
  // on backlight    
  backlight.on();
  
  // set default values to display buffers
  t[0] = "";
  t[1] = "";
  t[2] = "";
  t[3] = "";  
  
  // send "init" command to the Raspberry Pi backend
  Serial.println("init");  
}

/**
 * Application mode to handle station display
 * @param unsigned long current - current timestamp
 */
void AppStation(unsigned long current) {
  if (mode_changed) {
    lcd.clear();
    loadVolumeCharacters();
    mode_changed = false;
    prev_t[0] = "__";
    prev_t[1] = "__";
    prev_t[2] = "__";
    prev_t[3] = "__";
    setEncoder(station);
  }
  
  if (init_done) {
    
    station = readEncoder(max_stations);
  
    if (station != prev_station) {
      printStation(station);
      prev_station = station;
      last_enc = current;
      need_enc = true;
    }

    for (int i=0; i<2; i++) {
      if (prev_t[i] != t[i]) { 
        int l = t[i].length();
        if (l < COLS) {
          for (int j=l; j<COLS; j++) {
            t[i] += ' ';
          }
        }
        lcd.setCursor(0,i);
        lcd.print(t[i].substring(0, COLS));
        prev_t[i] = t[i];
    }
  }

  if (current - last_vol <= DELAY_VOLUME) {
      lcd.setCursor(0,2);
      lcd.print("VOLUME ");
      lcd.print(vol);
      lcd.print("  ");
      lcd.setCursor(10,2);
      lcd.print("          ");
      lcd.setCursor(0,3);
      printBar(vol);
      for (int i=0; i<ROWS; i++) {
        prev_t[i] = "___";
      }
    } else {
      for (int i=2; i<ROWS; i++) {
        if (prev_t[i] != t[i]) { 
          int l = t[i].length();
          if (l < COLS) {
            for (int j=l; j<COLS; j++) {
              t[i] += ' ';
            }
          }
          lcd.setCursor(0,i);
          lcd.print(t[i].substring(0, COLS));
          prev_t[i] = t[i];
        }
      }
    }
      
    // send encoder and volume to serial port with a small delay  
    if ((need_enc) && (current - last_enc >= DELAY_ENCODER)) {
      Serial.print(station);
      Serial.print(":");
      Serial.println(100);
      need_enc = false;
    }
  }
}

/**
 * Application mode to display a graphical equalizer (2x 7-band)
 * @param unsigned long current - current timestamp
 */
void AppEq(unsigned long current) {
  if (mode_changed) {
    lcd.clear();
    loadEqualizerCharacters();
    mode_changed = false;
  }
  
  readMsgeq();

  lcd.setCursor(0, ROWS-1);
  lcd.print("L");
  lcd.setCursor(COLS-1, ROWS-1);
  lcd.print("R");
  
  for (int i=0; i<7; i++) {
    int val_L = map(eq_L[i], 0, 1023, 0, 8 * ROWS);
    int val_R = map(eq_R[i], 0, 1023, 0, 8 * ROWS);
    for (int j=0; j<ROWS; j++) {
      int mx = 8*(j+1);
      int v_l = (val_L >= mx) ? 8 : val_L - mx + 8;
      if (v_l < 0) v_l = 0;
      int v_r = (val_R >= mx) ? 8 : val_R - mx + 8;
      if (v_r < 0) v_r = 0;
      
      lcd.setCursor(i + ((COLS == 20) ? 2 : 1), ROWS-j-1);
      if (v_l == 1 || v_l == 2 || v_l == 3) lcd.write(1);
      if (v_l == 4 || v_l == 5 || v_l == 6) lcd.write(2);
      if (v_l == 7 || v_l >= 8) lcd.write(3);
      if (v_l == 0) lcd.write(4);

      lcd.setCursor(i + ((COLS == 20) ? 11 : 8), ROWS-j-1);
      if (v_r == 1 || v_r == 2 || v_r == 3) lcd.write(1);
      if (v_r == 4 || v_r == 5 || v_r == 6) lcd.write(2);
      if (v_r == 7 || v_r >= 8) lcd.write(3);
      if (v_r == 0) lcd.write(4);
    }
    //delay(2);
  }  
}

/**
 * Application mode to control Bass tone
 * @param unsigned long current - current timestamp
 */
void AppToneBass(unsigned long current) {
  if (mode_changed) {
    lcd.clear();
    loadVolumeCharacters();
    mode_changed = false;
    setEncoder(tones[0]);
  }
  tones[0] = readEncoder(100);
  lcd.setCursor(0,2);
  lcd.print("BASS ");
  lcd.print(tones[0]);
  lcd.print("  ");
  lcd.setCursor(0,3);
  printBar(tones[0]);
}

/**
 * Application mode to control Mid tone
 * @param unsigned long current - current timestamp
 */
void AppToneMid(unsigned long current) {
 if (mode_changed) {
    lcd.clear();
    loadVolumeCharacters();
    mode_changed = false;
    setEncoder(tones[1]);
  }
  tones[1] = readEncoder(100);
  lcd.setCursor(0,2);
  lcd.print("MIDDLE ");
  lcd.print(tones[1]);
  lcd.print("  ");
  lcd.setCursor(0,3);
  printBar(tones[1]);
}

/**
 * Application mode to control Treble tone
 * @param unsigned long current - current timestamp
 */
void AppToneTreble(unsigned long current) {
 if (mode_changed) {
    lcd.clear();
    loadVolumeCharacters();
    mode_changed = false;
    setEncoder(tones[2]);
  }
  tones[2] = readEncoder(100);
  lcd.setCursor(0,2);
  lcd.print("TREBLE ");
  lcd.print(tones[2]);
  lcd.print("  ");
  lcd.setCursor(0,3);
  printBar(tones[2]);
}

/**
 * Main application loop
 */ 
void loop() {

  unsigned long current = millis();
    
  vol = readVolume();

  if (btn.isPressed() && current - last_mode >= DELAY_MODE) {
    last_mode = current;
    if (mode == NUM_MODES-1) {
      mode = 0;
    } else {
      mode = mode++;
    }
    mode_changed = true;
  }

  if (abs(vol-prev_vol) > 2) {
    prev_vol = vol;
    last_vol = current;
    need_vol = true;
    sendPT2322();
  }
  
  if (tones[0] != prev_tones[0] || tones[1] != prev_tones[1] || tones[2] != prev_tones[2]) {
    last_tone = current;
    prev_tones[0] = tones[0];
    prev_tones[1] = tones[1];
    prev_tones[2] = tones[2];
    need_store = true;
    sendPT2322();
  }
  
  // store settings in EEPROM with delay to reduce number of EEPROM write cycles
  if (need_store && current - last_tone >= DELAY_EEPROM) {
      storeTones();
      need_store = false;
  }
  
  switch (mode) {
    case app_mode_station:
      AppStation(current);
    break;
    case app_mode_eq:
      AppEq(current);
    break;
    case app_mode_tone_bass:
      AppToneBass(current);
    break;
    case app_mode_tone_mid:
      AppToneMid(current);
    break;
    case app_mode_tone_treble:
      AppToneTreble(current);
    break;

  }
  
   readLine();
   if (!buffering) {
     processInput();
     index = 0;
     buf[index] = '\0';
     buffering = true;
   } 
 
   //delay(20);  
}

/** 
 * Load stored tone control values from the EEPROM (into the tones and prev_tones)
 */
void restoreTones() {
  
  byte value;
  int addr;
  
  // bass / mid / treble
  for (int i=0; i<3; i++) {
    addr = i + EEPROM_ADDRESS_OFFSET;
    value = EEPROM.read(addr);
    // defaults
    if (value < 0 || value > 100) {
      value = 0;
    }
    tones[i] = value;
    prev_tones[i] = value;
  }  
}

/**
 * Store tone value in the EEPROM
 * @param int mode
 */
void storeTone(int mode) {
  int addr = mode + EEPROM_ADDRESS_OFFSET;
  EEPROM.write(addr, tones[mode]);  
}

/**
 * Store tone values in the EEPROM
 */
void storeTones() {
  // bass / treble / balance
  for (int i=0; i<3; i++) {
    storeTone(i);
  }
  int addr;
}

/**
 * Send tone control values to the PT2322
 */
void sendPT2322() {
  audio.masterVolume(map(vol, 0, 100, PT2322_MIN_VOLUME, PT2322_MAX_VOLUME));
  audio.leftVolume(0);
  audio.rightVolume(0);
  audio.centerVolume(-15); // off
  audio.rearLeftVolume(-15); // off
  audio.rearRightVolume(-15); // off
  audio.subwooferVolume(-15); // off
  audio.bass(map(tones[0], 0, 100, PT2322_MIN_TONE, PT2322_MAX_TONE));
  audio.middle(map(tones[1], 0, 100, PT2322_MIN_TONE, PT2322_MAX_TONE));
  audio.treble(map(tones[2], 0, 100, PT2322_MIN_TONE, PT2322_MAX_TONE));
  audio._3DOff(); // 3d
  audio.toneOn(); // tone Defeat
  audio.muteOff(); // mute off
}

/**
 * Read equalizer data from the 2xMSGEQ7 chips into the eq_L and eq_R arrays
 */
void readMsgeq() {
  digitalWrite(MSGEQ_RESET, HIGH);
  digitalWrite(MSGEQ_RESET, LOW);
  for (int i = 0; i < 7; i++) {
    digitalWrite(MSGEQ_STROBE, LOW);
    delayMicroseconds(30);
    eq_L[i] = analogRead(MSGEQ_OUT_L);
    eq_R[i] = analogRead(MSGEQ_OUT_R);
    digitalWrite(MSGEQ_STROBE,HIGH);
  }
  digitalWrite(MSGEQ_RESET, LOW);
  digitalWrite(MSGEQ_STROBE, HIGH);
}

/**
 * Read volume from the potentiometer
 * @return int
 */
int readVolume() {

  int values[NUM_READINGS];
  
  for (int i=0; i<NUM_READINGS; i++) {
    values[i] = map(analogRead(A0), 0, 1023, 0, max_vol);
  }

  int total = 0;
  
  for (int i=0; i<NUM_READINGS; i++) {
    total = total + values[i];
  }
  
  int value = total / NUM_READINGS;
  
  if (value >= max_vol) {
    value = max_vol;
  }
  
  if (value <= 0) {
    value = 0;
  }

  return value;
}

/**
 * Read encoder value with bounds from 0 to max_encoder_value
 * @param int max_encoder_value
 */
int readEncoder(int max_encoder_value) {
  int value = enc.read() / 4;
  if (value > max_encoder_value) {
    value = max_encoder_value;
    setEncoder(max_encoder_value);
  }
  if (value < 0) {
    value = 0;
    setEncoder(0);
  }
  return value;
}

/**
 * Save encoder value
 * @param int value
 */
void setEncoder(int value) {
  enc.write(value * 4);
}

 /**
  * Fill internal buffer with a single line from the serial port 
  *
  * @return void
  */
 void readLine() {
   if (Serial.available())  {
     while (Serial.available()) {
         char c = Serial.read();
         if (c == '\n' || c == '\r' || index >= SERIAL_BUF_LEN) {
           buffering = false;
         } else {
           buffering = true;
           buf[index] = c;
           index++;
           buf[index] = '\0';
         }
     }
   }
 }
 
 /**
  * Routine to compare input line from the serial port and perform a response, if required
  *
  * @return void
  */
 void processInput() {
     String content = String(buf);
  
     int pos = content.indexOf(sep);
     if (content.length() == 0 || pos < 0) return;
  
     String cmd = content.substring(0, pos);
     String arg = content.substring(pos+1);
     int arg_pos = arg.indexOf(sep);
     String arg1 = arg.substring(0, arg_pos);
     String arg2 = arg.substring(arg_pos+1);

     // command T1:<some text> will print text on the first line of the lcd
     if (cmd.compareTo("T1") == 0) {
         t[0] = arg;
     } 

     // command T2:<some text> will print text on the second line of the lcd, if allowed    
     if (cmd.compareTo("T2") == 0) {
         t[1] = arg;
     }

     // command T3:<some text> will print text on the third line of the lcd, if allowed
     if (cmd.compareTo("T3") == 0) {
         t[2] = arg;
     }

     // command T4:<some text> will print text on the fourth line of the lcd, if allowed
     if (cmd.compareTo("T4") == 0) {
         t[3] = arg;
     }
          
     // done init
     if (cmd.compareTo("D") == 0) {
       station = stringToInt(arg1);
       max_stations = stringToInt(arg2);
       printStation(station);
       t[2] = arg1;
       t[3] = arg2;
       init_done = true;
       mode_changed = true;
       need_vol = true;
       prev_vol = -1; // force show volume
     }
 }
 
 /**
  * Conver string object into signed integer value
  *
  * @param String s
  * @return int
  */
 int stringToInt(String s) {
     char this_char[s.length() + 1];
     s.toCharArray(this_char, sizeof(this_char));
     int result = atoi(this_char);     
     return result;
 }

/**
 * Print station name and index into the LCD buffer
 */
void printStation(int s) {
    String station_id = String(s+1);
    String station_count = String(max_stations+1);
    t[1] = station_id + " / " + station_count;
    t[2] = "";
    t[3] = "";
    prev_t[0] = "-";
    prev_t[1] = "-";
    prev_t[2] = "-";
    prev_t[3] = "-";
}

 /** 
  * Pring a progress bar on the current cursor position
  *
  * @param int percent
  */
 void printBar(int percent) {

   double length = COLS + 0.0;
   double value = length/100*percent;
   int num_full = 0;
   double value_half = 0.0;
   int peace = 0;
   
   // fill full parts of progress
   if (value>=1) {
    for (int i=1;i<value;i++) {
      lcd.write(3); 
      num_full=i;
    }
    value_half = value-num_full;
  } else {
    value_half = value;
  }
  
  // fill partial part of progress
  peace=value_half*5;
  
  if (peace > 0 && peace <=5) {
    if (peace == 1 || peace == 2) lcd.write(1);
    if (peace == 3 || peace == 4) lcd.write(2);
    if (peace == 5) lcd.write(3);
  }
  
  // fill spaces
  int spaces = length - num_full;
  if (peace) {
    spaces = spaces - 1;
  }
  for (int i =0;i<spaces;i++) { 
    lcd.write(6);
  }  
 }

