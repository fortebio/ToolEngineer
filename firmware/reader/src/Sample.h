#ifndef SAMPLE_H
#define SAMPLE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include "define.h"
// #include "displayresources.h"

// ---- EEPROM ----
// #define ADDR_PRAWN ADDR_SAMPLE_DATA(GROUP_PRAWN)
// #define ADDR_PRAWN_NUMBERSAMPLE (ADDR_PRAWN + 0)
// #define ADDR_PRAWN_NUMBERSICK (ADDR_PRAWN + 4)
// #define ADDR_PRAWN_LISTSICK(x) (ADDR_PRAWN + 8 + (8 * x))

#define ADDR_PRAWN ADDR_SAMPLE_DATA(GROUP_PRAWN)
#define ADDR_FISH ADDR_SAMPLE_DATA(GROUP_FISH)
#define ADDR_PIG ADDR_SAMPLE_DATA(GROUP_PIG)
#define ADDR_CHICKEN ADDR_SAMPLE_DATA(GROUP_CHICKEN)

#define ADDR_SAMPLE_NUMBERSAMPLE(addr) (addr + 4)
#define ADDR_SAMPLE_NUMBERSICK(addr) (addr + 6)
#define ADDR_SAMPLE_LISTSICK(addr, x) (addr + 8 + (8 * x))

// ---- Cấu trúc dữ liệu ----
typedef struct __attribute__((packed))
{
    char sickName[20];  // tên bệnh
    uint16_t threshold; // ngưỡng
    uint16_t color;     // màu hiển thị
} sick_t;

typedef struct __attribute__((packed))
{
    char sampleNames[10][15]; // tối đa 10 sample
    sick_t listSick[10];      // tối đa 10 bệnh
    uint8_t numberSample;     // số lượng sample thực tế
    uint8_t numberSick;       // số lượng bệnh thực tế
} animal_t;

typedef struct __attribute__((packed))
{
    char groupName[20]; // tên nhóm (Prawn, Fish, Pig, Chicken)
    animal_t items;     // chỉ 1 item duy nhất
} group_t;

/* Số mẫu của tôm */
#define PRAWN_SAMPLE_VANNAMEI 0
#define PRAWN_SAMPLE_MONODON 1
#define PRAWN_SAMPLE_WATER 2
#define NUMBER_SAMPLE_PRAWN 3
#define PRAWN_SICK_PC 0
#define PRAWN_SICK_EHP 1
#define PRAWN_SICK_EMS 2
#define PRAWN_SICK_WSSV 3
#define PRAWN_SICK_TPD 4
#define PRAWN_SICK_NUMBER 5
/* Số mẫu của cá */
#define FISH_SAMPLE_FISH 0
#define FISH_SAMPLE_WATER 1
#define NUMBER_SAMPLE_FISH 2
#define FISH_SICK_ISKNV 0
#define FISH_SICK_TILV 1
#define FISH_SICK_PC 2
#define FISH_SICK_NUMBER 3
/* Số mẫu của heo */
#define PIG_SAMPLE_BLOOD 0
#define PIG_SAMPLE_FECES 1
#define PIG_SAMPLE_SALIVA 2
#define NUMBER_SAMPLE_PIG 3
#define PIG_SICK_PC 0 
#define PIG_SICK_ASF 1 
#define PIG_SICK_NUMBER 2
/* Số mẫu của gà */
#define CHICKEN_SAMPLE_EMPTY1 0
#define CHICKEN_SAMPLE_EMPTY2 1
#define CHICKEN_SAMPLE_EMPTY3 2
#define NUMBER_SAMPLE_CHICKEN 2
#define CHICKEN_SICK_NUMBER 3

static const char *prawnSampleNames[] = {"P.Vannamei",
                                         "P.Monodon",
                                         "Water"};
static const char *prawnSickNames[] = {"PC",
                                       "EHP",
                                       "EMS",
                                       "WSSV",
                                       "TPD"};

static const char *fishSampleNames[] = {"Fish",
                                        "Water"};
static const char *fishSickNames[] = {"ISKNV",
                                      "TILV",
                                      "PC"};

static const char *pigSampleNames[] = {"Blood",
                                       "Feces",
                                       "Saliva"};
static const char *pigSickNames[] = {"ASF",
                                     "PC"};

static const char *chickenSampleNames[] = {"..",
                                           "..."};
static const char *chickenSickNames[] = {"empty1",
                                         "empty2",
                                         "empty3"};

// ---- API ----
void initAnimalData(void);

group_t *readAnimalJson(uint8_t &numberGroup);

void writeAnimalEeprom(uint16_t addr, const group_t &a);
group_t readAnimalEeprom(uint16_t addr);

void printAnimal(const group_t &a);

// ---- Biến toàn cục ----
extern group_t animals;
extern group_type animals_RD;

#endif // SAMPLE_H
