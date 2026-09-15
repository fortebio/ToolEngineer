#include "displayLCD.h"
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include "displayresources.h"

#define Forte_Green RGB565(32, 151, 194)
#define ORANGE_EMS RGB565(255, 71, 32)
#define VIOLET 0xA81F

String measure_value = "";
String Sample_measure = "";

extern String id_BLE;
extern String password;
extern String ssid;
extern String id_device;

bool butt = 1; // 0: blue, 1: green

displayLCD::displayLCD(/* args */)
{
  this->bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO);
  this->display = new Arduino_ILI9341(this->bus, TFT_RESET);
  // this->display = new Arduino_ST7789(this->bus, TFT_RESET);
}

displayLCD::~displayLCD()
{
}

void displayLCD::begin()
{
  this->display->begin();
  // this->display->invertDisplay(true);
  // initMenuSetting(display);
  this->display->fillScreen(BLACK);
  this->display->setRotation(1);
  this->display->setUTF8Print(true);
  this->display->setTextWrap(false);

  this->logoFortebiotech();

  // this->language = Chinese;

  this->type_infor = escreenStart;
  this->changeScreen = true;
}

void displayLCD::configFont()
{
  switch (this->language)
  {
  case VietNamese:
  case English:
  {
    this->display->setFont(u8g2_font_unifont_t_vietnamese2);
    break;
  }
  case Chinese:
  case Taiwanese:
  {
    this->display->setFont(u8g2_font_wqy14_t_gb2312);
    break;
  }
  }
}

// Kích thước màn hình
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

// Lấy chiều cao font hiện tại
uint16_t getFontHeight(language_pointer language, uint8_t fontSize)
{
  int16_t x1, y1;
  uint16_t w, h;
  switch (language)
  {
  case VietNamese:
  {
    _displayLCD.display->getTextBounds("Ấ", 0, 0, &x1, &y1, &w, &h);
    break;
  }
  case English:
  {
    _displayLCD.display->getTextBounds("A", 0, 0, &x1, &y1, &w, &h);
    break;
  }
  case Chinese:
  case Taiwanese:
  {
    _displayLCD.display->getTextBounds("始", 0, 0, &x1, &y1, &w, &h);
    break;
  }
  }
  return (fontSize == 2) ? (h + 8) : (h + 2);
}

uint16_t getTextWidth(String text)
{
  int16_t x1, y1;
  uint16_t w, h;
  _displayLCD.display->getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
  return w;
}

// In chuỗi tự xuống dòng
void drawWrappedText(const char *text, int16_t x, int16_t y, int16_t maxWidth, language_pointer language, uint8_t fontSize)
{
  int16_t cursorX = x;
  int16_t cursorY = y;

  _displayLCD.display->setTextSize(fontSize);
  uint16_t fontH = getFontHeight(language, fontSize);
  String buffer = "";

  while (*text)
  {
    buffer += *text;

    if (*text == ' ' || *(text + 1) == '\0')
    {
      int16_t bx = 0, by = 0;
      uint16_t bw = 0, bh = 0;
      buffer += ' ';
      _displayLCD.display->getTextBounds(buffer.c_str(), cursorX, cursorY, &bx, &by, &bw, &bh);
      if (cursorX + bw > maxWidth)
      {
        cursorX = x;
        cursorY += fontH;
      }

      _displayLCD.display->setCursor(cursorX, cursorY);
      _displayLCD.display->print(buffer);

      cursorX += bw;
      buffer = "";
    }

    text++;
  }
}

void drawLeftText(const char *text, int16_t x, int16_t y, language_pointer language, uint8_t fontSize)
{
  int16_t cursorX;
  int16_t cursorY = y;
  int16_t bx, by;
  uint16_t bw, bh;
  _displayLCD.display->getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  _displayLCD.display->setTextSize(fontSize);
  cursorX = 302 - bw;
  _displayLCD.display->setCursor(cursorX, cursorY);
  _displayLCD.display->print(text);
}

int16_t displayLCD::getCursorX_textRight(const char *text, language_pointer language, uint8_t fontSize)
{
  int16_t bx, by;
  uint16_t bw, bh;
  this->display->getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  return (320 - (bw + bx) - 10);
}

int16_t displayLCD::getCursorX_textLeft(const char *text, language_pointer language, uint8_t fontSize)
{
  int16_t bx, by;
  uint16_t bw, bh;
  this->display->getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  return (20 - bx);
}

int16_t displayLCD::getCursorX_textCenter(const char *text, language_pointer language, uint8_t fontSize)
{
  int16_t bx, by;
  uint16_t bw, bh;
  this->display->getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  return ((320 - bw) / 2 + 10);
}

void drawText(const char *text, int16_t x, int16_t y, uint8_t fontSize)
{
  int16_t cursorX = x;
  int16_t cursorY = y;
  _displayLCD.display->setTextSize(fontSize);
  _displayLCD.display->setCursor(cursorX, cursorY);
  _displayLCD.display->print(text);
}

void displayLCD::drawFrameDisplay(const char *title)
{
  // this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
  // this->display->fillRect(118, 0, 108, 20, Forte_Green);
  // this->display->setFont(u8g2_font_unifont_t_vietnamese2);
  // this->display->setTextSize(1);
  // this->display->setTextColor(BLACK);
  // this->display->setCursor(120, 15);
  // this->display->println("FORTE BIOTECH");
  this->display->drawBitmap(18, 5, logoFBT, 35, 34, Forte_Green);

  configFont();
  this->display->setTextSize(2);
  this->display->setTextColor(WHITE);
  this->display->setCursor(getCursorX_textCenter(title, this->language, 2), 55);
  this->display->print(title);
}

void logo(uint8_t x, uint8_t y)
{
  _displayLCD.display->fillTriangle(80 + x, 60 + y, 132 + x, 30 + y, 132 + x, 90 + y, _displayLCD.display->color565(16, 55, 50));
  _displayLCD.display->fillTriangle(130 + x, 100 + y, 78 + x, 70 + y, 78 + x, 130 + y, _displayLCD.display->color565(16, 55, 50));
  _displayLCD.display->fillTriangle(80 + x, 140 + y, 132 + x, 110 + y, 132 + x, 170 + y, _displayLCD.display->color565(16, 45, 20));
  _displayLCD.display->fillTriangle(142 + x, 30 + y, 252 + x, 10 + y, 142 + x, 68 + y, _displayLCD.display->color565(10, 30, 35));
  _displayLCD.display->fillTriangle(142 + x, 140 + y, 142 + x, 170 + y, 192 + x, 130 + y, _displayLCD.display->color565(16, 65, 30));
  _displayLCD.display->setFont(u8g2_font_unifont_t_vietnamese2);
  _displayLCD.display->setTextSize(2);
  _displayLCD.display->setTextColor(_displayLCD.display->color565(16, 55, 70));
  _displayLCD.display->setCursor(150 + x, 95 + y);
  _displayLCD.display->print("FORTE");
  _displayLCD.display->setCursor(150 + x, 125 + y);
  _displayLCD.display->print("BIOTECH");
}

void displayLCD::logoFortebiotech()
{
  this->display->fillScreen(BLACK);
  logo(0, 0);

  this->display->setFont(u8g2_font_helvB08_tf);
  this->display->setTextColor(Forte_Green);
  this->display->setTextSize(1);
  this->display->setCursor(70, 195);
  this->display->print("TEST   PRAWNS   WITH   RAPID");
  this->display->setCursor(100, 210);
  this->display->print("PROFIT   NO   LIMIT");
  this->display->setCursor(20, 230);
  this->display->print(FirmwareVer);
  delay(3000);
}

void displayLCD::screen_Start()
{
  ip = WiFi.localIP().toString().c_str(); // Taking ip address
  EEPROM.begin(_EEPROM_SIZE);
  EEPROM.put(ADDR_CHECK_INITIALIZE_SAMPLE, animals_RD);
  EEPROM.put(ADDR_LANGUAGE, _displayLCD.language);
  EEPROM.commit();
  EEPROM.end();
  animals = readAnimalEeprom(ADDR_SAMPLE_DATA(animals_RD));
  this->display->fillScreen(BLACK);
  drawFrameDisplay(" ");
  this->display->drawBitmap(275, 200, shrimp, 35, 29, Forte_Green);

  this->display->drawRoundRect(20, 60, 290, 100, 20, GREEN);
  this->display->drawCircle(47, 110, 25, GREEN);
  this->display->fillCircle(47, 110, 20, GREEN);
  this->display->setTextColor(GREEN);
  if (this->language == Chinese ||
      this->language == Taiwanese)
    drawWrappedText(language_RD[this->language][SCREEN_START], 80, 120, SCREEN_HEIGHT, this->language, 2);
  else
    drawWrappedText(language_RD[this->language][SCREEN_START], 80, 105, SCREEN_HEIGHT, this->language, 2);

  this->display->setTextColor(WHITE);
  this->display->setTextSize(1);
  this->display->setCursor(20, 230);
  this->display->print(ip);
}

void displayLCD::Choose_tube()
{
  /*bug don't touch*/
  this->display->fillScreen(BLACK);
  delete menu;
  uint8_t numberItem = 0;
  uint16_t x, y;

  if (animals.items.numberSick > 2)
  {
    x = 20;
    y = 75;
    numberItem = 3;
  }
  else
  {
    x = 20;
    y = 90;
    numberItem = animals.items.numberSick;
  }
  this->menu = new Menu(display, x, y, 280, 120, numberItem, animals.items.numberSick, MENU_SICK, this->language);

  /* Thiết lập menu */
  switch (animals_RD)
  {
  case GROUP_PRAWN:
  {
    menu->setItem(PRAWN_SICK_PC, language_sickName_positive[this->language], "", iconEmpty, 1);
    menu->setItem(PRAWN_SICK_EHP, "EHP", "", iconEmpty, 1);
    menu->setItem(PRAWN_SICK_EMS, "EMS", "", iconEmpty, 1);
    menu->setItem(PRAWN_SICK_WSSV, language_sickName_WSSV[this->language], "", iconEmpty, 1);
    menu->setItem(PRAWN_SICK_TPD, "TPD", "", iconEmpty, 1);
    break;
  }
  case GROUP_FISH:
  {
    menu->setItem(FISH_SICK_PC, language_sickName_positive[this->language], "", iconEmpty, 1);
    menu->setItem(FISH_SICK_ISKNV, "ISKNV", "", iconEmpty, 1);
    menu->setItem(FISH_SICK_TILV, "TILV", "", iconEmpty, 1);
    break;
  }
  case GROUP_PIG:
  {
    menu->setItem(PIG_SICK_PC, "PC", "", iconEmpty, 1);
    menu->setItem(PIG_SICK_ASF, "ASF", "", iconEmpty, 1);
    break;
  }
  case GROUP_CHICKEN:
  {
    menu->setItem(CHICKEN_SAMPLE_EMPTY1, ".", "", iconEmpty, 1);
    menu->setItem(CHICKEN_SAMPLE_EMPTY2, "..", "", iconEmpty, 1);
    menu->setItem(CHICKEN_SAMPLE_EMPTY3, "...", "", iconEmpty, 1);
    break;
  }
  }
  /****** */

  this->display->setTextSize(1);
  this->display->setTextColor(RED);
  this->display->setCursor(getCursorX_textRight(redMenu_feature[this->language], this->language, 1), 230);
  this->display->print(redMenu_feature[this->language]);
  this->display->setTextColor(GREEN);
  this->display->setCursor(getCursorX_textLeft(greenMenu_feature[this->language], this->language, 1), 230);
  this->display->print(greenMenu_feature[this->language]);
  this->display->setTextColor(WHITE);
  this->display->setCursor(getCursorX_textRight(whiteMenu_feature[this->language], this->language, 1), 210);
  this->display->print(whiteMenu_feature[this->language]);

  drawFrameDisplay(language_RD[this->language][CHOOSE_TUBE_MODE]);
  menu->drawMenu();
}

void displayLCD::Choose_sample()
{
  if (flag_groupData)
  {
    displayGroupData();
    flag_groupData = false;
    delay(2000);
  }
  this->display->fillScreen(BLACK);
  delete menu;
  uint8_t numberItem = 0;
  uint16_t x, y;

  if (animals.items.numberSample > 2)
  {
    x = 20;
    y = 75;
    numberItem = 3;
  }
  else
  {
    x = 20;
    y = 90;
    numberItem = animals.items.numberSample;
  }

  this->menu = new Menu(display, x, y, 280, 120, numberItem, animals.items.numberSample, MENU_SAMPLE);
  /* Thiết lập menu */
  switch (animals_RD)
  {
  case GROUP_PRAWN:
  {
    menu->setItem(PRAWN_SAMPLE_VANNAMEI, language_sampleVannamei[this->language], "", iconVannamei, 1);
    menu->setItem(PRAWN_SAMPLE_MONODON, language_sampleMonodon[this->language], "", iconMonodon, 1);
    menu->setItem(PRAWN_SAMPLE_WATER, language_sampleWater[this->language], "", iconWater, 1);
    break;
  }
  case GROUP_FISH:
  {
    menu->setItem(FISH_SAMPLE_FISH, "Tilapia", "", iconTilapia, 1);
    menu->setItem(FISH_SAMPLE_WATER, "Water", "", iconWater, 1);
    break;
  }
  case GROUP_PIG:
  {
    menu->setItem(PIG_SAMPLE_BLOOD, "Blood", "", iconWater, 1);
    menu->setItem(PIG_SAMPLE_FECES, "Feces", "", iconPig, 1);
    menu->setItem(PIG_SAMPLE_SALIVA, "Saliva", "", iconWater, 1);
    break;
  }
  case GROUP_CHICKEN:
  {
    menu->setItem(CHICKEN_SAMPLE_EMPTY1, "Empty1", "", iconVannamei, 1);
    menu->setItem(CHICKEN_SAMPLE_EMPTY2, "Empty2", "", iconMonodon, 1);
    menu->setItem(CHICKEN_SAMPLE_EMPTY3, "Empty3", "", iconWater, 1);
    break;
  }
  }
  /****** */

  this->display->setTextSize(1);
  this->display->setTextColor(RED);
  this->display->setCursor(getCursorX_textRight(redMenu_feature[this->language], this->language, 1), 230);
  this->display->print(redMenu_feature[this->language]);
  this->display->setTextColor(GREEN);
  this->display->setCursor(getCursorX_textLeft(greenMenu_feature[this->language], this->language, 1), 230);
  this->display->print(greenMenu_feature[this->language]);
  this->display->setTextColor(WHITE);
  this->display->setCursor(getCursorX_textRight(whiteMenu_feature[this->language], this->language, 1), 210);
  this->display->print(whiteMenu_feature[this->language]);

  this->display->setTextColor(RGB565(125,255,155));
  if (flag_groupState)
  {
    this->display->setCursor(getCursorX_textLeft(greenHold_Ugroup[this->language], this->language, 1), 210);
    this->display->print(greenHold_Ugroup[this->language]);
  }
  else if (!flag_groupState)
  {
    this->display->setCursor(getCursorX_textLeft(greenHold_Group[this->language], this->language, 1), 210);
    this->display->print(greenHold_Group[this->language]);
  }

  drawFrameDisplay(language_RD[this->language][CHOOSE_SAMPLE_MODE]);
  menu->drawMenu();
}

void displayLCD::Choose_Animals()
{
  this->display->fillScreen(BLACK);
  delete menu;
  this->menu = new Menu(display, 20, 75, 280, 120, 3, GROUP_COUNT, MENU_ANIMALS);
  /* Thiết lập menu */
  menu->setItem(GROUP_PRAWN, language_RD[this->language][CHOOSE_ANIMAL_PRAWN], "", iconVannamei, 1);
  menu->setItem(GROUP_FISH, language_RD[this->language][CHOOSE_ANIMAL_FISH], "", iconTilapia, 1);
  menu->setItem(GROUP_PIG, language_RD[this->language][CHOOSE_ANIMAL_PIG], "", iconPig, 1);
  // menu->setItem(GROUP_CHICKEN, language_RD[this->language][CHOOSE_ANIMAL_PIG], "", iconPig, 1);
  /****** */

  this->display->setTextSize(1);
  this->display->setTextColor(RED);
  this->display->setCursor(getCursorX_textRight(redMenu_feature[this->language], this->language, 1), 230);
  this->display->print(redMenu_feature[this->language]);
  this->display->setTextColor(GREEN);
  this->display->setCursor(getCursorX_textLeft(greenMenu_feature[this->language], this->language, 1), 230);
  this->display->print(greenMenu_feature[this->language]);
  this->display->setTextColor(WHITE);
  this->display->setCursor(getCursorX_textRight(whiteMenu_feature[this->language], this->language, 1), 210);
  this->display->print(whiteMenu_feature[this->language]);

  drawFrameDisplay(language_RD[this->language][CHOOSE_SAMPLE_MODE]);
  menu->drawMenu();
}

void displayLCD::waiting_Readsensor()
{
  this->display->fillScreen(BLACK);
  drawFrameDisplay(" ");
  this->display->setTextSize(3);
  this->display->setTextColor(RED);
  this->display->setCursor(20, 90);
  this->display->print(language_RD[this->language][WAITING_READ_SENSOR]);
  this->display->setTextColor(Forte_Green);
  drawWrappedText(language_RD[this->language][WAITING_PROCESS], 20, 150, (SCREEN_HEIGHT), this->language, 2);

  for (int i = 13; i < 265; ++i)
  {
    this->display->setCursor(i + 5, 210);
    this->display->print(".");
    delay(2);
  }
  _sensor.flagReadSensor = true;
}

void displayLCD::prepare()
{
  this->display->fillScreen(BLACK);
  // this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
  this->display->setTextColor(Forte_Green);
  drawWrappedText(language_RD[this->language][PREPARE_PUT_TUBE], 35, 45, SCREEN_HEIGHT, this->language, 2);

  for (int i = 18; i <= 300; i += 10)
  {
    static int x1 = 0, y1 = 100, x2 = 10, y2 = 110, y3 = 120;
    this->display->drawLine(x1 + i, y1, x2 + i, y2, PINK);
    this->display->drawLine(x1 + i, y3, x2 + i, y2, PINK);
  }

  this->display->drawRoundRect(30, 140, 272, 80, 8, RED);
  this->display->drawRoundRect(29, 139, 274, 82, 10, RED);
  this->display->drawCircle(55, 180, 22, RED);
  this->display->fillCircle(55, 180, 17, RED);
  this->display->setTextColor(RED);
  // if (this->language == Chinese ||
  //     this->language == Taiwanese)
  //   drawWrappedText(language_RD[this->language][PREPARE_NOTIFICATION], 90, 190, SCREEN_HEIGHT - 50, this->language, 2);
  // else
  drawWrappedText(language_RD[this->language][PREPARE_NOTIFICATION], 90, 175, SCREEN_HEIGHT - 30, this->language, 2);
}

char displayLCD::compare_result(uint32_t result)
{
  if (result >= animals.items.listSick[_sensor.sick].threshold)
  {
    this->display->setTextColor(RED);
    // this->display->print("+");
    return '+';
  }
  else
  {
    this->display->setTextColor(WHITE);
    // this->display->print("-");
    return '-';
  }
}

void displayLCD::screen_Average_Result()
{
  char resultChar = ' ';
  uint32_t nValue = _sensor.AverageResult[0];

  for (uint8_t i = 0; i < numSampling; i++)
  {
    if (_sensor.sensorError[i] == false)
    {
      resultChar = 'X';
      break;
    }
    resultChar = compare_result(nValue);
  }
  if (resultChar == 'X')
  {
    this->displayErrorProcesssing();
    this->type_infor = e_errorProcessing;
    this->changeScreen = true;
    delay(1000);
  }
  else
  {
    uint8_t x = 15;
    uint8_t y = 120;
    this->display->fillScreen(BLACK);
    // this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
    this->display->drawLine(0, 45, 320, 45, Forte_Green);

    this->display->setTextSize(1);
    this->display->setTextColor(YELLOW);
    for (int i = 0; i < 3; i++)
    {
      this->display->setCursor((x + 10), 170);
      this->display->print("#");
      this->display->print(i + 1);
      this->display->setCursor((x + 10), 200);
      if (_sensor.sensorError[i] == false)
      {

        this->display->println("-");
      }
      else
      {
        this->display->println(_sensor.result_Sensor[0][i]);
      }
      x += 74;
      y += 30;
      this->display->drawLine(x, 150, x, 210, Forte_Green);
      this->display->drawLine(0, y, 320, y, Forte_Green);
    }

    this->display->setTextSize(2);
    this->display->setTextColor(WHITE);
    this->display->setCursor(25, 35);
    this->display->print(language_RD[this->language][RESULT_SICK] + measure_value);

    this->display->setFont(u8g2_font_unifont_t_vietnamese2);
    this->display->setTextSize(7);
    this->display->setTextColor(Forte_Green);
    this->display->setCursor(85, 135);
    this->display->print("[");
    if (resultChar == 'X')
    {
      this->display->setTextColor(RED);
    }
    else
    {
      this->display->setTextColor(Forte_Green);
    }
    this->display->print(resultChar);
    this->display->setTextColor(Forte_Green);
    this->display->print("]");
    configFont();

    this->display->setTextSize(1);
    this->display->setTextColor(YELLOW);
    this->display->setCursor((3 * 74) + 25, 170);
    this->display->print(language_RD[this->language][RESULT_AVERAGE]);
    this->display->setCursor((3 * 74) + 25, 200);
    if (resultChar == 'X')
    {
      this->display->println('-');
    }
    else
    {
      this->display->println(nValue);
    }

    blockSend = false;
    postData_GoogleSheet(_sensor.sick, _sensor.result_Sensor[0][0], _sensor.result_Sensor[0][1], _sensor.result_Sensor[0][2], id_device);

    this->display->setTextColor(RED);
    drawLeftText(language_RD[this->language][RESULT_FEATURE_RED], 0, 230, this->language, 1);

    this->display->setTextColor(GREEN);
    drawText(language_RD[this->language][RESULT_FEATURE_GREEN], 20, 230, 1);
  }
}

void displayLCD::screen_Calib()
{
  String s;
  this->display->fillScreen(BLACK);
  this->display->setTextColor(WHITE);
  this->drawFrameDisplay(language_RD[this->language][CALIB_MODE]);
  this->display->setCursor(25, 120);
  this->display->print(language_RD[this->language][CALIB_SAMPLE]);

  this->display->setTextColor(RED);
  if (_sensor.typecalib == 0)
  {
    s = language_RD[this->language][CALIB_SAMPLE_MAX];
  }
  else
  {
    s = language_RD[this->language][CALIB_SAMPLE_MIN];
  }
  // if (this->language == Chinese || this->language == Taiwanese)
  //   this->display->fillRoundRect(180, 90, getTextWidth(s) + 20, 40, 10, Forte_Green);
  // else
  this->display->fillRoundRect(145, 90, getTextWidth(s) + 20, 40, 10, Forte_Green);
  this->display->print(s);

  this->display->setTextSize(1);
  this->display->setTextColor(GREEN);
  this->display->setCursor(getCursorX_textLeft(language_RD[this->language][CALIB_FEATURE_GREEN], this->language, 1), 190);
  this->display->print(language_RD[this->language][CALIB_FEATURE_GREEN]);
  this->display->setTextColor(RED);
  this->display->setCursor(getCursorX_textLeft(language_RD[this->language][CALIB_FEATURE_RED], this->language, 1), 210);
  this->display->print(language_RD[this->language][CALIB_FEATURE_RED]);
  this->display->setTextColor(WHITE);
  this->display->setCursor(getCursorX_textLeft(language_RD[this->language][CALIB_FEATURE_WHITE], this->language, 1), 230);
  this->display->print(language_RD[this->language][CALIB_FEATURE_WHITE]);

  this->display->setTextColor(YELLOW);
  this->display->setCursor(getCursorX_textRight("00000000", this->language, 1), 210);
  this->display->print(_sensor.valueCalibMin);
  this->display->setTextColor(YELLOW);
  this->display->setCursor(getCursorX_textRight("00000000", this->language, 1), 230);
  this->display->print(_sensor.valueCalibMax);
}

void displayLCD::displayErrorProcesssing()
{
  this->display->fillScreen(BLACK);
  drawFrameDisplay(" ");

  this->display->drawRoundRect(20, 60, 290, 100, 20, RED);
  this->display->drawCircle(47, 110, 25, RED);
  this->display->fillCircle(47, 110, 20, RED);
  this->display->setTextColor(RED);
  if (this->language == Chinese ||
      this->language == Taiwanese)
    drawWrappedText(language_RD[this->language][RESULT_ERROR], 80, 120, SCREEN_HEIGHT, this->language, 2);
  else
    drawWrappedText(language_RD[this->language][RESULT_ERROR], 80, 105, SCREEN_HEIGHT, this->language, 2);

  this->display->setTextColor(DARKGREY);
  drawWrappedText(language_RD[this->language][RESULT_NOISE_CONTENT], 20, 180, SCREEN_HEIGHT - 20, this->language, 1);
}

void displayLCD::waiting_Calib()
{
  this->display->fillScreen(BLACK);
  this->drawFrameDisplay("");
  this->display->setTextColor(WHITE);
  drawWrappedText(language_RD[this->language][CALIB_WAITING_TITILE], 45, 65, SCREEN_HEIGHT, this->language, 2);
  this->display->setTextColor(RED);
  drawWrappedText(language_RD[this->language][CALIB_WAITING_PLEASE], 25, 150, SCREEN_HEIGHT, this->language, 2);
}
void displayLCD::screen_Calib_Complete()
{
  this->display->fillScreen(BLACK);
  this->display->setTextColor(WHITE);
  this->drawFrameDisplay(language_RD[this->language][CALIB_MODE]);
  this->display->setTextColor(ORANGE);
  this->display->setCursor(45, 130);
  this->display->setTextColor(GREEN);
  this->display->print(language_RD[this->language][CALIB_COMPLETE]);
  delay(3000);
}

void displayLCD::setting()
{
  this->display->fillScreen(BLACK);
  delete menu;
  this->menu = new Menu(display, 20, 75, 280, 120, 3, 5, MENU_SETTING, language);
  /* Thiết lập menu */
  menu->setItem(LANGUAGE, language_RD[this->language][SETTING_LANGUAGE], "", iconLanguage, 3);
  menu->setItem(WIFI, language_RD[this->language][SETTING_WIFI], "", iconWifi, 2);
  menu->setItem(UPDATE, language_RD[this->language][SETTING_UPDATE], "", iconUpdate, 1);
  menu->setItem(THRESHOLD, language_RD[this->language][SETTING_THRESHOLD], "", iconThreshold, 1);
  menu->setItem(SAMPLE, language_RD[this->language][SETTING_SAMPLE], "", iconSample, GROUP_COUNT);
  /**
   * @brief QC Reader
   * Qc sensor
   *
   */
  // menu->setItem(SAMPLE, language_RD[this->language][SETTING_SAMPLE], "", iconSample, GROUP_COUNT);
  /****** */

  this->display->setTextSize(1);
  this->display->setTextColor(RED);
  this->display->setCursor(getCursorX_textRight(redMenu_feature[this->language], this->language, 1), 230);
  this->display->print(redMenu_feature[this->language]);
  this->display->setTextColor(GREEN);
  this->display->setCursor(getCursorX_textLeft(greenMenu_feature[this->language], this->language, 1), 230);
  this->display->print(greenMenu_feature[this->language]);
  this->display->setTextColor(WHITE);
  this->display->setCursor(getCursorX_textRight(whiteMenu_feature[this->language], this->language, 1), 210);
  this->display->print(whiteMenu_feature[this->language]);

  drawFrameDisplay(language_RD[this->language][SETTING_MODE]);
  menu->drawMenu();
}

void displayLCD::setting_threshold()
{
  this->display->fillScreen(BLACK);
  drawFrameDisplay(language_RD[this->language][THRESHOLD_MODE]);

  this->display->drawRoundRect(18, 70, 296, 50, 10, GREEN);
  this->display->setCursor(40, 110);
  this->display->setTextColor(GREEN);
  this->display->print(language_RD[this->language][THRESHOLD_MANUAL]);
  this->display->drawRoundRect(18, 130, 296, 50, 10, RED);
  this->display->setCursor(40, 170);
  this->display->setTextColor(RED);
  this->display->print(language_RD[this->language][THRESHOLD_WIRELESS]);

  this->display->setTextSize(1);
  this->display->setTextColor(GREEN);
  this->display->setCursor(getCursorX_textLeft(language_RD[this->language][THRESHOLD_FEATURE_GREEN], this->language, 1), 230);
  this->display->print(language_RD[this->language][THRESHOLD_FEATURE_GREEN]);
  this->display->setTextColor(RED);
  this->display->setCursor(getCursorX_textLeft(language_RD[this->language][THRESHOLD_FEATURE_RED], this->language, 1), 210);
  this->display->print(language_RD[this->language][THRESHOLD_FEATURE_RED]);
  this->display->setTextColor(WHITE);
  this->display->setCursor(getCursorX_textRight(language_RD[this->language][THRESHOLD_FEATURE_WHITE], this->language, 1), 230);
  this->display->print(language_RD[this->language][THRESHOLD_FEATURE_WHITE]);
}

void displayLCD::set_language()
{
  this->display->fillScreen(BLACK);
  delete menu;
  this->menu = new Menu(display, 20, 75, 280, 120, 3, NUMBER_LANGUAGE, MENU_LANGUAGE, this->language);
  /* Thiết lập menu */
  menu->setItem(VietNamese, language_convert[this->language][VietNamese], "", iconLanguage, 1);
  menu->setItem(English, language_convert[this->language][English], "", iconLanguage, 1);
  menu->setItem(Chinese, language_convert[this->language][Chinese], "", iconLanguage, 1);
  // menu->setItem(Taiwanese, language_convert[this->language][Taiwanese], "", iconLanguage, 1);
  /****** */

  this->display->setTextSize(1);
  this->display->setTextColor(RED);
  this->display->setCursor(getCursorX_textRight(redMenu_feature[this->language], this->language, 1), 230);
  this->display->print(redMenu_feature[this->language]);
  this->display->setTextColor(GREEN);
  this->display->setCursor(getCursorX_textLeft(greenMenu_feature[this->language], this->language, 1), 230);
  this->display->print(greenMenu_feature[this->language]);
  this->display->setTextColor(WHITE);
  this->display->setCursor(getCursorX_textRight(whiteMenu_feature[this->language], this->language, 1), 210);
  this->display->print(whiteMenu_feature[this->language]);

  drawFrameDisplay(language_RD[this->language][LANGUAGE_MODE]);
  menu->drawMenu();
}

void displayLCD::displaySettingWifi()
{
  this->display->fillScreen(BLACK);
  drawFrameDisplay(language_RD[this->language][WIFI_SETTING_MODE]);

  this->display->setTextSize(1);
  this->display->setTextColor(WHITE);
  this->display->drawRoundRect(20, 75, 290, 35, 10, WHITE);
  this->display->setCursor(30, 100);
  this->display->print(language_RD[this->language][WIFI_SETTING_ID] + ssid);
  this->display->drawRoundRect(20, 115, 290, 35, 10, WHITE);
  this->display->setCursor(30, 140);
  this->display->print(language_RD[this->language][WIFI_SETTING_PASSWORD] + password);
  this->display->drawRoundRect(20, 155, 290, 35, 10, WHITE);
  this->display->setCursor(30, 180);
  this->display->print(language_RD[this->language][WIFI_SETTING_ID_DEVICE] + id_device);

  settingWifi();
  saveCredentialsToEEPROM(); // save Wifi ID and password in EEPROM
  loadCredentialsFromEEPROM();

  this->display->fillScreen(BLACK);
  this->display->setTextSize(2);
  this->display->setCursor(25, 120);
  this->display->setTextColor(GREEN);
  this->display->print(language_RD[this->language][WIFI_SETTING_FINISHED]);
  delay(2000);

  this->display->fillScreen(BLACK);
  drawFrameDisplay(language_RD[this->language][WIFI_SETTING_MODE]);
  this->display->setTextSize(1);
  this->display->setTextColor(WHITE);
  this->display->drawRoundRect(20, 75, 285, 35, 10, WHITE);
  this->display->setCursor(25, 95);
  this->display->print(language_RD[this->language][WIFI_SETTING_ID] + ssid);
  this->display->drawRoundRect(20, 115, 285, 35, 10, WHITE);
  this->display->setCursor(25, 135);
  this->display->print(language_RD[this->language][WIFI_SETTING_PASSWORD] + password);
  this->display->drawRoundRect(20, 155, 285, 35, 10, WHITE);
  this->display->setCursor(25, 175);
  this->display->print(language_RD[this->language][WIFI_SETTING_ID_DEVICE] + id_device);
  delay(2000);
  ESP.restart();
}

void displayLCD::displaySettingUpdate()
{
  this->display->fillScreen(BLACK);
  drawFrameDisplay(language_RD[this->language][UPDATE_MODE]);

  this->display->setTextSize(2);
  this->display->setCursor(25, 120);
  this->display->setTextColor(GREEN);
  this->display->print(language_RD[this->language][UPDATE_LOADING]);

  settingUpdate();

  this->display->fillScreen(BLACK);
  drawFrameDisplay(language_RD[this->language][UPDATE_MODE]);
  this->display->setTextSize(2);
  this->display->setCursor(25, 120);
  this->display->setTextColor(GREEN);
  this->display->print(language_RD[this->language][UPDATE_FINISHED]);

  delay(2000);
  ESP.restart();
}

void displayLCD::waiting_SettingThreshold()
{
  this->display->fillScreen(BLACK);
  drawFrameDisplay(language_RD[this->language][THRESHOLD_WIRELESS_MODE]);

  this->display->setTextSize(2);
  this->display->setCursor(25, 120);
  this->display->setTextColor(GREEN);
  this->display->print(language_RD[this->language][THRESHOLD_WIRELESS_WAITING]);

  settingThreshold();

  this->display->fillScreen(BLACK);
  drawFrameDisplay(language_RD[this->language][THRESHOLD_WIRELESS_MODE]);
  this->display->setTextSize(2);
  this->display->setCursor(25, 120);
  this->display->setTextColor(GREEN);
  this->display->print(language_RD[this->language][THRESHOLD_WIRELESS_FINSHED]);
  // drawWrappedText(language_RD[this->language][UPDATE_FINISHED], 25, 150, SCREEN_HEIGHT, this->language, 2);
  delay(2000);
  ESP.restart();
}

void drawIconWifi(void)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    _displayLCD.display->drawBitmap(290, 7, image_wifi_connected_bits, 19, 16, WHITE);
  }
  else
    _displayLCD.display->drawBitmap(290, 7, image_wifi_not_connected_bits, 19, 16, WHITE);
}

void displayLCD::loop()
{
  if (this->changeScreen)
  {
    this->configFont();
    switch (this->type_infor)
    {
    case escreenStart:
    {
      dbg_display("escreenStart");
      this->screen_Start();
      _sensor.clear();
      break;
    }
    case ewaitingReadsensor:
    {
      waiting_Readsensor();
      break;
    }
    case eprepare:
    {
      this->prepare();
      break;
    }
    case escreenAverageResult:
    {
      _sensor.Average_Result();
      this->screen_Average_Result();
      _sensor.clear();
      break;
    }
    case e_loginKeyUpdateCalib:
    {
      this->updateKeyCalib();
      break;
    }
    case e_loginCalib:
    {
      // this->updateKeyCalib();
      uint8_t checkCalib = 0;
      EEPROM.begin(_EEPROM_SIZE);
      EEPROM.get(ADDR_CHECK_CALIB, checkCalib);
      EEPROM.end();
      /* Calib lần đầu*/
      if (checkCalib != 0x01)
      {
        this->type_infor = ecalibSensor;
        this->screen_Calib();
      }
      /* Đã calib nên cần nhập mặt khẩu */
      else
      {
        this->type_infor = e_loginKeyUpdateCalib;
        this->updateKeyCalib();
      }
      break;
    }
    case ecalibSensor:
    {
      this->screen_Calib();
      break;
    }
    case echoosetube:
    {
      this->Choose_tube();
      break;
    }
    case echooseSample:
    {
      this->Choose_sample();
      break;
    }
    case echooseAnimals:
    {
      this->Choose_Animals();
      break;
    }
    case e_setting:
    {
      this->setting();
      break;
    }

    case e_settingWifi:
    {
      this->displaySettingWifi();
      break;
    }
    case e_settingUpdate:
    {
      this->displaySettingUpdate();
      break;
    }
    case e_groupData:
    {
      this->displayGroupData();
      break;
    }

    case e_language:
    {
      this->set_language();
      break;
    }
    case e_setThreshold:
    {
      this->setting_threshold();
      break;
    }
    case THRESHOLD_SETTING_BLE:
    {
      this->waiting_SettingThreshold();
      break;
    }
    case THRESHOLD_SETTING_MANUAL:
    {
      this->setupThreshold();
      break;
    }
    case e_updateThreshold:
    {
      this->updateThreshold();
      break;
    }
    }
    if (this->type_infor != e_setting)
      drawIconWifi();
    this->changeScreen = false;
  }
}

// /* coding */
void displayLCD::updateThreshold()
{
  this->display->fillScreen(BLACK);
  drawFrameDisplay(language_RD[this->language][THRESHOLD_UPDATE_MODE]);
  this->display->setFont(u8g2_font_unifont_t_vietnamese2);

  this->display->setTextColor(WHITE);
  this->display->setTextSize(5);
  this->display->fillTriangle((_Threshold.index - 20), 85, (_Threshold.index + 20), 85, _Threshold.index, 95, WHITE);
  this->display->setCursor(85, 160);
  this->display->println(_Threshold.value[0]);
  this->display->setCursor(125, 160);
  this->display->println(_Threshold.value[1]);
  this->display->setCursor(165, 160);
  this->display->println(_Threshold.value[2]);
  this->display->setCursor(205, 160);
  this->display->println(_Threshold.value[3]);
  configFont();

  this->display->setTextSize(1);
  this->display->setTextColor(GREEN);
  this->display->setCursor(getCursorX_textLeft(language_RD[this->language][THRESHOLD_UPDATE_GREEN], this->language, 1), 210);
  this->display->print(language_RD[this->language][THRESHOLD_UPDATE_GREEN]);
  this->display->setTextColor(RED);
  this->display->setCursor(getCursorX_textLeft(language_RD[this->language][THRESHOLD_UPDATE_RED], this->language, 1), 230);
  this->display->print(language_RD[this->language][THRESHOLD_UPDATE_RED]);
  this->display->setTextColor(WHITE);
  this->display->setCursor(getCursorX_textRight(language_RD[this->language][THRESHOLD_UPDATE_WHITE], this->language, 1), 210);
  this->display->print(language_RD[this->language][THRESHOLD_UPDATE_WHITE]);
  this->display->setTextColor(GREEN);
  this->display->setCursor(getCursorX_textRight(language_RD[this->language][THRESHOLD_UPDATE_PRESSGREEN], this->language, 1), 230);
  this->display->print(language_RD[this->language][THRESHOLD_UPDATE_PRESSGREEN]);
}

void displayLCD::updateKeyCalib()
{
  this->display->fillScreen(BLACK);
  drawFrameDisplay(language_RD[this->language][CALIB_LOGIN_MODE]);
  this->display->setFont(u8g2_font_unifont_t_vietnamese2);

  this->display->setTextColor(WHITE);
  this->display->setTextSize(5);
  this->display->fillTriangle((_keyCalib.index - 20), 85, (_keyCalib.index + 20), 85, _keyCalib.index, 95, WHITE);
  this->display->setCursor(85, 160);
  this->display->println(_keyCalib.value[0]);
  this->display->setCursor(125, 160);
  this->display->println(_keyCalib.value[1]);
  this->display->setCursor(165, 160);
  this->display->println(_keyCalib.value[2]);
  this->display->setCursor(205, 160);
  this->display->println(_keyCalib.value[3]);
  configFont();

  this->display->setTextSize(1);
  this->display->setTextColor(GREEN);
  this->display->setCursor(getCursorX_textLeft(language_RD[this->language][CALIB_LOGIN_UPDATE_GREEN], this->language, 1), 210);
  this->display->print(language_RD[this->language][CALIB_LOGIN_UPDATE_GREEN]);
  this->display->setTextColor(RED);
  this->display->setCursor(getCursorX_textLeft(language_RD[this->language][CALIB_LOGIN_UPDATE_RED], this->language, 1), 230);
  this->display->print(language_RD[this->language][CALIB_LOGIN_UPDATE_RED]);
  this->display->setTextColor(WHITE);
  this->display->setCursor(getCursorX_textRight(language_RD[this->language][CALIB_LOGIN_UPDATE_WHITE], this->language, 1), 210);
  this->display->print(language_RD[this->language][CALIB_LOGIN_UPDATE_WHITE]);
  this->display->setTextColor(GREEN);
  this->display->setCursor(getCursorX_textRight(language_RD[this->language][CALIB_LOGIN_UPDATE_HOLDGREEN], this->language, 1), 230);
  this->display->print(language_RD[this->language][CALIB_LOGIN_UPDATE_HOLDGREEN]);
}

void displayLCD::setupThreshold()
{
  this->display->fillScreen(BLACK);
  delete menu;
  uint8_t numberItem = 0;
  uint16_t x, y;
  if (animals.items.numberSick > 2)
  {
    x = 20;
    y = 75;
    numberItem = 3;
  }
  else
  {
    x = 20;
    y = 90;
    numberItem = animals.items.numberSick;
  }
  this->menu = new Menu(display, x, y, 280, 120, numberItem, animals.items.numberSick, MENU_THRESHOLD, this->language);

  /* Thiết lập menu */
  switch (animals_RD)
  {
  case GROUP_PRAWN:
  {
    menu->setItem(PRAWN_SICK_PC, language_sickName_positive[this->language], "", iconEmpty, 1);
    menu->setItem(PRAWN_SICK_EHP, "EHP", "", iconEmpty, 1);
    menu->setItem(PRAWN_SICK_EMS, "EMS", "", iconEmpty, 1);
    menu->setItem(PRAWN_SICK_WSSV, language_sickName_WSSV[this->language], "", iconEmpty, 1);
    menu->setItem(PRAWN_SICK_TPD, "TPD", "", iconEmpty, 1);
    break;
  }
  case GROUP_FISH:
  {
    menu->setItem(FISH_SICK_ISKNV, "ISKNV", "", iconEmpty, 1);
    menu->setItem(FISH_SICK_TILV, "TILV", "", iconEmpty, 1);
    menu->setItem(FISH_SICK_PC, language_sickName_positive[this->language], "", iconEmpty, 1);
    break;
  }
  case GROUP_PIG:
  {
    menu->setItem(PIG_SICK_PC, "PC", "", iconEmpty, 1);
    menu->setItem(PIG_SICK_ASF, "ASF", "", iconEmpty, 1);
    break;
  }
  case GROUP_CHICKEN:
  {
    menu->setItem(CHICKEN_SAMPLE_EMPTY1, ".", "", iconEmpty, 1);
    menu->setItem(CHICKEN_SAMPLE_EMPTY2, "..", "", iconEmpty, 1);
    menu->setItem(CHICKEN_SAMPLE_EMPTY3, "...", "", iconEmpty, 1);
    break;
  }
  }
  /****** */

  this->display->setTextSize(1);
  this->display->setTextColor(RED);
  this->display->setCursor(getCursorX_textRight(redMenu_feature[this->language], this->language, 1), 230);
  this->display->print(redMenu_feature[this->language]);
  this->display->setTextColor(GREEN);
  this->display->setCursor(getCursorX_textLeft(greenMenu_feature[this->language], this->language, 1), 230);
  this->display->print(greenMenu_feature[this->language]);
  this->display->setTextColor(WHITE);
  this->display->setCursor(getCursorX_textRight(whiteMenu_feature[this->language], this->language, 1), 210);
  this->display->print(whiteMenu_feature[this->language]);

  drawFrameDisplay(language_RD[this->language][THRESHOLD_MODE]);
  menu->drawMenu();
}

void displayLCD::displayGroupData()
{
  switch (flag_groupState)
  {
  case START_GROUP_DATA:
  {
    this->display->fillScreen(BLACK);
    this->display->setTextSize(2);
    this->display->setTextColor(GREEN);
    if (language == 0)
    {
      this->display->setCursor(30, 120);
      this->display->print("Bắt Đầu");
      this->display->setCursor(30, 155);
      this->display->print("Nhóm Dữ Liệu...!");
    }
    else
    {
      this->display->setCursor(30, 120);
      this->display->print("Start ");
      this->display->setCursor(30, 155);
      this->display->print("Data Grouping....!");
    }
    postData_StartGroup(id_device);
    break;
  }
  case END_GROUP_DATA:
  {
    this->display->fillScreen(BLACK);
    this->display->setTextSize(2);
    this->display->setTextColor(GREEN);
    if (language == 0)
    {
      this->display->setCursor(30, 120);
      this->display->print("Ngừng Nhóm ");
      this->display->setCursor(30, 155);
      this->display->print("Dữ Liệu.........!");
    }
    else
    {
      this->display->setCursor(30, 120);
      this->display->print("Stop ");
      this->display->setCursor(30, 155);
      this->display->print("Data Grouping....!");
    }
    postData_EndGroup(id_device);

    break;
  }
  }
}

displayLCD _displayLCD;
