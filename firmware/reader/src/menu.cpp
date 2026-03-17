// menu.cpp
#include "menu.h"
#include "Sample.h"
/* Size TFT ILI9341 */

Menu::Menu(Arduino_GFX *display, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t numItemDisplay, uint8_t count, MenuType menuType, uint8_t language)
{
    d = display;
    ndisItem = numItemDisplay;
    numItems = count;
    items = (item_type *)malloc(count * sizeof(item_type));
    selected = 0;
    cursorX = x;
    cursorY = y; // vị trí bắt đầu vẽ menu
    hei = h;
    wei = w;
    scrollMenu = 0;
    m = menuType;
    l = language;
}

Menu::Menu(Arduino_GFX *display, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t numItemDisplay, uint8_t count, MenuType menuType)
{
    d = display;
    ndisItem = numItemDisplay;
    numItems = count;
    items = (item_type *)malloc(count * sizeof(item_type));
    selected = 0;
    cursorX = x;
    cursorY = y; // vị trí bắt đầu vẽ menu
    hei = h;
    wei = w;
    scrollMenu = 0;
    m = menuType;
    // l = 0;
}

Menu::~Menu()
{
    if (items)
    {
        free(items);
        items = nullptr;
    }
}

void Menu::setItem(uint8_t index,
                   const char *title,
                   const char *desc,
                   const unsigned char **icon,
                   uint16_t numOpt)
{
    if (index >= numItems)
        return;

    items[index].x = cursorX;
    items[index].y = cursorY + index * (ITEM_HEIGTH + ITEM_SPACES);
    items[index].w = wei;
    items[index].h = ITEM_HEIGTH;

    items[index].tt = title;
    items[index].ds = desc;
    items[index].ic = icon;
    items[index].index = index;
    items[index].numOpt = numOpt;
    items[index].select = (index == selected);
}

void Menu::drawMenu()
{
    /** Configure fonts before drawing function menu*/
    if (l == VietNamese || l == English)
    {
        d->setFont(u8g2_font_unifont_t_vietnamese2);
    }
    else if (l == Chinese || l == Taiwanese)
    {
        d->setFont(u8g2_font_wqy14_t_gb2312);
    }

    for (uint8_t i = scrollMenu; i < (scrollMenu + ndisItem); i++)
    {
        if (i == selected)
            drawItemSelect(i);
        else
            drawItem(i);
    }
    for (uint8_t i = scrollMenu; i < (scrollMenu + ndisItem); i++)
    {
        switch (m)
        {
        case MENU_SICK:
        {
            drawIconProcessing(i);
            break;
        }
        case MENU_SAMPLE:
        {
            drawIconSamples(i);
            break;
        }
        case MENU_ANIMALS:
        {
            drawIconAnimals(i);
            break;
        }
        case MENU_SETTING:
        {
            drawIconSetting(i);
            break;
        }
        case MENU_LANGUAGE:
        {
            drawIconLanguage(i);
            break;
        }
        case MENU_THRESHOLD:
        {
            drawIconThreshold(i);
            break;
        }
        }
    }
    if (selected < (numItems - 1) && numItems != ndisItem)
        d->drawBitmap(160, (cursorY + ndisItem * (ITEM_HEIGTH + ITEM_SPACES) + 2), image_InfraredArrowDown_bits, 8, 4, WHITE);
    else
        d->drawBitmap(160, (cursorY + ndisItem * (ITEM_HEIGTH + ITEM_SPACES) + 2), image_InfraredArrowDown_bits, 8, 4, BLACK);

    if (scrollMenu != 0)
        d->drawBitmap(160, (cursorY - 9), image_InfraredArrowUp_bits, 8, 4, WHITE);
    else
        d->drawBitmap(160, (cursorY - 9), image_InfraredArrowUp_bits, 8, 4, BLACK);
}

void Menu::drawItem(uint8_t index)
{
    item_type it = items[index];
    it.y -= ((ITEM_HEIGTH + ITEM_SPACES) * scrollMenu);
    uint16_t cl = BLACK;
    if ((m == MENU_SICK) || (m == MENU_THRESHOLD))
    {
        cl = animals.items.listSick[it.index].color;
    }
    else
        cl = BLACK;

    d->fillRoundRect(it.x, it.y, it.w, it.h, ITEM_RADIUS, BLACK);
    d->drawRoundRect(it.x, it.y, it.w, it.h, ITEM_RADIUS, WHITE);
    d->fillRoundRect(it.w - 60, it.y, 90, it.h, ITEM_RADIUS, cl);
    d->drawRoundRect(it.w - 60, it.y, 90, it.h, ITEM_RADIUS, WHITE);
    d->setCursor(it.x + 10, it.y + 30);
    d->setTextColor(WHITE);
    d->print(it.tt);
}

void Menu::drawItemSelect(uint8_t index)
{
    item_type it = items[index];
    uint16_t cl = BLACK;
    if ((m == MENU_SICK) || (m == MENU_THRESHOLD))
    {
        cl = animals.items.listSick[it.index].color;
    }
    else
        cl = BLACK;

    it.y -= ((ITEM_HEIGTH + ITEM_SPACES) * scrollMenu);
    d->fillRoundRect(it.x, it.y, it.w, it.h, ITEM_RADIUS, DARKCYAN);
    d->fillRoundRect(it.w - 60, it.y, 90, it.h, ITEM_RADIUS, cl);
    d->drawRoundRect(it.w - 60, it.y, 90, it.h, ITEM_RADIUS, DARKCYAN);
    d->setCursor(it.x + 10, it.y + 30);
    d->setTextColor(BLACK);
    d->print(it.tt);
}

void Menu::drawIconSetting(uint8_t index)
{
    item_type it = items[index];
    it.y -= ((ITEM_HEIGTH + ITEM_SPACES) * scrollMenu);

    d->setTextColor(WHITE);
    if (it.index == LANGUAGE)
    {
        switch (l)
        {
        case Chinese:
        case Taiwanese:
        {
            d->drawBitmap(it.w - 30, it.y + 12, it.ic[l], 32, 14, 0xFFFF);
            break;
        }

        default:
        {
            d->drawBitmap(it.w - 30, it.y + 12, it.ic[l], 34, 14, 0xFFFF);
            break;
        }
        }
    }
    else if (it.index == SAMPLE)
    {
        d->drawBitmap(it.w - 30, it.y + 4, it.ic[animals_RD], 30, 30, WHITE);
    }
    else if (it.index == WIFI)
    {
        if (WiFi.status() == WL_CONNECTED)
            d->drawBitmap(it.w - 22, it.y + 12, it.ic[0], 19, 16, 0xFFFF);
        else
            d->drawBitmap(it.w - 22, it.y + 12, it.ic[1], 19, 16, 0xFFFF);
    }
    else if (it.index == UPDATE)
    {
        d->drawBitmap(it.w - 20, it.y + 12, it.ic[0], 15, 16, 0xFFFF);
    }
    else if (it.index == THRESHOLD)
    {
        d->drawBitmap(it.w - 20, it.y + 12, it.ic[0], 14, 16, 0xFFFF);
    }
}

void Menu::drawIconProcessing(uint8_t index)
{
    int16_t x, y;
    uint16_t w, h;
    item_type it = items[index];
    it.y -= ((ITEM_HEIGTH + ITEM_SPACES) * scrollMenu);
    d->getTextBounds(animals.items.listSick[it.index].sickName, 0, 0, &x, &y, &w, &h);

    d->setFont(u8g2_font_helvB08_tf);
    d->setTextColor(BLACK);
    d->setTextSize(2);
    if (w > 60)
        w = 60;
    d->setCursor(it.w - 60 + ((90 - w) / 2), it.y + 28);
    d->println(animals.items.listSick[it.index].sickName);
}

void Menu::drawIconSamples(uint8_t index)
{
    item_type it = items[index];
    it.y -= ((ITEM_HEIGTH + ITEM_SPACES) * scrollMenu);
    d->drawBitmap(it.w - 30, it.y + 4, it.ic[0], 30, 30, WHITE);
}

void Menu::drawIconAnimals(uint8_t index)
{
    item_type it = items[index];
    it.y -= ((ITEM_HEIGTH + ITEM_SPACES) * scrollMenu);
    d->drawBitmap(it.w - 30, it.y + 4, it.ic[0], 30, 30, WHITE);
}

void Menu::drawIconLanguage(uint8_t index)
{
    item_type it = items[index];
    it.y -= ((ITEM_HEIGTH + ITEM_SPACES) * scrollMenu);
    String lang = "";

    switch (index)
    {
    case Chinese:
        lang = "CHI";
        break;
    case Taiwanese:
        lang = "TWN";
        break;
    case English:
        lang = "ENG";
        break;
    case VietNamese:
        lang = "VIE";
        break;
    }
    d->setFont(u8g2_font_helvB08_tf);
    d->setTextColor(WHITE);
    d->setTextSize(2);
    d->setCursor(it.w - 60 + 25, it.y + 26);
    d->println(lang);
}

void Menu::drawIconThreshold(uint8_t index)
{
    item_type it = items[index];
    it.y -= ((ITEM_HEIGTH + ITEM_SPACES) * scrollMenu);
    d->setTextColor(BLACK);
    d->setTextSize(2);
    d->setCursor(it.w - 45, it.y + 30);
    d->printf("%d", animals.items.listSick[it.index].threshold);
}

void Menu::moveUp()
{
    if (selected > 0)
    {
        selected--;
        if (selected >= ndisItem - 1)
        {
            scrollMenu--;
        }
    }
    else if (selected <= 0)
    {
        selected = numItems - 1;
        scrollMenu = numItems - ndisItem;
    }
    drawMenu();
}

void Menu::moveDown()
{
    if (selected < numItems - 1)
    {
        selected++;
        if (selected > ndisItem - 1)
        {
            scrollMenu++;
        }
    }
    else if (selected >= (numItems - 1))
    {
        selected = 0;
        scrollMenu = 0;
    }
    drawMenu();
}
