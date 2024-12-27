#include "Bluetooth.h"
#include <ArduinoJson.h>

BluetoothSerial SerialBT;
String ssid = "";
String password = "";
uint64_t epsid = ESP.getEfuseMac();
String id(String(epsid).c_str());

extern int language =0;

void connectBLE(){

  SerialBT.begin("BTDetector-" + String(ESP.getEfuseMac())); //Bluetooth device name
  dbg_bluetooth("The device started with name BTDetector-%s, now you can pair it with bluetooth!\n", String(ESP.getEfuseMac()).c_str());
}

void BLEloop(){
  if (SerialBT.available())
    {SerialBT.printf("received data: %s\n", SerialBT.readString());
    dbg_bluetooth("receive data");}
    // SerialBT.write('t');
    delay(300);
  // }

}


void readEEPROM()
{
  EEPROM.begin(_EEPROM_SIZE);
  String strjson = "";
  // Read json data from EEPROM
  char tmp[4] = {0};
  for (uint32_t i = 0; i < _EEPROM_SIZE; i++) {
    char c = EEPROM.read(i);
    sprintf(tmp, "%02X", c);
    strjson += tmp;
    if (i%32 == 31)
    {
      info_displayln(strjson);
      strjson = "";
    }
  }
  info_displayln(strjson);
  EEPROM.end();
}

void paraDisplay(parastructure para)
{
  info_displayf("Length: %d\n", para.length);

  DynamicJsonDocument paradata(3000);   //support maximum 3K

  paradata["para version"] = para.para_version;
  // info_displayf("para version: %s\n", para.para_version);
  paradata["PCB version"] = para.PCB_version;
  JsonObject calibration = paradata.createNestedObject("opto calibration");
  JsonArray slopes = calibration.createNestedArray("slopes");
  JsonArray origins = calibration.createNestedArray("origins");
  JsonArray ledPower = paradata.createNestedArray("LED power");
  for(int i=0;i<OPTOCHANNELS;i++)
  {
      slopes.add(para.slopes[i]);
      origins.add(para.origins[i]);
      ledPower.add(para.led_power[i]);
  }

  JsonObject opto_parameter = paradata.createNestedObject("parameters");
  opto_parameter["min increase"] = para.min_increase;
  opto_parameter["min sharpness"] = para.min_sharpness;
  opto_parameter["min slight positive time"] = para.min_slight_positive_time;
  opto_parameter["detect shape"] = para.detect_shape;
  opto_parameter["detection margin time"] = para.detection_margin_time;
  opto_parameter["arm percentile"] = para.arm_percentile;
  opto_parameter["transition percentile"] = para.transition_percentile;
  opto_parameter["sg order"] = para.sg_order;
  opto_parameter["sg window"] = para.sg_window;
  opto_parameter["baseline start"] = para.baseline_start;
  opto_parameter["baseline range"] = para.baseline_range;

  paradata["units"] = para.units;
  paradata["device ID"] = para.device_id;
  paradata["lysis duration"] = para.lysisDuration;
  paradata["opto preheat time"] = para.optopreheatduration;
  paradata["LED Duration"] = para.LEDDuration;
  paradata["time per loop"] = para.timePerLoop;
  paradata["amplification time"] = para.amplification_time;
  paradata["lysis temperature"] = para.lysisTemp;
  paradata["amplification temperature"] = para.amplifTemp;

  JsonArray bottomTemperatureSensorSq = paradata.createNestedArray("bottom temperature sensor seq");
  for(int i=0;i<3;i++)
  {
      bottomTemperatureSensorSq.add(para.bottomTemperatureSensorSq[i]);
  }

  JsonArray topTemperatureSensorSq = paradata.createNestedArray("top temperature sensor seq");
  for(int i=0;i<4;i++)
  {
      topTemperatureSensorSq.add(para.topTemperatureSensorSq[i]);
  }

  JsonArray pid1 = paradata.createNestedArray("PID parameter");
  JsonArray pid2 = paradata.createNestedArray("PID2 parameter");
  JsonArray bottomOverheat = paradata.createNestedArray("Bottom overheat value");
  JsonArray topOverheat = paradata.createNestedArray("Top overheat value");
  for(int i=0;i<3;i++)
  {
      pid1.add(para.kpid[i]);
      pid2.add(para.kpid2[i]);
      bottomOverheat.add(para.bottomOverheat[i]);
      topOverheat.add(para.topOverheat[i]);
  }

  JsonArray temperatureOffset = paradata.createNestedArray("temperature value calibration");
  for(int i=0;i<7;i++)
  {
      temperatureOffset.add(para.temperatureOffset[i]);
  }

  JsonArray hotlidPWM = paradata.createNestedArray("top heater PWM");
  for (int i = 0; i < 3; i++) {
    JsonArray row = hotlidPWM.createNestedArray();
    for (int j = 0; j < 2; j++) {
      row.add(para.hotlidPWM[i][j]);
    }
  }
  paradata["buzzer"] = para.buzzerOn?"On":"Off";

  //Output metadata
  String output;
  serializeJsonPretty(paradata, output);
  info_displayln(output+"@");

  // info_displayf("bottom temperature sensor seq: %d, %d, %d\n", para.bottomTemperatureSensorSq[0], para.bottomTemperatureSensorSq[1], para.bottomTemperatureSensorSq[2]);
  // info_displayf("top temperature sensor seq: %d, %d, %d, %d\n", para.topTemperatureSensorSq[0], para.topTemperatureSensorSq[1], para.topTemperatureSensorSq[2], para.topTemperatureSensorSq[3]);
  // info_displayf("PID parameter for Lysis: %.2f, %.2f, %.2f\n", para.kpid[0], para.kpid[1], para.kpid[2]);
  // info_displayf("PID2 parameter for Amplification: %.2f, %.2f, %.2f\n", para.kpid2[0], para.kpid2[1], para.kpid2[2]);
  // info_displayf("overheat value of bottom heater: %.2f, %.2f, %.2f\n", para.bottomOverheat[0], para.bottomOverheat[1], para.bottomOverheat[2]);
  // info_displayf("overheat value of top heater: %.2f, %.2f, %.2f\n", para.topOverheat[0], para.topOverheat[1], para.topOverheat[2]);
  // info_displayf("temperature value calibration: %.4g", para.temperatureOffset[0]);

  // for (uint8_t i = 1; i < 7; i++)
  // {
  //   info_displayf(", %.4g", para.temperatureOffset[i]);
  // }
  // info_displayln("\ntop heater PWM:");
  // for (uint8_t i = 0; i < 3; i++)
  // {
  //   info_displayf("Min %d, Max %d\n", para.hotlidPWM[i][0], para.hotlidPWM[i][1]);
  // }
  // info_displayf("buzzer is %s\n", para.buzzerOn?"On":"Off");

  // info_displayf("para version: %s\n", para.para_version);
  // info_displayf("PCB version: %s\n", para.PCB_version);

  // info_displayf("slopes: %.4g", para.slopes[0]);
  // for (uint8_t i = 1; i < 10; i++)
  // {
  //   info_displayf(", %.4g", para.slopes[i]);
  // }
  

  // info_displayf("\norigins: %.4g", para.origins[0]);
  // for (uint8_t i = 1; i < 10; i++)
  // {
  //   info_displayf(", %.4g", para.origins[i]);
  // }
  
  // info_displayf("\nLED power: %d", para.led_power[0]);
  // for (uint8_t i = 1; i < 10; i++)
  // {
  //   info_displayf(", %d", para.led_power[i]);
  // }

  // info_displayf("\nmin increase: %.4g\n", para.min_increase);
  // info_displayf("min sharpness: %.4g\n", para.min_sharpness);
  // info_displayf("min slight positive time: %.4g\n", para.min_slight_positive_time);
  // info_displayf("detect shape: %s\n", para.detect_shape?"True":"False");
  // info_displayf("detection margin time: %.4g\n", para.detection_margin_time);
  // info_displayf("arm percentile: %.4g\n", para.arm_percentile);
  // info_displayf("transition percentile: %.4g\n", para.transition_percentile);
  // info_displayf("sg order: %d\n", para.sg_order);
  // info_displayf("sg window: %d\n", para.sg_window);
  // info_displayf("baseline start: %d\n", para.baseline_start);
  // info_displayf("baseline range: %d\n", para.baseline_range);
  // info_displayf("units: %s\n", para.units);
  // info_displayf("device ID: %s\n", para.device_id);
  // info_displayf("slots: %d\n", OPTOCHANNELS);
  // info_displayf("lysis duration: %d\n", para.lysisDuration);
  // info_displayf("opto preheat time %d\n", para.optopreheatduration);
  // info_displayf("LED duration %d\n", para.LEDDuration);
  // info_displayf("time per loop: %d\n", para.timePerLoop);
  // info_displayf("amplification time: %d\n", para.amplification_time);
  // info_displayf("Lysis temperature: %.4g\n", para.lysisTemp);
  // info_displayf("amplification temperature: %.4g\n", para.amplifTemp);
  // info_displayf("bottom temperature sensor seq: %d, %d, %d\n", para.bottomTemperatureSensorSq[0], para.bottomTemperatureSensorSq[1], para.bottomTemperatureSensorSq[2]);
  // info_displayf("top temperature sensor seq: %d, %d, %d, %d\n", para.topTemperatureSensorSq[0], para.topTemperatureSensorSq[1], para.topTemperatureSensorSq[2], para.topTemperatureSensorSq[3]);
  // info_displayf("PID parameter for Lysis: %.2f, %.2f, %.2f\n", para.kpid[0], para.kpid[1], para.kpid[2]);
  // info_displayf("PID2 parameter for Amplification: %.2f, %.2f, %.2f\n", para.kpid2[0], para.kpid2[1], para.kpid2[2]);
  // info_displayf("overheat value of bottom heater: %.2f, %.2f, %.2f\n", para.bottomOverheat[0], para.bottomOverheat[1], para.bottomOverheat[2]);
  // info_displayf("overheat value of top heater: %.2f, %.2f, %.2f\n", para.topOverheat[0], para.topOverheat[1], para.topOverheat[2]);
  // info_displayf("temperature value calibration: %.4g", para.temperatureOffset[0]);

  // for (uint8_t i = 1; i < 7; i++)
  // {
  //   info_displayf(", %.4g", para.temperatureOffset[i]);
  // }
  // info_displayln("\ntop heater PWM:");
  // for (uint8_t i = 0; i < 3; i++)
  // {
  //   info_displayf("Min %d, Max %d\n", para.hotlidPWM[i][0], para.hotlidPWM[i][1]);
  // }
  // info_displayf("buzzer is %s\n", para.buzzerOn?"On":"Off");
}


void loadParaFromEEPROM()
{
  EEPROM.begin(_EEPROM_SIZE);
  parastructure para;
  EEPROM.get(PARAMETERPOS, para);
  info_displayf("check para in EEPROM, length is %d, right one is %d\n", para.length, sizeof(para));
  if (para.length == sizeof(para))        //if the length of the parameter in EEPROM is not -1 or 0, then use it.
  {
      info_displayln("There is para in the EEPROM");
      paraDisplay(para);
  }
  else
  {
      info_displayln("No para in the EEPROM");
      return;
  }
}


void connectWIFI(){
  ssid = ""; // Initialize as empty
  password = ""; // Initialize as empty
  id="";
  SerialBT.begin();
  while(!SerialBT.hasClient()) { // check if bluetooth connection is established
    delay(10);
  }
  SerialBT.println("Establishing setup...");
  delay(3000);
  SerialBT.println("Enter Wifi ID:");
  delay(3000);
  while(ssid.isEmpty()){
    ssid = SerialBT.readString();
    ssid.trim();
  }
  SerialBT.println("Wifi is " + ssid);
  SerialBT.println("Enter Wifi password:");
  while(password.isEmpty()){
    password = SerialBT.readString();
    password.trim();
  }
  SerialBT.println("Password is "+password);
  SerialBT.println("Enter ID machine:");
  while(id.isEmpty()){
    id = SerialBT.readString();
    id.trim();
  }
  SerialBT.println("ID is "+id);
  SerialBT.println("Setup completed!");
  delay(3000);
  SerialBT.end();
}

void saveCredentialsToEEPROM() {
  EEPROM.begin(_EEPROM_SIZE);
  // Write SSID length to EEPROM
  unsigned char ssidLength = ssid.length();
  EEPROM.put(10, ssidLength); // change from 0 to 10 to avoid conflict with max-min calibration value
  // Write SSID to EEPROM
  for (unsigned char i = 0; i < ssidLength; i++) {
    EEPROM.write(i + sizeof(ssidLength) + 10, ssid[i]);
  }
  // Write password length to EEPROM
  unsigned char passwordLength = password.length();
  EEPROM.put(sizeof(ssidLength) + ssidLength + 10, passwordLength);
  // Write password to EEPROM
  for (int i = 0; i < passwordLength; i++) {
    EEPROM.write(i + sizeof(ssidLength) + ssidLength + sizeof(passwordLength) + 10, password[i]);
  }
  //write ID length to EEPROM
  unsigned char IDLength = id.length();
  EEPROM.put(sizeof(ssidLength) + ssidLength + 30, IDLength);
  // Write ID to EEPROM
  for (int i = 0; i < IDLength; i++) {
    EEPROM.write(i + sizeof(ssidLength) + ssidLength + sizeof(passwordLength) + 30, id[i]);
  }
  EEPROM.commit();
  EEPROM.end();
}

void loadCredentialsFromEEPROM() {
  EEPROM.begin(_EEPROM_SIZE);
  unsigned char ssidLength;
  EEPROM.get(10, ssidLength);
  ssid = ""; // Clear ssid before loading
  // Read SSID from EEPROM
  for (unsigned char i = 0; i < ssidLength; i++) {
    char c = EEPROM.read(i + sizeof(ssidLength) + 10);
    ssid += c;
  }
  unsigned char passwordLength;
  EEPROM.get(sizeof(ssidLength) + ssidLength + 10, passwordLength);
  password = ""; // Clear password before loading
  // Read password from EEPROM
  for (unsigned char i = 0; i < passwordLength; i++) {
    char c = EEPROM.read(i + sizeof(ssidLength) + ssidLength + sizeof(passwordLength) + 10);
    password += c;
  }
unsigned char IDLength;
  EEPROM.get(sizeof(ssidLength) + ssidLength + 30, IDLength);

  id = ""; // Clear ID before loading
  // Read ID from EEPROM
  for (unsigned char i = 0; i < IDLength; i++) {
    char c = EEPROM.read(i + sizeof(ssidLength) + ssidLength + sizeof(passwordLength) + 30);
    id += c;
  }


  EEPROM.end();
 
}

void Write_language_ToEEPROM()
{
  EEPROM.begin(_EEPROM_SIZE);
  EEPROM.write(200,language);
  delay(50);
  EEPROM.commit();
  //EEPROM.end(); 
}
void Read_language_fromEEPROM()
{
  EEPROM.begin(_EEPROM_SIZE);
  language = EEPROM.read(200);
  delay(50);
  EEPROM.end();
}