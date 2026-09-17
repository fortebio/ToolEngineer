#include "displayLCD.h"

#define Forte_Green 0x25F8
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
}

displayLCD::~displayLCD()
{
}

void displayLCD::begin()
{
  this->display->begin();
  this->display->fillScreen(BLACK);
  this->display->setRotation(1);
  this->display->setUTF8Print(true);
  this->display->setTextWrap(false);

  this->logoFortebiotech();

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
      this->display->setFont(u8g2_font_unifont_t_chinese4);
      break;
    }
    default:
      break;
  }
}

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

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
  return (fontSize == 2) ? (h + 6) : (h + 2);
}

uint16_t getTextWidth(String text)
{
  int16_t x1, y1;
  uint16_t w, h;
  _displayLCD.display->getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
  return w;
}

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
  this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
  this->display->fillRect(118, 0, 108, 20, Forte_Green);
  this->display->setFont(u8g2_font_unifont_t_vietnamese2);
  this->display->setTextSize(1);
  this->display->setTextColor(BLACK);
  this->display->setCursor(120, 15);
  this->display->println("FORTE BIOTECH");
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
  EEPROM.put(ADDR_LANGUAGE, _displayLCD.language);
  EEPROM.commit();
  EEPROM.end();

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

void displayLCD::choose_tube()
{
    /*bug don't touch*/
  this->display->fillScreen(BLACK);
  delete menu;
  this->menu = new Menu(display, 25, 75, 285, 120, 3, SICK_NUMBER, MENU_PROCESSING);
  /* Thiết lập menu */
  menu->setItem(PC, language_sickName_positive[this->language], "", iconPC, 1);
  menu->setItem(EHP, "EHP", "", iconEHP, 1);
  menu->setItem(EMS, "EMS", "", iconEMS, 1);
  menu->setItem(WSSV, language_sickName_WSSV[this->language], "", iconWSSV, 1);
  menu->setItem(TPD, "TPD", "", iconTPD, 1);
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

void displayLCD::choose_sample()
{
/*bug don't touch*/
#define NUMBER_SAMPLE_MENU 5
  this->display->fillScreen(BLACK);
  delete menu;
  this->menu = new Menu(display, 25, 75, 285, 120, 3, SAMPLE_NUMBER, MENU_SAMPLES);
  /* Thiết lập menu */
  menu->setItem(PRAWN_Vannamei, language_sampleVannamei[this->language], "", iconVannamei, 1);
  menu->setItem(PRAWN_Monodon, language_sampleMonodon[this->language], "", iconMonodon, 1);
  menu->setItem(FISH_Tilapia, language_sampleTilapia[this->language], "", iconTilapia, 1);
  menu->setItem(PIG, language_samplePig[this->language], "", iconPig, 1);
  menu->setItem(WATER, language_sampleWater[this->language], "", iconWater, 1);
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
/*
void displayLCD::choose_Sensor()
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
  this->display->drawRoundRect(15, 40, 302, 170, 0, Forte_Green);

  if (language == 0)
  {
    this->display->setTextSize(2);

    this->display->setTextColor(WHITE);
    this->display->setCursor(50, 30);
    this->display->print("Chọn vị trí đo");

    this->display->setTextColor(Forte_Green);
    this->display->setCursor(60, 70);
    this->display->print("Vị trí 1");

    this->display->setCursor(60, 100);
    this->display->print("Vị trí 2");

    this->display->setCursor(60, 130);
    this->display->print("Vị trí 3");

    this->display->setCursor(60, 165);
    this->display->print("Vị trí 4");

    this->display->setCursor(60, 200);
    this->display->print("Tất cả");

    switch (this->step_slot)
    {
    case 0:
    {
      this->display->fillTriangle(47, 60, 15, 50, 15, 70, RED);
      _sensor.slot = 0;
      break;
    }
    case 1:
    {
      this->display->fillTriangle(47, 90, 15, 80, 15, 100, RED);
      _sensor.slot = 1;
      break;
    }
    case 2:
    {
      this->display->fillTriangle(47, 120, 15, 110, 15, 130, RED);
      _sensor.slot = 2;
      break;
    }
    case 3:
    {
      this->display->fillTriangle(47, 155, 15, 145, 15, 165, RED);
      _sensor.slot = 3;
      break;
    }
    case 4:
    {
      this->display->fillTriangle(47, 190, 15, 180, 15, 200, RED);
      _sensor.slot = 4;
      break;
    }
    default:
      break;
    }
    this->display->setTextSize(1);
    this->display->setTextColor(RED);
    this->display->setCursor(18, 230);
    this->display->print("Kế tiếp");
    this->display->setCursor(190, 230);
    this->display->setTextColor(GREEN);
    this->display->print("Chọn");
  }
  else
  {
    this->display->setTextSize(2);

    this->display->setTextColor(WHITE);
    this->display->setCursor(60, 30);
    this->display->print("Select slot");

    this->display->setTextColor(Forte_Green);
    this->display->setCursor(60, 80);
    this->display->print("Slot 1");

    this->display->setCursor(60, 110);
    this->display->print("Slot 2");

    this->display->setCursor(60, 140);
    this->display->print("Slot 3");

    this->display->setCursor(60, 170);
    this->display->print("Slot 4");

    this->display->setCursor(60, 200);
    this->display->print("All");

    switch (this->step_slot)
    {
    case 0:
    {
      this->display->fillTriangle(47, 70, 15, 60, 15, 80, RED);
      _sensor.slot = 0;
      break;
    }
    case 1:
    {
      this->display->fillTriangle(47, 100, 15, 90, 15, 110, RED);
      _sensor.slot = 1;
      break;
    }
    case 2:
    {
      this->display->fillTriangle(47, 130, 15, 120, 15, 140, RED);
      _sensor.slot = 2;
      break;
    }
    case 3:
    {
      this->display->fillTriangle(47, 160, 15, 150, 15, 170, RED);
      _sensor.slot = 3;
      break;
    }
    case 4:
    {
      this->display->fillTriangle(47, 190, 15, 180, 15, 200, RED);
      _sensor.slot = 4;
      break;
    }
    default:
      break;
    }
    this->display->setTextSize(1);
    this->display->setTextColor(RED);
    this->display->setCursor(18, 230);
    this->display->print("Next");
    this->display->setCursor(190, 230);
    this->display->setTextColor(GREEN);
    this->display->print("Select");
  }
  _displayLCD.step_slot++;
  if (_displayLCD.step_slot == 5)
  {
    _displayLCD.step_slot = 0;
  }
}
 */
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
  this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
  this->display->setTextColor(Forte_Green);
  drawWrappedText(language_RD[this->language][PREPARE_PUT_TUBE], 25, 40, SCREEN_HEIGHT, this->language, 2);

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
  if (this->language == Chinese ||
      this->language == Taiwanese)
    drawWrappedText(language_RD[this->language][PREPARE_NOTIFICATION], 90, 190, SCREEN_HEIGHT, this->language, 2);
  else
    drawWrappedText(language_RD[this->language][PREPARE_NOTIFICATION], 90, 175, SCREEN_HEIGHT, this->language, 2);
}

void displayLCD::compare_result(uint32_t result)
{
  if (result  >= _sensor.valueThreshold[_sensor.sick])
  {
    this->display->setTextColor(RED);
    this->display->print("+");
  }
  else
  {
    this->display->setTextColor(WHITE);
    this->display->print("-");
  }
}

void displayLCD::screen_Average_Result()
{
  this->display->fillScreen(BLACK);
  this->display->drawRoundRect(15, 0, 302, 240, 10, Forte_Green);
  this->display->drawLine(60, 42, 60, 210, Forte_Green);
  this->display->drawLine(190, 42, 190, 210, Forte_Green);
  this->display->drawLine(15, 210, 315, 210, Forte_Green);
  this->display->setTextSize(2);
  this->display->setTextColor(WHITE);
  for (int i = 0; i < 4; i++)
  {
    this->display->drawLine(15, 42 + (i * 42), 315, 42 + (i * 42), Forte_Green);
    this->display->setCursor(30, 75 + (i * 42));
    this->display->println(i + 1);
    this->display->setCursor(90, 75 + (i * 42));
    this->display->println(_sensor.AverageResult[i]);
  }
  for(int i = 0; i < 4; i++)
  {
    this->display->setTextSize(3);
    this->display->setCursor(240, 75 + (i * 45));
    compare_result(_sensor.AverageResult[i]);
  }

  this->display->setTextSize(2);
  this->display->setTextColor(WHITE);
  this->display->setCursor(25, 35);
  this->display->print(language_RD[this->language][RESULT_SICK] + measure_value);
  
  //postData_GoogleSheet(_sensor.sick, _sensor.result_Sensor[0][0], _sensor.result_Sensor[0][1], _sensor.result_Sensor[0][2], id_device);

  this->display->setTextColor(RED);
  drawText(language_RD[this->language][RESULT_FEATURE_RED], 50, 230, 1);

  this->display->setTextColor(GREEN);
  drawText(language_RD[this->language][RESULT_FEATURE_GREEN], 0, 230, 1);
}

void displayLCD::screen_Calib()
{
  String s;
  this->display->fillScreen(BLACK);
  this->display->setTextColor(WHITE);
  this->drawFrameDisplay(language_RD[this->language][CALID_MODE]);
  this->display->setCursor(25, 120);
  this->display->print(language_RD[this->language][CALID_SAMPLE]);

  
  this->display->setCursor(60, 140);
  this->display->print("slot: ");
  this->display->print(_sensor.slot);
  

  this->display->setTextColor(RED);
  if (_sensor.typecalib == 0)
  {
    s = language_RD[this->language][CALID_SAMPLE_MAX];
  }
  else
  {
    s = language_RD[this->language][CALID_SAMPLE_MIN];
    // this->display->print(language_RD[this->language][CALID_SAMPLE_MIN]);
  }
  if (this->language == Chinese || this->language == Taiwanese)
    this->display->fillRoundRect(180, 90, getTextWidth(s) + 20, 40, 10, Forte_Green);
  else
    this->display->fillRoundRect(145, 90, getTextWidth(s) + 20, 40, 10, Forte_Green);
  // this->display->print(language_RD[this->language][CALID_SAMPLE_MAX]);
  this->display->print(s);

  this->display->setTextSize(1);
  this->display->setTextColor(GREEN);
  this->display->setCursor(getCursorX_textLeft(language_RD[this->language][CALID_FEATURE_GREEN], this->language, 1), 190);
  this->display->print(language_RD[this->language][CALID_FEATURE_GREEN]);
  this->display->setTextColor(RED);
  this->display->setCursor(getCursorX_textLeft(language_RD[this->language][CALID_FEATURE_RED], this->language, 1), 210);
  this->display->print(language_RD[this->language][CALID_FEATURE_RED]);
  this->display->setTextColor(WHITE);
  this->display->setCursor(getCursorX_textLeft(language_RD[this->language][CALID_FEATURE_WHITE], this->language, 1), 230);
  this->display->print(language_RD[this->language][CALID_FEATURE_WHITE]);
}

void displayLCD::waiting_Calib()
{
  this->display->fillScreen(BLACK);
  this->drawFrameDisplay("");
  this->display->setTextColor(WHITE);
  drawWrappedText(language_RD[this->language][CALID_WAITING_TITILE], 45, 65, SCREEN_HEIGHT, this->language, 2);
  this->display->setTextColor(RED);
  drawWrappedText(language_RD[this->language][CALID_WAITING_PLEASE], 25, 150, SCREEN_HEIGHT, this->language, 2);
}

void displayLCD::screen_Calib_Complete()
{
  this->display->fillScreen(BLACK);
  this->display->setTextColor(WHITE);
  this->drawFrameDisplay(language_RD[this->language][CALID_MODE]);
  this->display->setTextColor(ORANGE);
  this->display->setCursor(45, 130);
  this->display->setTextColor(GREEN);
  this->display->print(language_RD[this->language][CALID_COMPLETE]);
  delay(3000);
}

void displayLCD::setting()
{
  this->display->fillScreen(BLACK);
  delete menu;
  this->menu = new Menu(display, 25, 75, 285, 120, 3, 4, MENU_SETTING, language);
  /* Thiết lập menu */
  menu->setItem(LANGUAGE, language_RD[this->language][SETTING_LANGUAGE], "", iconLanguage, 3);
  menu->setItem(WIFI, language_RD[this->language][SETTING_WIFI], "", iconWifi, 2);
  menu->setItem(UPDATE, language_RD[this->language][SETTING_UPDATE], "", iconUpdate, 1);
  menu->setItem(THRESHOLD, language_RD[this->language][SETTING_THRESHOLD], "", iconThreshold, 1);
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
  this->menu = new Menu(display, 25, 75, 285, 120, 3, NUMBER_LANGUAGE, MENU_LANGUAGE);
  /* Thiết lập menu */
  menu->setItem(VietNamese, language_convert[this->language][VietNamese], "", iconLanguage, 1);
  menu->setItem(English, language_convert[this->language][English], "", iconLanguage, 1);
  menu->setItem(Chinese, language_convert[this->language][Chinese], "", iconLanguage, 1);
  menu->setItem(Taiwanese, language_convert[this->language][Taiwanese], "", iconLanguage, 1);
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
        dbg_display("ewaitingReadsensor");
        this->waiting_Readsensor();
        break;
      }
      case eprepare:
      {
        dbg_display("eprepare");
        this->prepare();
        break;
      }
      case escreenAverageResult:
      {
        dbg_display("escreenAverageResult");
        _sensor.Average_All_Result();
        this->screen_Average_Result();
        break;
      }
      case ecalibSensor:
      {
        dbg_display("ecalibSensor");
        this->screen_Calib();
        break;
      }
      case echooseTube:
      {
        this->choose_tube();
        break;
      }
      case echooseSample:
      {
        this->choose_sample();
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
      /*
      case escreenchooseSlot:
      {
        this->choose_Sensor();
        break;
      }
      */
      default:
        break;
    }
    if (this->type_infor != e_setting)
      drawIconWifi();
    this->changeScreen = false;
  }
}

void displayLCD::updateThreshold()
{
  this->display->fillScreen(BLACK);
  drawFrameDisplay(language_RD[this->language][THRESHOLD_UPDATE_MODE]);
  this->display->setFont(u8g2_font_unifont_t_vietnamese2);

  this->display->setTextColor(WHITE);
  this->display->setTextSize(5);
  this->display->fillTriangle((_Threshold.index - 20), 85, (_Threshold.index + 20), 85, _Threshold.index, 95, WHITE);
  this->display->setCursor(85, 160);
  this->display->println(_Threshold.valueThreshold[0]);
  this->display->setCursor(125, 160);
  this->display->println(_Threshold.valueThreshold[1]);
  this->display->setCursor(165, 160);
  this->display->println(_Threshold.valueThreshold[2]);
  this->display->setCursor(205, 160);
  this->display->println(_Threshold.valueThreshold[3]);
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

void displayLCD::setupThreshold()
{
  this->display->fillScreen(BLACK);
  delete menu;
  this->menu = new Menu(display, 25, 75, 285, 120, 3, SICK_NUMBER, MENU_THRESHOLD);
  /* Thiết lập menu */
  menu->setItem(PC, language_sickName_positive[this->language], "", iconPC, _sensor.valueThreshold[PC]);
  menu->setItem(EHP, "EHP", "", iconEHP, _sensor.valueThreshold[EHP]);
  menu->setItem(EMS, "EMS", "", iconEMS, _sensor.valueThreshold[EMS]);
  menu->setItem(WSSV, language_sickName_WSSV[this->language], "", iconWSSV, _sensor.valueThreshold[WSSV]);
  menu->setItem(TPD, "TPD", "", iconTPD, _sensor.valueThreshold[TPD]);
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

displayLCD _displayLCD;
