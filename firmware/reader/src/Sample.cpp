#include "Sample.h"
#include "Arduino_GFX.h"

/***
 * YELLOW: 65504
 * RED: 63488
 * BLUE: 31
 * GREEN: 2016
 * ORANGE: 64800
 * WHITE: 65535
 * BLACK: 0
 */
// JSON mẫu (có 4 group)
const char *jsonData = R"json(
[
  {
    "name": "Prawn",
    "items": {
      "sampleName": ["P.Vannamei", "P.Monodon", "Water"],
      "sicksName": [
        {"sick": "PC", "threshold": 600, "color": 2016},
        {"sick": "EHP", "threshold": 600, "color": 31},
        {"sick": "EMS", "threshold": 600, "color": 64800},
        {"sick": "WSSV", "threshold": 600, "color": 65504},
        {"sick": "TPD", "threshold": 600, "color": 65535}
      ]
    }
  },
  {
    "name": "Fish",
    "items": {
      "sampleName": ["Fish", "Water"],
      "sicksName": [
        {"sick": "ISKNV", "threshold": 600, "color": 31},
        {"sick": "TILV", "threshold": 600, "color": 65504},
        {"sick": "PC", "threshold": 600, "color": 2016}
      ]
    }
  },
  {
    "name": "Pig",
    "items": {
      "sampleName": ["Blood", "Feces","Saliva"],
      "sicksName": [
        {"sick": "PC", "threshold": 600, "color": 2016},
        {"sick": "ASF", "threshold": 600, "color": 31}
      ]
    }
  },
  {
    "name": "Chicken",
    "items": {
      "sampleName": ["Chicken", "Water"],
      "sicksName": [
        {"sick": "empty", "threshold": 600, "color": 63488},
        {"sick": "empty", "threshold": 600, "color": 63488},
        {"sick": "empty", "threshold": 600, "color": 63488}
      ]
    }
  }
]
)json";

group_t animals;
group_type animals_RD;

// ---- Parse JSON ----
void initAnimalData(void)
{
  /* Check Init */
  EEPROM.begin(_EEPROM_SIZE);
  EEPROM.get(ADDR_CHECK_INITIALIZE_SAMPLE, animals_RD);
  EEPROM.end();

  if (animals_RD != GROUP_PRAWN &&
      animals_RD != GROUP_FISH &&
      animals_RD != GROUP_PIG &&
      animals_RD != GROUP_CHICKEN)
  {
    animals_RD = GROUP_PRAWN;
    /**Initialize Animals */
    group_t *a = nullptr;
    uint8_t numberGroup = 0;
    a = readAnimalJson(numberGroup);
    for (uint8_t i = 0; i < numberGroup; i++)
    {
      writeAnimalEeprom(ADDR_SAMPLE_DATA(i), a[i]);
    }

    if (a)
      free(a);
  }
}

group_t *readAnimalJson(uint8_t &numberGroup)
{
  group_t *a = nullptr;
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, jsonData);
  if (err)
  {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    return nullptr;
  }

  numberGroup = doc.size();
  a = (group_t *)malloc(sizeof(group_t) * numberGroup);

  for (int i = 0; i < numberGroup; i++)
  {
    JsonObject groupObj = doc[i];
    strlcpy(a[i].groupName, groupObj["name"] | "Unknown",
            sizeof(a[i].groupName));

    JsonObject item = groupObj["items"];

    // numberSample
    a[i].items.numberSample = item["sampleName"].size() | 0;
    JsonArray samples = item["sampleName"];
    for (int j = 0; j < a[i].items.numberSample; j++)
    {
      strlcpy(a[i].items.sampleNames[j],
              samples[j] | "",
              sizeof(a[i].items.sampleNames[j]));
    }

    // numberSick
    a[i].items.numberSick = item["sicksName"].size() | 0;
    JsonArray sicks = item["sicksName"];
    for (int k = 0; k < a[i].items.numberSick; k++)
    {
      JsonObject s = sicks[k];
      strlcpy(a[i].items.listSick[k].sickName,
              s["sick"] | "?",
              sizeof(a[i].items.listSick[k].sickName));
      a[i].items.listSick[k].threshold = s["threshold"] | 0;
      a[i].items.listSick[k].color = s["color"] | 0;
    }
  }
  return a;
}

void writeAnimalEeprom(uint16_t addr, const group_t &a)
{
  EEPROM.begin(_EEPROM_SIZE);
  EEPROM.put(ADDR_SAMPLE_NUMBERSAMPLE(addr), a.items.numberSample);
  EEPROM.put(ADDR_SAMPLE_NUMBERSICK(addr), a.items.numberSick);
  for (uint8_t i = 0; i < a.items.numberSick; i++)
  {
    EEPROM.put(ADDR_SAMPLE_LISTSICK(addr, i), a.items.listSick[i].threshold);
    EEPROM.put(ADDR_SAMPLE_LISTSICK(addr, i) + 4, a.items.listSick[i].color);
  }
  EEPROM.commit();
  EEPROM.end();
}

group_t readAnimalEeprom(uint16_t addr)
{
  group_t a;
  EEPROM.begin(_EEPROM_SIZE);
  EEPROM.get(ADDR_SAMPLE_NUMBERSAMPLE(addr), a.items.numberSample);
  EEPROM.get(ADDR_SAMPLE_NUMBERSICK(addr), a.items.numberSick);
  // for (uint8_t i = 0; i < a.items.numberSick && a.items.numberSick == PRAWN_SICK_NUMBER; i++)
  for (uint8_t i = 0; i < a.items.numberSick; i++)
  {
    uint16_t threshold = 0;
    uint16_t color = 0;
    EEPROM.get(ADDR_SAMPLE_LISTSICK(addr, i), threshold);
    a.items.listSick[i].threshold = threshold;
    EEPROM.get(ADDR_SAMPLE_LISTSICK(addr, i) + 4, color);
    a.items.listSick[i].color = color;
  }
  switch (addr)
  {
  case ADDR_PRAWN:
  {
    strcpy(a.groupName, "Prawn");
    for (uint8_t i = 0; i < a.items.numberSample && a.items.numberSample == NUMBER_SAMPLE_PRAWN; i++)
    {
      strcpy(a.items.sampleNames[i], prawnSampleNames[i]);
    }
    for (uint8_t i = 0; i < a.items.numberSick && a.items.numberSick == PRAWN_SICK_NUMBER; i++)
    {
      strcpy(a.items.listSick[i].sickName, prawnSickNames[i]);
    }
    break;
  }
  case ADDR_FISH:
  {
    strcpy(a.groupName, "Fish");
    for (uint8_t i = 0; i < a.items.numberSample; i++)
    {
      strcpy(a.items.sampleNames[i], fishSampleNames[i]);
    }
    for (uint8_t i = 0; i < a.items.numberSick; i++)
    {
      strcpy(a.items.listSick[i].sickName, fishSickNames[i]);
    }
    break;
  }
  case ADDR_PIG:
  {
    strcpy(a.groupName, "Pig");
    for (uint8_t i = 0; i < a.items.numberSample; i++)
    {
      strcpy(a.items.sampleNames[i], pigSampleNames[i]);
    }
    for (uint8_t i = 0; i < a.items.numberSick; i++)
    {
      strcpy(a.items.listSick[i].sickName, pigSickNames[i]);
    }
    break;
  }
  case ADDR_CHICKEN:
  {
    strcpy(a.groupName, "Chicken");
    for (uint8_t i = 0; i < a.items.numberSample; i++)
    {
      strcpy(a.items.sampleNames[i], chickenSampleNames[i]);
    }
    for (uint8_t i = 0; i < a.items.numberSick; i++)
    {
      strcpy(a.items.listSick[i].sickName, chickenSickNames[i]);
    }
    break;
  }
  }
  EEPROM.end();
  return a;
}

// ---- Debug print ----
void printAnimal(const group_t &a)
{
  Serial.print("Group: ");
  Serial.println(a.groupName);

  Serial.printf(" Number Sample: %d\n\r", a.items.numberSample);
  Serial.printf(" Number Sick: %d\n\r", a.items.numberSick);

  Serial.println(" Samples:");
  for (int i = 0; i < a.items.numberSample; i++)
  {
    Serial.print("  - ");
    Serial.println(a.items.sampleNames[i]);
  }

  Serial.println(" Sicks:");
  for (int k = 0; k < a.items.numberSick; k++)
  {
    Serial.print("  - ");
    Serial.print(a.items.listSick[k].sickName);
    Serial.print(" (thr=");
    Serial.print(a.items.listSick[k].threshold);
    Serial.print(", color=");
    Serial.print(a.items.listSick[k].color);
    Serial.println(")");
  }
}
