#include <Arduino.h>
#include <Wire.h>
#include <Encoder.h>
// Use flash memory as eeprom
#include <FlashAsEEPROM.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

// Load local libraries
#include "scales.cpp"
#include "quantizer.cpp"

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Rotery encoder setting
#define ENCODER_OPTIMIZE_INTERRUPTS // counter measure of noise

#define ENC_PIN_1 3      // rotary encoder left pin
#define ENC_PIN_2 6      // rotary encoder right pin
#define CLK_PIN 7        // Clock input pin
#define CV_1_IN_PIN 8    // channel 1 analog in
#define CV_2_IN_PIN 9    // channel 2 analog in
#define ENC_CLICK_PIN 10 // pin for encoder switch
#define ENV_OUT_PIN_1 1
#define ENV_OUT_PIN_2 2

// Declare function prototypes
void noteDisp(int16_t, int16_t, boolean);
void OLED_display();
void intDAC(int);
void MCP(int);
void PWM1(int);
void PWM2(int);
void save();

////////////////////////////////////////////
// ADC calibration. Change these according to your resistor values to make readings more accurate
float AD_CH1_calb = 0.98; // reduce resistance error
float AD_CH2_calb = 0.98; // reduce resistance error
/////////////////////////////////////////

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Encoder myEnc(ENC_PIN_1, ENC_PIN_2); // rotery encoder library setting
float oldPosition = -999;            // rotery encoder library setting
float newPosition = -999;            // rotery encoder library setting
// Amount of menu items
int menuItems = 39;
// i is the current position of the encoder
int i = 1;

bool SW = 0;
bool old_SW = 0;
bool CLK_in = 0;
bool old_CLK_in = 0;
byte mode = 0; // 0=select,1=atk1,2=dcy1,3=atk2,4=dcy2

float AD_CH1, old_AD_CH1, AD_CH2, old_AD_CH2;

int CV_in1, CV_in2;
float CV_out1, CV_out2, old_CV_out1, old_CV_out2;
long gate_timer1, gate_timer2; // EG curve progress speed

int k = 0;

// envelope curve setting
int ad[200] = { // envelope table
    0, 15, 30, 44, 59, 73, 87, 101, 116, 130, 143, 157, 170, 183, 195, 208, 220, 233, 245, 257, 267, 279, 290, 302, 313, 324, 335, 346, 355, 366, 376, 386, 397, 405, 415, 425, 434, 443, 452, 462, 470, 479, 488, 495, 504, 513, 520, 528, 536, 544, 552, 559, 567, 573, 581, 589, 595, 602, 609, 616, 622, 629, 635, 642, 648, 654, 660, 666, 672, 677, 683, 689, 695, 700, 706, 711, 717, 722, 726, 732, 736, 741, 746, 751, 756, 760, 765, 770, 774, 778, 783, 787, 791, 796, 799, 803, 808, 811, 815, 818, 823, 826, 830, 834, 837, 840, 845, 848, 851, 854, 858, 861, 864, 866, 869, 873, 876, 879, 881, 885, 887, 890, 893, 896, 898, 901, 903, 906, 909, 911, 913, 916, 918, 920, 923, 925, 927, 929, 931, 933, 936, 938, 940, 942, 944, 946, 948, 950, 952, 954, 955, 957, 960, 961, 963, 965, 966, 968, 969, 971, 973, 975, 976, 977, 979, 980, 981, 983, 984, 986, 988, 989, 990, 991, 993, 994, 995, 996, 997, 999, 1000, 1002, 1003, 1004, 1005, 1006, 1007, 1008, 1009, 1010, 1012, 1013, 1014, 1014, 1015, 1016, 1017, 1018, 1019, 1020};
int ad1 = 0; // PWM DUTY reference
int ad2 = 0; // PWM DUTY reference
bool ad_trg1 = 0;
bool ad_trg2 = 0;
int atk1, atk2, dcy1, dcy2;                       // attack time,decay time
bool sync1, sync2;                                // 0=sync with trig , 1=sync with note change
int sensitivity_ch1, sensitivity_ch2, oct1, oct2; // sens = AD input attn,amp.oct=octave shift

// CV setting
int cv_qnt_thr_buf1[62]; // input quantize
int cv_qnt_thr_buf2[62]; // input quantize
// Scale and Note loading indexes
int scale_load = 0;
int note_load = 0;
// Note storage
bool note1[12]; // 1=note valid,0=note invalid
bool note2[12];
byte note_str1, note_str11, note_str2, note_str22;

// display
bool disp_refresh = 1; // 0=not refresh display , 1= refresh display , countermeasure of display refresh busy

//-------------------------------Initial setting--------------------------
void setup()
{
  analogWriteResolution(10);
  analogReadResolution(12);
  pinMode(CLK_PIN, INPUT_PULLDOWN);     // CLK in
  pinMode(CV_1_IN_PIN, INPUT);          // IN1
  pinMode(CV_2_IN_PIN, INPUT);          // IN2
  pinMode(ENC_CLICK_PIN, INPUT_PULLUP); // push sw
  pinMode(ENV_OUT_PIN_1, OUTPUT);       // CH1 EG out
  pinMode(ENV_OUT_PIN_2, OUTPUT);       // CH2 EG out
  // OLED initialize
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();

  // I2C connect
  Wire.begin();

  // ADC settings. These increase ADC reading stability but at the cost of cycle time. Takes around 0.7ms for one reading with these
  REG_ADC_AVGCTRL |= ADC_AVGCTRL_SAMPLENUM_1;
  ADC->AVGCTRL.reg = ADC_AVGCTRL_SAMPLENUM_128 | ADC_AVGCTRL_ADJRES(4);

  // read stored data
  if (EEPROM.isValid() == 1)
  { // already writed eeprom
    note_str1 = EEPROM.read(1);
    note_str11 = EEPROM.read(2);
    note_str2 = EEPROM.read(3);
    note_str22 = EEPROM.read(4);
    atk1 = EEPROM.read(5);
    dcy1 = EEPROM.read(6);
    atk2 = EEPROM.read(7);
    dcy2 = EEPROM.read(8);
    sync1 = EEPROM.read(9);
    sync2 = EEPROM.read(10);
    oct1 = EEPROM.read(11);
    oct2 = EEPROM.read(12);
    sensitivity_ch1 = EEPROM.read(13);
    sensitivity_ch2 = EEPROM.read(14);
  }
  else if (EEPROM.isValid() == 0)
  { // no eeprom data , setting any number to eeprom
    note_str1 = 0;
    note_str11 = 0;
    note_str2 = 2;
    note_str22 = 2;
    atk1 = 1;
    dcy1 = 4;
    atk2 = 2;
    dcy2 = 6;
    sync1 = 1;
    sync2 = 1;
    oct1 = 2;
    oct2 = 2;
    sensitivity_ch1 = 4;
    sensitivity_ch2 = 4;
  }
  // setting stored note data
  for (int j = 0; j <= 7; j++)
  {
    note1[j] = bitRead(note_str1, j);
    note2[j] = bitRead(note_str2, j);
  }
  for (int j = 0; j <= 3; j++)
  {
    note1[j + 8] = bitRead(note_str11, j);
    note2[j + 8] = bitRead(note_str22, j);
  }

  // initial quantizer setting
  initializeQuantBuffer(note1, cv_qnt_thr_buf1);
  initializeQuantBuffer(note2, cv_qnt_thr_buf2);
}

void loop()
{
  old_SW = SW;
  old_CLK_in = CLK_in;
  old_CV_out1 = CV_out1;
  old_CV_out2 = CV_out2;
  old_AD_CH1 = AD_CH1;
  old_AD_CH2 = AD_CH2;

  //-------------------------------Rotery endoder--------------------------
  newPosition = myEnc.read();
  if ((newPosition - 3) / 4 > oldPosition / 4)
  { // 4 is resolution of encoder
    oldPosition = newPosition;
    disp_refresh = 1;
    switch (mode)
    {
    case 0:
      i = i - 1;
      if (i < 0)
      {
        i = menuItems;
      }
      break;
    case 1:
      atk1--;
      break;
    case 2:
      dcy1--;
      break;
    case 3:
      atk2--;
      break;
    case 4:
      dcy2--;
      break;
    }
  }
  else if ((newPosition + 3) / 4 < oldPosition / 4)
  { // 4 is resolution of encoder
    oldPosition = newPosition;
    disp_refresh = 1;
    switch (mode)
    {
    case 0:
      i = i + 1;
      if (menuItems < i)
      {
        i = 0;
      }
      break;
    case 1:
      atk1++;
      break;
    case 2:
      dcy1++;
      break;
    case 3:
      atk2++;
      break;
    case 4:
      dcy2++;
      break;
    }
  }
  i = constrain(i, 0, menuItems);
  atk1 = constrain(atk1, 1, 26);
  dcy1 = constrain(dcy1, 1, 26);
  atk2 = constrain(atk2, 1, 26);
  atk2 = constrain(atk2, 1, 26);
  dcy2 = constrain(dcy2, 1, 26);

  //-----------------PUSH SW------------------------------------
  SW = digitalRead(ENC_CLICK_PIN);
  if (SW == 1 && old_SW != 1)
  {
    disp_refresh = 1;
    if (i <= 11 && i >= 0 && mode == 0)
    {
      note1[i] = !note1[i];
    }
    else if (i >= 14 && i <= 25 && mode == 0)
    {
      note2[i - 14] = !note2[i - 14];
    }
    else if (i == 12 && mode == 0)
    {           // CH1 atk setting
      mode = 1; // atk1 setting
    }
    else if (i == 12 && mode == 1)
    { // CH1 atk setting
      mode = 0;
    }
    else if (i == 13 && mode == 0)
    {           // CH1 atk setting
      mode = 2; // dcy1 setting
    }
    else if (i == 13 && mode == 2)
    { // CH1 atk setting
      mode = 0;
    }
    else if (i == 26 && mode == 0)
    {           // CH1 atk setting
      mode = 3; // atk2 setting
    }
    else if (i == 26 && mode == 3)
    { // CH1 atk setting
      mode = 0;
    }
    else if (i == 27 && mode == 0)
    {           // CH1 atk setting
      mode = 4; // dcy2 setting
    }
    else if (i == 27 && mode == 4)
    { // CH1 atk setting
      mode = 0;
    }
    else if (i == 28)
    { // CH1 sync setting
      sync1 = !sync1;
    }
    else if (i == 29)
    { // CH2 sync setting
      sync2 = !sync2;
    }
    else if (i == 30)
    { // CH1 oct setting
      oct1++;
      if (oct1 > 4)
      {
        oct1 = 0;
      }
    }
    else if (i == 31)
    { // CH2 oct setting
      oct2++;
      if (oct2 > 4)
      {
        oct2 = 0;
      }
    }
    else if (i == 32)
    { // CH1 sens setting
      sensitivity_ch1++;
      if (sensitivity_ch1 > 8)
      {
        sensitivity_ch1 = 0;
      }
    }
    else if (i == 33)
    { // CH2 sens setting
      sensitivity_ch2++;
      if (sensitivity_ch2 > 8)
      {
        sensitivity_ch2 = 0;
      }
    }
    else if (i == 34)
    { // Save settings
      save();
    }

    else if (i == 35)
    { // Set Scale for loading avoiding overflow of numScales
      scale_load++;
      if (scale_load > numScales - 1)
      {
        scale_load = 0;
      }
    }
    else if (i == 36)
    { // Set Note for Loading avoiding overflow of 12 notes
      note_load++;
      if (note_load > 11)
      {
        note_load = 0;
      }
    }
    else if (i == 37)
    { // Load Scale into quantizer 1
      buildScale(note_load, scale_load, note1);
    }
    else if (i == 38)
    { // Load Scale into quantizer 2
      buildScale(note_load, scale_load, note2);
    }
    else if (i == 39)
    { // Save settings
      save();
    }

    // select note set
    buildQuantBuffer(note1, cv_qnt_thr_buf1);
    buildQuantBuffer(note2, cv_qnt_thr_buf2);
  }

  //-------------------------------Analog read and qnt setting--------------------------
  AD_CH1 = analogRead(CV_1_IN_PIN) / AD_CH1_calb;
  quantizeCV(AD_CH1, cv_qnt_thr_buf1, sensitivity_ch1, oct1, &CV_out1);

  AD_CH2 = analogRead(CV_2_IN_PIN) / AD_CH2_calb;
  quantizeCV(AD_CH2, cv_qnt_thr_buf2, sensitivity_ch2, oct2, &CV_out2);

  //-------------------------------OUTPUT SETTING--------------------------
  CLK_in = digitalRead(CLK_PIN);

  // trig sync trigger detect
  if (CLK_in == 1 && old_CLK_in == 0)
  {
    if (sync1 == 0)
    {
      ad1 = 0;
      ad_trg1 = 1;
      gate_timer1 = micros();
      if (atk1 == 1)
      {
        ad1 = 200; // no attack time
      }
    }
    if (sync2 == 0)
      ad2 = 0;
    ad_trg2 = 1;
    gate_timer2 = micros();
    if (atk2 == 1)
    {
      ad2 = 200; // no attack time
    }
  }

  // note sync trigger detect
  if (sync1 == 1 && old_CV_out1 != CV_out1)
  {
    ad1 = 0;
    ad_trg1 = 1;
    gate_timer1 = micros();
    if (atk1 == 1)
    {
      ad1 = 200; // no attack time
    }
  }
  if (sync2 == 1 && old_CV_out2 != CV_out2)
  {
    ad2 = 0;
    ad_trg2 = 1;
    gate_timer2 = micros();
    if (atk2 == 1)
    {
      ad2 = 200; // no attack time
    }
  }

  // envelope ch1 out
  if (gate_timer1 + (atk1 - 1) * 200 <= micros() && ad_trg1 == 1 && ad1 <= 199)
  {
    ad1++;
    gate_timer1 = micros();
  }
  else if (gate_timer1 + (dcy1 - 1) * 600 <= micros() && ad_trg1 == 1 && ad1 > 199)
  {
    ad1++;
    gate_timer1 = micros();
  }

  if (ad1 <= 199)
  {
    PWM1(1021 - ad[ad1]);
  }
  else if (ad1 > 199 && ad1 < 399)
  {
    PWM1(ad[ad1 - 200]);
  }
  else if (ad1 >= 399)
  {
    PWM1(1023);
    ad_trg1 = 0;
  }

  // envelope ch2 out
  if (gate_timer2 + (atk2 - 1) * 200 <= micros() && ad_trg2 == 1 && ad2 <= 199)
  {
    ad2++;
    gate_timer2 = micros();
  }
  else if (gate_timer2 + (dcy2 - 1) * 600 <= micros() && ad_trg2 == 1 && ad2 > 199)
  {
    ad2++;
    gate_timer2 = micros();
  }

  if (ad2 <= 199)
  {
    PWM2(1021 - ad[ad2]);
  }
  else if (ad2 > 199 && ad2 < 399)
  {
    PWM2(ad[ad2 - 200]);
  }
  else if (ad2 >= 399)
  {
    PWM2(1023);
    ad_trg2 = 0;
  }

  // DAC OUT
  if (old_CV_out1 != CV_out1)
  {
    intDAC(CV_out1);
  }
  if (old_CV_out2 != CV_out2)
  {
    MCP(CV_out2);
  }

  // display out
  if (disp_refresh == 1)
  {
    OLED_display(); // refresh display
    disp_refresh = 0;
  }
}

void noteDisp(int x0, int y0, boolean on)
{
  int width = 11;
  int height = 13;
  int radius = 2;
  int color = WHITE;
  if (on)
  {
    display.fillRoundRect(x0, y0, width, height, radius, color);
  }
  else
  {
    display.drawRoundRect(x0, y0, width, height, radius, color);
  }
};

//-----------------------------DISPLAY----------------------------------------
void OLED_display()
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  // Draw the keyboard scale 1
  if (i <= 27)
  {
    note1[1] == 0 ? noteDisp(7, 0, 0) : noteDisp(7, 0, 1);
    note1[3] == 0 ? noteDisp(7 + 14 * 1, 0, 0) : noteDisp(7 + 14 * 1, 0, 1);
    note1[6] == 0 ? noteDisp(8 + 14 * 3, 0, 0) : noteDisp(8 + 14 * 3, 0, 1);
    note1[8] == 0 ? noteDisp(8 + 14 * 4, 0, 0) : noteDisp(8 + 14 * 4, 0, 1);
    note1[10] == 0 ? noteDisp(8 + 14 * 5, 0, 0) : noteDisp(8 + 14 * 5, 0, 1);
    note1[0] == 0 ? noteDisp(0, 15, 0) : noteDisp(0, 15, 1);
    note1[2] == 0 ? noteDisp(0 + 14 * 1, 15, 0) : noteDisp(0 + 14 * 1, 15, 1);
    note1[4] == 0 ? noteDisp(0 + 14 * 2, 15, 0) : noteDisp(0 + 14 * 2, 15, 1);
    note1[5] == 0 ? noteDisp(0 + 14 * 3, 15, 0) : noteDisp(0 + 14 * 3, 15, 1);
    note1[7] == 0 ? noteDisp(0 + 14 * 4, 15, 0) : noteDisp(0 + 14 * 4, 15, 1);
    note1[9] == 0 ? noteDisp(0 + 14 * 5, 15, 0) : noteDisp(0 + 14 * 5, 15, 1);
    note1[11] == 0 ? noteDisp(0 + 14 * 6, 15, 0) : noteDisp(0 + 14 * 6, 15, 1);

    // Draw the keyboard scale 2
    note2[1] == 0 ? noteDisp(7, 0 + 34, 0) : noteDisp(7, 0 + 34, 1);
    note2[3] == 0 ? noteDisp(7 + 14 * 1, 0 + 34, 0) : noteDisp(7 + 14 * 1, 0 + 34, 1);
    note2[6] == 0 ? noteDisp(8 + 14 * 3, 0 + 34, 0) : noteDisp(8 + 14 * 3, 0 + 34, 1);
    note2[8] == 0 ? noteDisp(8 + 14 * 4, 0 + 34, 0) : noteDisp(8 + 14 * 4, 0 + 34, 1);
    note2[10] == 0 ? noteDisp(8 + 14 * 5, 0 + 34, 0) : noteDisp(8 + 14 * 5, 0 + 34, 1);
    note2[0] == 0 ? noteDisp(0, 15 + 34, 0) : noteDisp(0, 15 + 34, 1);
    note2[2] == 0 ? noteDisp(0 + 14 * 1, 15 + 34, 0) : noteDisp(0 + 14 * 1, 15 + 34, 1);
    note2[4] == 0 ? noteDisp(0 + 14 * 2, 15 + 34, 0) : noteDisp(0 + 14 * 2, 15 + 34, 1);
    note2[5] == 0 ? noteDisp(0 + 14 * 3, 15 + 34, 0) : noteDisp(0 + 14 * 3, 15 + 34, 1);
    note2[7] == 0 ? noteDisp(0 + 14 * 4, 15 + 34, 0) : noteDisp(0 + 14 * 4, 15 + 34, 1);
    note2[9] == 0 ? noteDisp(0 + 14 * 5, 15 + 34, 0) : noteDisp(0 + 14 * 5, 15 + 34, 1);
    note2[11] == 0 ? noteDisp(0 + 14 * 6, 15 + 34, 0) : noteDisp(0 + 14 * 6, 15 + 34, 1);

    // Draw the selection triangle
    if (i <= 4)
    {
      display.fillTriangle(5 + i * 7, 28, 2 + i * 7, 33, 8 + i * 7, 33, WHITE);
    }
    else if (i >= 5 && i <= 11)
    {
      display.fillTriangle(12 + i * 7, 28, 9 + i * 7, 33, 15 + i * 7, 33, WHITE);
    }
    else if (i == 12)
    {
      if (mode == 0)
      {
        display.drawTriangle(127, 0, 127, 6, 121, 3, WHITE);
      }
      else if (mode == 1)
      {
        display.fillTriangle(127, 0, 127, 6, 121, 3, WHITE);
      }
    }
    else if (i == 13)
    {
      if (mode == 0)
      {
        display.drawTriangle(127, 16, 127, 22, 121, 19, WHITE);
      }
      else if (mode == 2)
      {
        display.fillTriangle(127, 16, 127, 22, 121, 19, WHITE);
      }
    }
    else if (i >= 14 && i <= 18)
    {
      display.fillTriangle(12 + (i - 15) * 7, 33, 9 + (i - 15) * 7, 28, 15 + (i - 15) * 7, 28, WHITE);
    }
    else if (i >= 19 && i <= 25)
    {
      display.fillTriangle(12 + (i - 14) * 7, 33, 9 + (i - 14) * 7, 28, 15 + (i - 14) * 7, 28, WHITE);
    }
    else if (i == 26)
    {
      if (mode == 0)
      {
        display.drawTriangle(127, 32, 127, 38, 121, 35, WHITE);
      }
      else if (mode == 3)
      {
        display.fillTriangle(127, 32, 127, 38, 121, 35, WHITE);
      }
    }
    else if (i == 27)
    {
      if (mode == 0)
      {
        display.drawTriangle(127, 48, 127, 54, 121, 51, WHITE);
      }
      else if (mode == 4)
      {
        display.fillTriangle(127, 48, 127, 54, 121, 51, WHITE);
      }
    }

    // Draw envelope param
    display.setTextSize(1);
    display.setCursor(100, 0); // effect param3
    display.print("ATK");
    display.setCursor(100, 16); // effect param3
    display.print("DCY");
    display.setCursor(100, 32); // effect param3
    display.print("ATK");
    display.setCursor(100, 48); // effect param3
    display.print("DCY");
    display.fillRoundRect(100, 9, atk1 + 1, 4, 1, WHITE);
    display.fillRoundRect(100, 25, dcy1 + 1, 4, 1, WHITE);
    display.fillRoundRect(100, 41, atk2 + 1, 4, 1, WHITE);
    display.fillRoundRect(100, 57, dcy2 + 1, 4, 1, WHITE);
  }

  // Draw config settings
  if (i >= 28 && i <= 34)
  { // draw sync mode setting
    display.setTextSize(1);
    display.setCursor(10, 0);
    display.print("SYNC CH1:");
    display.setCursor(72, 0);
    if (sync1 == 0)
    {
      display.print("TRIG");
    }
    else if (sync1 == 1)
    {
      display.print("NOTE");
    }
    display.setCursor(10, 9);
    display.print("     CH2:");
    display.setCursor(72, 9);
    if (sync2 == 0)
    {
      display.print("TRIG");
    }
    else if (sync2 == 1)
    {
      display.print("NOTE");
    }
    // draw octave shift
    display.setCursor(10, 18);
    display.print("OCT  CH1:");
    display.setCursor(10, 27);
    display.print("     CH2:");
    display.setCursor(72, 18);
    display.print(oct1 - 2);
    display.setCursor(72, 27);
    display.print(oct2 - 2);

    // draw sensitivity
    display.setCursor(10, 36);
    display.print("SENS CH1:");
    display.setCursor(10, 45);
    display.print("     CH2:");
    display.setCursor(72, 36);
    display.print(sensitivity_ch1 - 4);
    display.setCursor(72, 45);
    display.print(sensitivity_ch2 - 4);

    // draw save
    display.setCursor(10, 54);
    display.print("SAVE");

    display.drawTriangle(0, 0 + (i - 28) * 9, 0, 6 + (i - 28) * 9, 7, 3 + (i - 28) * 9, WHITE);
  }
  // draw scale load setting
  const char *scale_name = scaleNames[scale_load];
  const char *note_name = noteNames[note_load];
  if (i >= 35 && i <= 38)
  {
    display.setTextSize(1);
    display.setCursor(10, 0);
    display.print("SCALE:");
    display.setCursor(72, 0);
    display.print(scale_name);
    display.setCursor(10, 9);
    display.print("ROOT:");
    display.setCursor(72, 9);
    display.print(note_name);
    display.setCursor(10, 18);
    display.print("LOAD IN CH1");
    display.setCursor(10, 27);
    display.print("LOAD IN CH2");
    // draw save
    display.setCursor(10, 36);
    display.print("SAVE");

    display.drawTriangle(0, 0 + (i - 35) * 9, 0, 6 + (i - 35) * 9, 7, 3 + (i - 35) * 9, WHITE);
  }
  display.display();
}

//-----------------------------OUTPUT----------------------------------------
void intDAC(int intDAC_OUT)
{
  analogWrite(A0, intDAC_OUT / 4); // "/4" -> 12bit to 10bit
}

void MCP(int MCP_OUT)
{
  Wire.beginTransmission(0x60);
  Wire.write((MCP_OUT >> 8) & 0x0F);
  Wire.write(MCP_OUT);
  Wire.endTransmission();
}

void PWM1(int duty1)
{
  pwm(ENV_OUT_PIN_1, 46000, duty1);
}
void PWM2(int duty2)
{
  pwm(ENV_OUT_PIN_2, 46000, duty2);
}

//-----------------------------store data----------------------------------------
void save()
{ // save setting data to flash memory
  delay(100);
  for (int j = 0; j <= 7; j++)
  { // Convert note setting to bits
    bitWrite(note_str1, j, note1[j]);
    bitWrite(note_str2, j, note2[j]);
  }
  for (int j = 0; j <= 3; j++)
  {
    bitWrite(note_str11, j, note1[j + 8]);
    bitWrite(note_str22, j, note2[j + 8]);
  }

  EEPROM.write(1, note_str1);  // ch1 select note
  EEPROM.write(2, note_str11); // ch1 select note
  EEPROM.write(3, note_str2);  // ch2 select note
  EEPROM.write(4, note_str22); // ch2 select note
  EEPROM.write(5, atk1);
  EEPROM.write(6, dcy1);
  EEPROM.write(7, atk2);
  EEPROM.write(8, dcy2);
  EEPROM.write(9, sync1);
  EEPROM.write(10, sync2);
  EEPROM.write(11, oct1);
  EEPROM.write(12, oct2);
  EEPROM.write(13, sensitivity_ch1);
  EEPROM.write(14, sensitivity_ch2);
  EEPROM.commit();
  display.clearDisplay(); // clear display
  display.setTextSize(2);
  display.setTextColor(BLACK, WHITE);
  display.setCursor(10, 40);
  display.print("SAVED");
  display.display();
  delay(1000);
}
