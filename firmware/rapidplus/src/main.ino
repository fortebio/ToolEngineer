/*
Version 1.3 note: add function send wifi id and password via bluetooth
Version 1.4 note: add function sellect language

*/
#include "define.h"
// #include "sensor.h"
#include "displayCLD.h"
#include "button.h"
#include "bluetooth.h"
#include "thermometer.h"
#include "wire.h"
#include "sensor6035.h"
#include "PIDControl.h"
#include "ForteSetting.h"
#include "Fan.h"
// #include "driver/uart.h"

// const char* host = "esp32";
// WebServer server(80);

// /////////////////////////////////////////////
// String style =
// "<style>#file-input,input{width:100%;height:44px;border-radius:4px;margin:10px auto;font-size:15px}"
// "input{background:#f1f1f1;border:0;padding:0 15px}body{background:#3498db;font-family:sans-serif;font-size:14px;color:#42f5d7}"
// "#file-input{padding:0;border:1px solid #ddd;line-height:44px;text-align:left;display:block;cursor:pointer}"
// "#bar,#prgbar{background-color:#f1f1f1;border-radius:10px}#bar{background-color:#3498db;width:0%;height:10px}"
// "form{background:#fff;max-width:258px;margin:75px auto;padding:30px;border-radius:5px;text-align:center}"
// ".btn{background:#3498db;color:#fff;cursor:pointer}</style>";
 
// /* Login page */
// String loginIndex = 
// "<form name=loginForm>"
// "<h1>FORTE BIOTECH </h1>" 
// "<h1> Team Engineer </h1>"
// "<h3> Update firmware to reader </h3>"
// "<input name=userid placeholder='User ID'> "
// "<input name=pwd placeholder=Password type=Password> "
// "<input type=submit onclick=check(this.form) class=btn value=Login></form>"
// "<script>"
// "function check(form) {"
// "if(form.userid.value=='ForteBiotech' && form.pwd.value=='ForteBiotech')"
// "{window.open('/serverIndex')}"
// "else"
// "{alert('Error Password or Username')}"
// "}"
// "</script>" + style;
  
// /* Server Index Page */
// String serverIndex = 
// "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
// "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
// "<input type='file' name='update' id='file' onchange='sub(this)' style=display:none>"
// "<label id='file-input' for='file'>   Choose file...</label>"
// "<input type='submit' class=btn value='Update'>"
// "<br><br>"
// "<div id='prg'></div>"
// "<br><div id='prgbar'><div id='bar'></div></div><br></form>"
// "<script>"
// "function sub(obj){"
// "var fileName = obj.value.split('\\\\');"
// "document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1];"
// "};"
// "$('form').submit(function(e){"
// "e.preventDefault();"
// "var form = $('#upload_form')[0];"
// "var data = new FormData(form);"
// "$.ajax({"
// "url: '/update',"
// "type: 'POST',"
// "data: data,"
// "contentType: false,"
// "processData:false,"
// "xhr: function() {"
// "var xhr = new window.XMLHttpRequest();"
// "xhr.upload.addEventListener('progress', function(evt) {"
// "if (evt.lengthComputable) {"
// "var per = evt.loaded / evt.total;"
// "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
// "$('#bar').css('width',Math.round(per*100) + '%');"
// "}"
// "}, false);"
// "return xhr;"
// "},"
// "success:function(d, s) {"
// "document.write('success!') "
// "},"
// "error: function (a, b, c) {"
// "}"
// "});"
// "});"
// "</script>" + style;
// /////////////////////////////////////////////

buttonManager _buttonManager;
  

void setup() {
  connectBLE();

  // Serial.setRxBufferSize(2048);
  Serial.setRxBufferSize(3*1024);
  Serial.begin(115200);
  //configure the I2C IO
  Wire.begin(SDA_Forte, SCL_Forte);
  //configure the button
  _buttonManager.buttonStart();

  // loadCredentialsFromEEPROM();
  // WiFi.begin(ssid.c_str(), password.c_str());
  /*use mdns for host name resolution*/
  // if (!MDNS.begin(host)) { //http://esp32
  //   info_displayln("Error setting up MDNS responder!");
  //   while (1) {
  //     delay(10);
  //   }
  // }
  // info_displayln("mDNS responder started");
 
  /*return index page which is stored in serverIndex */
  // server.on("/", HTTP_GET, []() {
  //   server.sendHeader("Connection", "close");
  //   server.send(200, "text/html", loginIndex);
  // });
  // server.on("/serverIndex", HTTP_GET, []() {
  //   server.sendHeader("Connection", "close");
  //   server.send(200, "text/html", serverIndex);
  // });
  // /*handling uploading firmware file */
  // server.on("/update", HTTP_POST, []() {
  //   server.sendHeader("Connection", "close");
  //   server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
  //   ESP.restart();
  // }, []() {
  //   HTTPUpload& upload = server.upload();
  //   if (upload.status == UPLOAD_FILE_START) {
  //     info_displayf("Update: %s\n", upload.filename.c_str());
  //     if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
  //       Update.printError(Serial);
  //     }
  //   } else if (upload.status == UPLOAD_FILE_WRITE) {
  //     /* flashing firmware to ESP*/
  //     if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
  //       Update.printError(Serial);
  //     }
  //   } else if (upload.status == UPLOAD_FILE_END) {
  //     if (Update.end(true)) { //true to set the size to the current progress
  //       info_displayf("Update Success: %u\nRebooting...\n", upload.totalSize);
  //     } else {
  //       Update.printError(Serial);
  //     }
  //   }
  // });
  // Read_language_fromEEPROM();

  _displayCLD.begin();
  _ForteSetting.begin();

  _PIDControl.begin();
  // _PIDControl.sensorSeq(); //move inside _PIDControl.begin()
  _displayCLD.logoFortebiotech();

  _sensor6035.begin();    //include sensor, LED and buzzer!

  _Fan.begin();

  _PIDControl.timeoutSetting();
  
  info_displayf("This is the Forte Heater&Reader %s on PCB %s @ %s %s\n", FirmwareVer, _ForteSetting.parameter.PCB_version,__DATE__, __TIME__);
}

void loop() {
  _PIDControl.loop();

  _sensor6035.loop();

  _displayCLD.loop();

  _ForteSetting.loop();   //configure para

  _buzzer.loop();

  _Fan.loop();    //keep open the Fan

  // server.handleClient();

}
