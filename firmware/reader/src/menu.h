// menu.h
#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "displayresources.h"
#include "define.h"

#define ITEM_WIDTH 285
#define ITEM_HEIGTH 35
#define ITEM_RADIUS 10
#define ITEM_CURSORX 25
#define ITEM_SPACES 5
#define DISPLAY_WIDTH 280
#define DISPLAY_HEIGTH 240

enum MenuType
{
    MENU_SICK,
    MENU_SAMPLE,
    MENU_ANIMALS,
    MENU_SETTING,
    MENU_LANGUAGE,
    MENU_THRESHOLD,
    CUSTOM
};

typedef enum
{
    LANGUAGE,
    WIFI,
    UPDATE,
    THRESHOLD,
    SAMPLE,
    NUMBER_SETTING
} setting_type;

typedef struct
{
    uint16_t x, y, w, h;
    const char *tt;
    const char *ds;
    const unsigned char **ic;
    uint8_t index;
    bool select;
    uint8_t idItem;
    uint16_t numOpt;
} item_type;

class Menu
{
private:
    Arduino_GFX *d;   // đối tượng hiển thị
    item_type *items; // mảng các item
    uint8_t numItems; // số lượng item
    uint8_t selected; // item đang được chọn
    uint8_t scrollMenu;
    uint8_t ndisItem;
    uint16_t cursorX, cursorY;
    uint16_t hei, wei;
    MenuType m;
    uint8_t l = VietNamese;

public:
    /*Setting*/
    Menu(Arduino_GFX *display, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t numItemDisplay, uint8_t count, MenuType menuType, uint8_t language);
    Menu(Arduino_GFX *display, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t numItemDisplay, uint8_t count, MenuType menuType);
    // Menu(Arduino_GFX *display, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t numItemDisplay, uint8_t count, MenuType menuType);
    ~Menu();

    void setItem(uint8_t index,
                 const char *title,
                 const char *desc,
                 const unsigned char **icon,
                 uint16_t numOpt);

    void drawMenu();
    void drawItem(uint8_t index);
    void drawItemSelect(uint8_t index);
    void drawIconSetting(uint8_t index);
    void drawIconProcessing(uint8_t index);
    void drawIconSamples(uint8_t index);
    void drawIconAnimals(uint8_t index);
    void drawIconLanguage(uint8_t index);
    void drawIconThreshold(uint8_t index);

    void moveUp(void);
    void moveDown(void);
    uint8_t getSelectedIndex(void)
    {
        uint8_t idx = selected;
        if (items)
        {
            free(items);
            items = nullptr;
            numItems = 0;
        }
        return idx;
    };

    void freeMenu(void)
    {
        if (items)
        {
            free(items);
            items = nullptr;
            numItems = 0;
        }
    };
};
