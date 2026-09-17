#include "menu.h"
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
    l = 0;
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
        case MENU_PROCESSING:
        {
            drawIconProcessing(i);
            break;
        }
        case MENU_SAMPLES:
        {
            drawIconSamples(i);
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
    if (selected < (numItems - 1))
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
    d->fillRoundRect(it.x, it.y, it.w, it.h, ITEM_RADIUS, BLACK);
    d->drawRoundRect(it.x, it.y, it.w, it.h, ITEM_RADIUS, WHITE);
    d->drawRoundRect(it.w - 55, it.y, 80, it.h, ITEM_RADIUS, WHITE);
    d->setCursor(it.x + 15, it.y + 30);
    d->setTextColor(WHITE);
    d->print(it.tt);
}

void Menu::drawItemSelect(uint8_t index)
{
    item_type it = items[index];
    it.y -= ((ITEM_HEIGTH + ITEM_SPACES) * scrollMenu);
    d->fillRoundRect(it.x, it.y, it.w, it.h, ITEM_RADIUS, BLUE);
    d->fillRoundRect(it.w - 55, it.y, 80, it.h, ITEM_RADIUS, BLACK);
    d->drawRoundRect(it.w - 55, it.y, 80, it.h, ITEM_RADIUS, BLUE);
    d->setCursor(it.x + 15, it.y + 30);
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
    item_type it = items[index];
    it.y -= ((ITEM_HEIGTH + ITEM_SPACES) * scrollMenu);

    d->setTextColor(WHITE);
    if (it.index == PC)
    {

        d->drawBitmap(it.w - 23, it.y + 12, it.ic[0], 22, 14, 0xFFFF);
    }
    else if (it.index == EHP)
    {
        d->drawBitmap(it.w - 30, it.y + 12, it.ic[0], 34, 14, 0xFFFF);
    }
    else if (it.index == EMS)
    {
        d->drawBitmap(it.w - 30, it.y + 12, it.ic[0], 34, 14, 0xFFFF);
    }
    else if (it.index == WSSV)
    {
        d->drawBitmap(it.w - 40, it.y + 12, it.ic[0], 46, 14, 0xFFFF);
    }
    else if (it.index == TPD)
    {
        d->drawBitmap(it.w - 30, it.y + 12, it.ic[0], 34, 14, 0xFFFF);
    }
}

void Menu::drawIconSamples(uint8_t index)
{
    item_type it = items[index];
    it.y -= ((ITEM_HEIGTH + ITEM_SPACES) * scrollMenu);

    // d->setTextColor(WHITE);
    d->drawBitmap(it.w - 30, it.y + 4, it.ic[0], 30, 30, WHITE);
}

void Menu::drawIconLanguage(uint8_t index)
{
    item_type it = items[index];
    it.y -= ((ITEM_HEIGTH + ITEM_SPACES) * scrollMenu);

    d->setTextColor(WHITE);
    switch (index)
    {
    case Chinese:
    case Taiwanese:
    {
        d->drawBitmap(it.w - 30, it.y + 12, it.ic[index], 32, 14, 0xFFFF);
        break;
    }

    default:
    {
        d->drawBitmap(it.w - 30, it.y + 12, it.ic[index], 34, 14, 0xFFFF);
        break;
    }
    }
}

void Menu::drawIconThreshold(uint8_t index)
{
    item_type it = items[index];
    it.y -= ((ITEM_HEIGTH + ITEM_SPACES) * scrollMenu);
    d->setTextColor(WHITE);
    d->setTextSize(2);
    d->setCursor(it.w - 45, it.y + 30);
    d->printf("%d", it.numOpt);
}

void Menu::moveUp()
{
    if (selected > 0)
    {

        selected--;
        if (selected > ndisItem - 1)
        {
        }
        else
        {
            scrollMenu = 0;
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