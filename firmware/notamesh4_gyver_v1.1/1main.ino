#if (IR_ON == 1) && (KOL_LED > IR_MAX_LEDS)
#error "Значение KOL_LED должно быть меньше или равно IR_MAX_LEDS"
#endif

#define qsubd(x, b)  ((x>b)?wavebright:0)                     // A digital unsigned subtraction macro. if result <0, then => 0. Otherwise, take on fixed value.
#define qsuba(x, b)  ((x>b)?x-b:0)                            // Unsigned subtraction macro. if result <0, then => 0.

#define NOTAMESH_VERSION 103                                  // Just a continuation of seirlight and previously aalight.

#include "FastLED.h"                                          // https://github.com/FastLED/FastLED
#if   IR_ON == 1
#include "EEPROM.h"                                           // This is included with base install
#include "IRremote.h"                                         // 
#endif
#include "commands.h"                                         // The commands.

#if FASTLED_VERSION < 3001000
#error "Requires FastLED 3.1 or later; check github for latest code."
#endif

#if KEY_ON == 1                                                 //Для аналоговых кнопок
int key_input = 0;                                            //Последнее нажатие кнопки
int key_input_new;                                            //только что пришедьшее нажатие кнопки
bool key_bounce = 0;                                          //для антидребезга
uint32_t key_time;                                            //время последнего нажатия
#endif

#if IR_ON == 1
int RECV_PIN = PIN_IR;
IRrecv irrecv(RECV_PIN);
decode_results results;
#endif

#if ( IR_ON == 1 || KEY_ON == 1 || USE_BTN == 1 )
uint8_t  IR_New_Mode = 0;                                      //Выбор эффекта
uint32_t IR_Time_Mode = 0;                                     //время последнего нажатия
#endif

// Serial definition
#define SERIAL_BAUDRATE 57600                                 // Or 115200.

// Fixed definitions cannot change on the fly.
#if IR_ON == 1
#define MAX_LEDS IR_MAX_LEDS
#else
#define MAX_LEDS  KOL_LED
#endif

// Initialize changeable global variables.
#if MAX_LEDS < 255
uint8_t NUM_LEDS;                                           // Number of LED's we're actually using, and we can change this only the fly for the strand length.
uint8_t KolLed;
#else
uint16_t NUM_LEDS;                                          // Number of LED's we're actually using, and we can change this only the fly for the strand length.
uint16_t KolLed;
#endif

#ifdef ESP32
  #include <WiFi.h>
  #include <AsyncTCP.h>
#else
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>
//#include <WebServer.h>
#include <ArduinoOTA.h>

const char* ssid = "MY_SSID";
const char* password = "MY_PASSWORD";

const char* PARAM_INPUT_1 = "lup";
const char* PARAM_INPUT_2 = "ldown";
//int ledmode = 0;

int max_bright = 255;                                     // Overall brightness definition. It can be changed on the fly.

struct CRGB leds[MAX_LEDS];                                   // Initialize our LED array. We'll be using less in operation.

CRGBPalette16 gCurrentPalette;                               // Use palettes instead of direct CHSV or CRGB assignments
CRGBPalette16 gTargetPalette;                                // Also support smooth palette transitioning
CRGB solid = CRGB::Black;                                    // Цвет закраски


extern const TProgmemRGBGradientPalettePtr gGradientPalettes[]; // These are for the fixed palettes in gradient_palettes.h
extern const uint8_t gGradientPaletteCount;                     // Total number of fixed palettes to display.
uint8_t gCurrentPaletteNumber = 0;                              // Current palette number from the 'playlist' of color palettes
uint8_t currentPatternIndex = 0;                                // Index number of which pattern is current
uint32_t demo_time = 0;                                         // время демо режима

TBlendType currentBlending = LINEARBLEND;                     // NOBLEND or LINEARBLEND

// EEPROM location definitions.
#define STARTMODE 0                                           // EEPROM location for the starting mode.
#define STRANDLEN 1                                           // EEPROM location for the actual Length of the strand, which is < MAX_LEDS
#define STRANDEL  3                                           // EEPROM location for the mesh delay value.
#define ISINIT    4                                           // EEPROM location used to verify that this Arduino has been initialized

#define INITVAL   0x55                                        // If this is the value in ISINIT, then the Arduino has been initialized. Startmode should be 0 and strandlength should be 
#define INITMODE  0                                           // Startmode is 0, which is black.
#define INITLEN   KOL_LED                                     // Start length LED's.
#define INITDEL   0                                           // Starting mesh delay value of the strand in milliseconds.

uint16_t meshdelay;                                             // Timer for the notamesh. Works with INITDEL.

uint8_t ledMode = 0;                                            // номер текущего режима
#if CHANGE_ON == 1
uint8_t newMode = 0;                                        // номер нового режима
#if MAX_LEDS < 255
uint8_t StepMode = MAX_LEDS;                              // Текущий шаг перехода от нового режима до старого
#else
uint16_t StepMode = MAX_LEDS;                             // Текущий шаг перехода от нового режима до старого
#endif
#endif

uint8_t demorun = DEMO_MODE;                                    // 0 = regular mode, 1 = demo mode, 2 = shuffle mode.
#define maxMode  41                                           // Maximum number of modes.

uint8_t Protocol = 0;                                         // Temporary variables to save latest IR input
uint32_t Command = 0;

// Общие переменные ----------------------------------------------------------------------
uint8_t allfreq = 32;                                         // Меняет частоту. Переменная для эффектов one_sin_pal и two_sin.
uint8_t bgclr = 0;                                            // Общий цвет фона. Переменная для эффектов matrix_pal и one_sin_pal.
uint8_t bgbri = 0;                                            // Общая фоновая яркость. Переменная для эффектов matrix_pal и one_sin_pal.
bool    glitter = GLITER_ON;                                  // Флаг включения блеска
bool    background = BACKGR_ON;                               // Флаг включения заполнения фона
#if CANDLE_KOL >0
bool    candle = CANDLE_ON;                                 // Флаг включения свечей
#endif
uint8_t palchg = 3;                                           // Управление палитрой  3 - менять палитру автоматически иначе нет
uint8_t startindex = 0;                                       // С какого цвета начинать. Переменная для эффектов one_sin_pal.
uint8_t thisbeat;                                             // Переменная для эффектов juggle_pal.
uint8_t thiscutoff = 192;                                     // Если яркость ниже этой, то яркость = 0. Переменная для эффектов one_sin_pal и two_sin.
uint16_t thisdelay = 0;                                       // Задержка delay
uint8_t thisdiff = 1;                                         // Шаг изменения палитры. Переменная для эффектов confetti_pal, juggle_pal и rainbow_march.
int8_t  thisdir = 1;                                          // Направление движения эффекта. принимает значение -1 или 1.
uint8_t thisfade = 224;                                       // Скорость затухания. Переменная для эффектов confetti_pal и juggle_pal.
uint8_t thishue = 0;                                          // Оттенок Переменная для эффектов two_sin.
uint8_t thisindex = 0;                                        // Указатель ан элемент палитры
uint8_t thisinc = 1;                                          // Изменение начального цвета после каждого прохода. Переменная для эффектов confetti_pal и one_sin_pal.
int     thisphase = 0;                                        // Изменение фазы. Переменная для эффектов one_sin_pal, plasma и two_sin.
uint8_t thisrot = 1;                                          // Измение скорости вращения волны. В настоящее время 0.
int8_t  thisspeed = 4;                                        // Изменение стандартной скорости
uint8_t wavebright = 255;                                     // Вы можете изменить яркость волн / полос, катящихся по экрану.

#if MY_MODE
const PROGMEM uint8_t my_mode[] = { MY_MODE };            //массив выбранных режимов
const uint8_t my_mode_count = sizeof( my_mode );          //колличество выбрано режимов
uint8_t tek_my_mode = 0;                                  //Указатель на текущий режим
#endif

#if CHANGE_SPARK == 4
uint8_t rand_spark = 0;
#endif

long summ = 0;

void strobe_mode(uint8_t newMode, bool mc);
void bootme();
void meshwait();
void getirl();
void demo_check();
//void SetMode(uint8_t Mode); 

// Display functions -----------------------------------------------------------------------

// Support functions
#include "addings.h"

// Display routines
#include "confetti_pal.h"
#include "gradient_palettes.h"
#include "juggle_pal.h"
#include "matrix_pal.h"
#include "noise16_pal.h"
#include "noise8_pal.h"
#include "one_sin_pal.h"
#include "plasma.h"
#include "rainbow_march.h"
#include "rainbow_beat.h"
#include "serendipitous_pal.h"
#include "three_sin_pal.h"
#include "two_sin.h"
#include "blendwave.h"
#include "fire.h"
#include "candles.h"
#include "colorwave.h"
#include "getirl.h"
#include "GyverButton.h"

GButton btn(BTN_PIN);

//Web server definitions
AsyncWebServer server(80);
//WebServer server(80);
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Christmas Web Server</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html {font-family: Arial; display: inline-block;}
    h2 {font-size: 2.0rem; color: blue; font-weight: bold; text-align: center;}
    h3 {font-size: 2.0rem; text-align: center;}
    h4 {font-size: 2.2rem; color: red; text-align: center;}
    p {font-size: 3.0rem; color: red; font-weight: bold; text-align: center;}
    button {font-size: 2.0rem; font-weight: bold; border-radius: 5px; padding: 5px; display: inline-block; width: 100%%; height: 100%%;}
    button:focus {background-color: #FFFF22;}
    table { margin-left: auto;  margin-right: auto;}
    body {max-width: 600px; margin:0px auto; padding-bottom: 25px;}
    .switch {position: relative; display: inline-block; width: 120px; height: 68px} 
    .switch input {display: none}
    .slider {position: absolute; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; border-radius: 34px}
    .slider:before {position: absolute; content: ""; height: 52px; width: 52px; left: 8px; bottom: 8px; background-color: #fff; -webkit-transition: .4s; transition: .4s; border-radius: 68px}
    input:checked+.slider {background-color: #2196F3}
    input:checked+.slider:before {-webkit-transform: translateX(52px); -ms-transform: translateX(52px); transform: translateX(52px)}
    input[type=range][orient=vertical]
{
    writing-mode: bt-lr; /* IE */
    -webkit-appearance: slider-vertical; /* WebKit */
    width: 8px;
    height: 300px;
    padding: 0 5px;
    text-align: right;
}
  </style>
</head>
<body>
  %BUTTONPLACEHOLDER%
<table id="modeselector">
<tr>
<td><button id="b1" onclick="select_mode(this)">1</button></td>
<td><button id="b2" onclick="select_mode(this)">2</button></td>
<td><button id="b3" onclick="select_mode(this)">3</button></td>
<td><button id="b4" onclick="select_mode(this)">4</button></td>
<td><button id="b5" onclick="select_mode(this)">5</button></td></tr>
<tr>
<td><button id="b6" onclick="select_mode(this)">6</button></td>
<td><button id="b7" onclick="select_mode(this)">7</button></td>
<td><button id="b8" onclick="select_mode(this)">8</button></td>
<td><button id="b9" onclick="select_mode(this)">9</button></td>
<td><button id="b10" onclick="select_mode(this)">10</button></td></tr>
<tr>
<td><button id="b11" onclick="select_mode(this)">11</button></td>
<td><button id="b12" onclick="select_mode(this)">12</button></td>
<td><button id="b13" onclick="select_mode(this)">13</button></td>
<td><button id="b14" onclick="select_mode(this)">14</button></td>
<td><button id="b15" onclick="select_mode(this)">15</button></td></tr>
<tr>
<td><button id="b16" onclick="select_mode(this)">16</button></td>
<td><button id="b17" onclick="select_mode(this)">17</button></td>
<td><button id="b18" onclick="select_mode(this)">18</button></td>
<td><button id="b19" onclick="select_mode(this)">19</button></td>
<td><button id="b20" onclick="select_mode(this)">20</button></td></tr>
<tr>
<td><button id="b21" onclick="select_mode(this)">21</button></td>
<td><button id="b22" onclick="select_mode(this)">22</button></td>
<td><button id="b23" onclick="select_mode(this)">23</button></td>
<td><button id="b24" onclick="select_mode(this)">24</button></td>
<td><button id="b25" onclick="select_mode(this)">25</button></td></tr>
<tr>
<td><button id="b26" onclick="select_mode(this)">26</button></td>
<td><button id="b27" onclick="select_mode(this)">27</button></td>
<td><button id="b28" onclick="select_mode(this)">28</button></td>
<td><button id="b29" onclick="select_mode(this)">29</button></td>
<td><button id="b30" onclick="select_mode(this)">30</button></td></tr>
<tr>
<td><button id="b31" onclick="select_mode(this)">31</button></td>
<td><button id="b32" onclick="select_mode(this)">32</button></td>
<td><button id="b33" onclick="select_mode(this)">33</button></td>
<td><button id="b34" onclick="select_mode(this)">34</button></td>
<td><button id="b35" onclick="select_mode(this)">35</button></td></tr>
<tr>
<td><button id="b36" onclick="select_mode(this)">36</button></td>
<td><button id="b37" onclick="select_mode(this)">37</button></td>
<td><button id="b38" onclick="select_mode(this)">38</button></td>
<td><button id="b39" onclick="select_mode(this)">39</button></td>
<td><button id="b40" onclick="select_mode(this)">40</button></td></tr>
</table>
<script>function toggleCheckbox(param, state) {
  console.log("Button is ")
  console.log(state)
  var xhr = new XMLHttpRequest();/*
  if(state){ xhr.open("GET", "/update?state=1", true); }
  else { xhr.open("GET", "/update?state=0", true); }*/
  xhr.open("GET", "/update?" + param + "=" + state, true);
  xhr.send();
}

setInterval(function() {
  // Call a function repetatively with 0.5 Second interval
  getData();
}, 500); //500mSeconds update rate

function getData() {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("ledc").innerHTML =
      this.responseText;
    }
  };
  xhttp.open("GET", "getLedMode", true);
  xhttp.send();
}
//function updateC(a) {
//  var span = document.getElementById("ledc")
//  text = span.innerHTML
//  text = parseInt(text)
//  text += a
//  span.innerHTML = text.toString()
//}

//var b_lup = document.getElementById("lup")
//b_lup.onmousedown = function(e) {
//  console.log("lup button down")
//  toggleCheckbox("lup", 1)
  //updateC(1)
//}
//b_lup.onmouseup = function(e) {
//  console.log("lup button up")
//  toggleCheckbox("lup", 0)
//}

//var b_ldown = document.getElementById("ldown")
//b_ldown.onmousedown = function(e) {
//  console.log("ldown button down")
//  toggleCheckbox("ldown", 1)
  //updateC(-1)
//}
//b_ldown.onmouseup = function(e) {
//  console.log("ldown button up")
//  toggleCheckbox("ldown", 0)
//}
function select_mode(btn) {
  var xhr = new XMLHttpRequest();/*
  if(state){ xhr.open("GET", "/update?state=1", true); }
  else { xhr.open("GET", "/update?state=0", true); }*/
  xhr.open("GET", "/setmode?mode=" + btn.innerHTML, true);
  xhr.send();  
}
</script>
</body>
</html>
)rawliteral";

// Replaces placeholder with button section in your web page
String processor(const String& var){
  //Serial.println(var);
  if(var == "BUTTONPLACEHOLDER"){
    String buttons ="";
    // button html
    buttons += "<h2>XmsTree Web Server</h2>";
    buttons += "<h3>Current LED Mode: <p id=\"ledc\">";
    buttons += ledMode;
    buttons += "</p></h3>";
    buttons += "<h3>Select mode</h3>";
    //buttons += "<br><button style=\"width:100px;height:50px\" id=\"lup\">LEDMODE UP</button><br><button style=\"width:100px;height:50px\" id=\"ldown\">LEDMODE DOWN</button>";
    return buttons;
  }
  return String();
}
bool changedFromWeb = false;

//void handleLedMode() {
// String ledValue = String(ledMode);
// return ledValue;
// server.send(200, "text/plane", ledValue); //Send LED mode only to client ajax request
//}
/*------------------------------------------------------------------------------------------
  --------------------------------------- Start of code --------------------------------------
  ------------------------------------------------------------------------------------------*/
void setup() {

#if KEY_ON == 1
  pinMode(PIN_KEY, INPUT);                                                        //Для аналоговых кнопок
#endif

#if LOG_ON == 1
  Serial.begin(SERIAL_BAUDRATE);                                                  // Setup serial baud rate

  Serial.println(F(" ")); Serial.println(F("---SETTING UP---"));
#endif
  delay(1000);                                                                    // Slow startup so we can re-upload in the case of errors.

//WI-FI and web server setup
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }

  // Print ESP Local IP Address
  Serial.println(WiFi.localIP());
  ArduinoOTA.begin();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  server.on("/getLedMode", HTTP_GET, [](AsyncWebServerRequest *request){
//    request->send_P(200, "text/plane", handleLedMode);
    char cstr[16];
    itoa(ledMode, cstr, 10);
    request->send_P(200, "text/plane", cstr);
  });
//  server.on("/getLedMode", handleLedMode);//To get update of LED mode only

  // Send a GET request
  server.on("/setmode", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String inputMessage;
    String inputParam;

    if (request->hasParam("mode")) {
      inputMessage = request->getParam("mode")->value();
      inputParam = "mode";
      newMode = inputMessage.toInt();
      SetMode(newMode);
      }

    else {
      inputMessage = "No message sent";
      inputParam = "none";
    }
    request->send(200, "text/plain", "OK");
  });
  // Send a GET request
  server.on("/update", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String inputMessage;
    String inputParam;
    // GET input1 value on <ESP_IP>/update?state=<inputMessage>
    if (request->hasParam(PARAM_INPUT_1)) {
      inputMessage = request->getParam(PARAM_INPUT_1)->value();
      inputParam = PARAM_INPUT_1;
      if (inputMessage.toInt() == 1) {
        newMode = ledMode;
        if (++newMode >= maxMode) newMode = 0;
        //changedFromWeb = true;
        SetMode(newMode);
      }
    }
    else if (request->hasParam(PARAM_INPUT_2)) {
      inputMessage = request->getParam(PARAM_INPUT_2)->value();
      inputParam = PARAM_INPUT_2;
      if (inputMessage.toInt() == 1) {
        newMode = ledMode;
        if (--newMode <= 0) newMode = maxMode;
        //changedFromWeb = true;
        SetMode(newMode);
      }
    }
    else {
      inputMessage = "No message sent";
      inputParam = "none";
    }

    if (inputMessage.toInt() == 1) {
      Serial.println("ledmode is");
      Serial.println(ledMode);
    }
    request->send(200, "text/plain", "OK");
  });
  // Start server
  server.begin();

#if IR_ON == 1
  irrecv.enableIRIn();                                                          // Start the receiver
#endif

  LEDS.setBrightness(max_bright);                                                 // Set the generic maximum brightness value.

#if LED_CK

  LEDS.addLeds<CHIPSET, LED_DT, LED_CK, COLOR_ORDER>(leds, MAX_LEDS);

#else

  LEDS.addLeds<CHIPSET, LED_DT, COLOR_ORDER >(leds, MAX_LEDS);                   //Для светодиодов WS2812B

#endif

  set_max_power_in_volts_and_milliamps(POWER_V, POWER_I);                         //Настройка блока питания

  random16_set_seed(4832);                                                        // Awesome randomizer of awesomeness
  random16_add_entropy(analogRead(2));


#if IR_ON == 1

ledMode = EEPROM.read(STARTMODE);
// Location 0 is the starting mode.
NUM_LEDS = EEPROM.read(STRANDLEN); 
#if MAX_LEDS < 255
if (EEPROM.read(STRANDLEN+1))
NUM_LEDS = MAX_LEDS; // Need to ensure NUM_LEDS < MAX_LEDS elsewhere.
#else
  NUM_LEDS = (uint16_t)EEPROM.read(STRANDLEN + 1) << 8 +                               // Need to ensure NUM_LEDS < MAX_LEDS elsewhere.
             EEPROM.read(STRANDLEN);
#endif
  meshdelay = EEPROM.read(STRANDEL);                                              // This is our notamesh delay for cool delays across strands.

  if (  (EEPROM.read(ISINIT) != INITVAL) ||                                        // Check to see if Arduino has been initialized, and if not, do so.
        (NUM_LEDS > MAX_LEDS )  ||
        ((ledMode > maxMode) && (ledMode != 100) ) )
  {
    EEPROM.write(STARTMODE, INITMODE);                                            // Initialize the starting mode to 0.
#if MAX_LEDS < 255
    EEPROM.write(STRANDLEN, INITLEN);                                           // Initialize the starting length to 20 LED's.
#else
    EEPROM.write(STRANDLEN,   (uint16_t)(INITLEN) & 0x00ff);                      // Initialize the starting length to 20 LED's.
    EEPROM.write(STRANDLEN + 1, (uint16_t)(INITLEN) >> 8);                        // Initialize the starting length to 20 LED's.
#endif
    EEPROM.write(ISINIT, INITVAL);                                                // Initialize the starting value (so we know it's initialized) to INITVAL.
    EEPROM.write(STRANDEL, INITDEL);                                              // Initialize the notamesh delay to 0.
    ledMode = INITMODE;
    NUM_LEDS = INITLEN;
    meshdelay = INITDEL;
  }
#else
  ledMode = INITMODE;
  NUM_LEDS = KOL_LED;
  meshdelay = INITDEL;
#endif

#if LOG_ON == 1
  Serial.print(F("Initial delay: ")); Serial.print(meshdelay * 100); Serial.println(F("ms delay."));
  Serial.print(F("Initial strand length: ")); Serial.print(NUM_LEDS); Serial.println(F(" LEDs"));
#endif

#if BLACKSTART == 1
  solid = CRGB::Black;                 //Запуск с пустого поля
  newMode = ledMode;
  ledMode = 100;
  StepMode = 1;
#else
#if MY_MODE
  switch (demorun)  {
    case 3:   ledMode = pgm_read_byte(my_mode + tek_my_mode); break;            // демо 3
    case 4:   ledMode = pgm_read_byte(my_mode + random8(my_mode_count)); break;   // демо 4
  }
#endif
#endif
  gCurrentPalette = gGradientPalettes[0];
  gTargetPalette = gGradientPalettes[0];
  strobe_mode(ledMode, 1);                                                        // Initialize the first sequence

#if LOG_ON == 1
  if (DEMO_MODE) {
    Serial.print(F("DEMO MODE "));
    Serial.println(DEMO_MODE);
  }
  Serial.println(F("---SETUP COMPLETE---"));
#endif
} // setup()


bool onFlag = true;
//------------------MAIN LOOP---------------------------------------------------------------
void loop() {

  ArduinoOTA.handle();
//  delay(2);
/* 
#if (USE_BTN == 1)
  static bool stepFlag = false;
  static bool brightDir = true;
  btn.tick();
  if (btn.isSingle()) {
    onFlag = !onFlag;
    FastLED.setBrightness(onFlag ? max_bright : 0);
    FastLED.show();
  }
  if (btn.isDouble()) {
#if MY_MODE
    if (++tek_my_mode >= (my_mode_count - 1)) tek_my_mode = 0;
    newMode = pgm_read_byte(my_mode + tek_my_mode);
#else
    if (++newMode >= maxMode) newMode = 0;
#endif
    SetMode(newMode);
  }
  if (btn.isTriple()) {
#if MY_MODE
    if (--tek_my_mode <= 0) tek_my_mode = my_mode_count - 1;
    newMode = pgm_read_byte(my_mode + tek_my_mode);
#else
    if (--newMode <= 0) newMode = maxMode;
#endif
    SetMode(newMode);
  }
  if (btn.hasClicks()) {
    if (btn.getClicks() == 4) glitter = !glitter;
  }
  if (stepFlag && btn.isRelease()) {
    stepFlag = false;
    brightDir = !brightDir;
  }
  if (btn.isStep()) {
    stepFlag = true;
    max_bright += (brightDir ? 20 : -20);
    max_bright = constrain(max_bright, 0, 255);
    FastLED.setBrightness(max_bright);
  }
#endif
*/
#if ( IR_ON == 1 || KEY_ON == 1 || USE_BTN == 1 )
  getirl();                                                                   // Обработка команд с пульта и аналоговых кнопок
#endif

//if (changedFromWeb) {
//  SetMode(newMode);
//  changedFromWeb = false;
//}

  if (onFlag) {
    demo_check();                                                                 // Работа если включен демонстрационный режим

    EVERY_N_MILLISECONDS(50) {                                                  // Плавный переход от одной палитры в другую
      uint8_t maxChanges = 24;
#if CHANGE_ON == 1
      if (StepMode == MAX_LEDS )
#endif
        nblendPaletteTowardPalette(gCurrentPalette, gTargetPalette, maxChanges);
    }

#if PALETTE_TIME>0
    if (palchg) {
      EVERY_N_SECONDS(PALETTE_TIME) {                                            // Смена палитры
        if (palchg == 3) {
          if (gCurrentPaletteNumber < (gGradientPaletteCount - 1))  gCurrentPaletteNumber++;
          else                                                    gCurrentPaletteNumber = 0;
#if LOG_ON == 1
          Serial.print(F("New Palette: "));  Serial.println(gCurrentPaletteNumber);
#endif
        }
        gTargetPalette = gGradientPalettes[gCurrentPaletteNumber];                // We're just ensuring that the gTargetPalette WILL be assigned.
      }
    }
#endif

#if DIRECT_TIME > 0
    EVERY_N_SECONDS(DIRECT_TIME) {                                            // Меняем направление
      thisdir = thisdir * -1;
    }
#endif

    EVERY_N_MILLIS_I(thistimer, thisdelay) {                                    // Sets the original delay time.
      thistimer.setPeriod(thisdelay);                                           // This is how you update the delay value on the fly.
      KolLed = NUM_LEDS;                                                        // Выводим Эффект на все светодиоды
      strobe_mode(ledMode, 0);                                                  // отобразить эффект;
#if CHANGE_ON == 1
      if (StepMode < NUM_LEDS) {                                                // требуется наложить новый эффект
        KolLed = StepMode;                                                      // Выводим Эффект на все светодиоды
        if (StepMode > 10)  strobe_mode(newMode, 0);                            // отобразить эффект;
#if CHANGE_SPARK == 4
        sparkler(rand_spark);
#else
        sparkler(CHANGE_SPARK);                                                             // бенгальский огонь
#endif
      }
#endif
    }

#if CHANGE_ON == 1
    EVERY_N_MILLISECONDS(CHANGE_TIME * 1000 / NUM_LEDS) {                      // Движение плавной смены эффектов
      if ( StepMode < NUM_LEDS)
      { StepMode++;
        if (StepMode == 10) strobe_mode(newMode, 1);
        if (StepMode >= NUM_LEDS)
        { ledMode = newMode;
          StepMode = MAX_LEDS;
#if LOG_ON == 1
          Serial.println(F("End SetMode"));
#endif
        }
        nblendPaletteTowardPalette(gCurrentPalette, gTargetPalette, NUM_LEDS);
      }
    }
#endif

    if (glitter) addglitter(10);                                                // If the glitter flag is set, let's add some.
#if CANDLE_KOL >0
    if (candle)  addcandle();
#endif

    if (background) addbackground();                                            // Включить заполнение черного цвета фоном
  }

  static uint32_t showTimer = 0;
  if (onFlag && millis() - showTimer >= 10) {
    showTimer = millis();
    FastLED.show();
  }
} // loop()


//-------------------OTHER ROUTINES----------------------------------------------------------
void strobe_mode(uint8_t mode, bool mc) {                  // mc stands for 'Mode Change', where mc = 0 is strobe the routine, while mc = 1 is change the routine

  if (mc) {
    fill_solid(leds, NUM_LEDS, CRGB(0, 0, 0));                // Clean up the array for the first time through. Don't show display though, so you may have a smooth transition.
#if LOG_ON == 1
    Serial.print(F("Mode: "));
    Serial.println(mode);
    Serial.println(millis());
#endif
#if PALETTE_TIME>0
    if (palchg == 0) palchg = 3;
#else
    if (palchg == 0) palchg = 1;
#endif
  }


  switch (mode) {                                          // First time through a new mode, so let's initialize the variables for a given display.

    case  0: if (mc) {
        thisdelay = 10;
        palchg = 0;
      } blendwave(); break;
    case  1: if (mc) {
        thisdelay = 10;
        palchg = 0;
      } rainbow_beat(); break;
    case  2: if (mc) {
        thisdelay = 10;
        allfreq = 2;
        thisspeed = 1;
        thatspeed = 2;
        thishue = 0;
        thathue = 128;
        thisdir = 1;
        thisrot = 1;
        thatrot = 1;
        thiscutoff = 128;
        thatcutoff = 192;
      } two_sin(); break;
    case  3: if (mc) {
        thisdelay = 20;
        allfreq = 4;
        bgclr = 0;
        bgbri = 0;
        startindex = 64;
        thisinc = 2;
        thiscutoff = 224;
        thisphase = 0;
        thiscutoff = 224;
        thisrot = 0;
        thisspeed = 4;
        wavebright = 255;
      } one_sin_pal(); break;
    case  4: if (mc) {
        thisdelay = 10;
      } noise8_pal(); break;
    case  5: if (mc) {
        thisdelay = 10;
        allfreq = 4;
        thisspeed = -1;
        thatspeed = 0;
        thishue = 64;
        thathue = 192;
        thisdir = 1;
        thisrot = 0;
        thatrot = 0;
        thiscutoff = 64;
        thatcutoff = 192;
      } two_sin(); break;
    case  6: if (mc) {
        thisdelay = 20;
        allfreq = 10;
        bgclr = 64;
        bgbri = 4;
        startindex = 64;
        thisinc = 2;
        thiscutoff = 224;
        thisphase = 0;
        thiscutoff = 224;
        thisrot = 0;
        thisspeed = 4;
        wavebright = 255;
      } one_sin_pal(); break;
    case  7: if (mc) {
        thisdelay = 10;
        numdots = 2;
        thisfade = 16;
        thisbeat = 8;
        thisdiff = 64;
      } juggle_pal(); break;
    case  8: if (mc) {
        thisdelay = 40;
        thisindex = 128;
        thisdir = 1;
        thisrot = 0;
        bgclr = 200;
        bgbri = 6;
      } matrix_pal(); break;
    case  9: if (mc) {
        thisdelay = 10;
        allfreq = 6;
        thisspeed = 2;
        thatspeed = 3;
        thishue = 96;
        thathue = 224;
        thisdir = 1;
        thisrot = 0;
        thatrot = 0;
        thiscutoff = 64;
        thatcutoff = 64;
      } two_sin(); break;
    case 10: if (mc) {
        thisdelay = 20;
        allfreq = 16;
        bgclr = 0;
        bgbri = 0;
        startindex = 64;
        thisinc = 2;
        thiscutoff = 224;
        thisphase = 0;
        thiscutoff = 224;
        thisrot = 0;
        thisspeed = 4;
        wavebright = 255;
      } one_sin_pal(); break;
    case 11: if (mc) {
        thisdelay = 50;
        mul1 = 5;
        mul2 = 8;
        mul3 = 7;
      } three_sin_pal(); break;
    case 12: if (mc) {
        thisdelay = 10;
      } serendipitous_pal(); break;
    case 13: if (mc) {
        thisdelay = 20;
        allfreq = 8;
        bgclr = 0;
        bgbri = 4;
        startindex = 64;
        thisinc = 2;
        thiscutoff = 224;
        thisphase = 0;
        thiscutoff = 224;
        thisrot = 0;
        thisspeed = 4;
        wavebright = 255;
      } one_sin_pal(); break;
    case 14: if (mc) {
        thisdelay = 10;
        allfreq = 20;
        thisspeed = 2;
        thatspeed = -1;
        thishue = 24;
        thathue = 180;
        thisdir = 1;
        thisrot = 0;
        thatrot = 1;
        thiscutoff = 64;
        thatcutoff = 128;
      } two_sin(); break;
    case 15: if (mc) {
        thisdelay = 50;
        thisindex = 64;
        thisdir = -1;
        thisrot = 1;
        bgclr = 100;
        bgbri = 10;
      } matrix_pal(); break;
    case 16: if (mc) {
        thisdelay = 10;
      } noise8_pal(); break; // By: Andrew Tuline
    case 17: if (mc) {
        thisdelay = 10;
      } plasma(11, 23, 4, 18); break;
    case 18: if (mc) {
        thisdelay = 20;
        allfreq = 10;
        thisspeed = 1;
        thatspeed = -2;
        thishue = 48;
        thathue = 160;
        thisdir = -1;
        thisrot = 1;
        thatrot = -1;
        thiscutoff = 128;
        thatcutoff = 192;
      } two_sin(); break;
    case 19: if (mc) {
        thisdelay = 50;
        palchg = 0;
        thisdir = 1;
        thisrot = 1;
        thisdiff = 1;
      } rainbow_march(); break;
    case 20: if (mc) {
        thisdelay = 10;
        mul1 = 6;
        mul2 = 9;
        mul3 = 11;
      } three_sin_pal(); break;
    case 21: if (mc) {
        thisdelay = 10;
        palchg = 0;
        thisdir = 1;
        thisrot = 2;
        thisdiff = 10;
      } rainbow_march(); break;
    case 22: if (mc) {
        thisdelay = 20;
        palchg = 0;
        hxyinc = random16(1, 15);
        octaves = random16(1, 3);
        hue_octaves = random16(1, 5);
        hue_scale = random16(10, 50);
        x = random16();
        xscale = random16();
        hxy = random16();
        hue_time = random16();
        hue_speed = random16(1, 3);
        x_speed = random16(1, 30);
      } noise16_pal(); break;
    case 23: if (mc) {
        thisdelay = 20;
        allfreq = 6;
        bgclr = 0;
        bgbri = 0;
        startindex = 64;
        thisinc = 2;
        thiscutoff = 224;
        thisphase = 0;
        thiscutoff = 224;
        thisrot = 0;
        thisspeed = 4;
        wavebright = 255;
      } one_sin_pal(); break;
    case 24: if (mc) {
        thisdelay = 10;
      } plasma(23, 15, 6, 7); break;
    case 25: if (mc) {
        thisdelay = 20;
        thisinc = 1;
        thisfade = 2;
        thisdiff = 32;
      } confetti_pal(); break;
    case 26: if (mc) {
        thisdelay = 10;
        thisspeed = 2;
        thatspeed = 3;
        thishue = 96;
        thathue = 224;
        thisdir = 1;
        thisrot = 1;
        thatrot = 2;
        thiscutoff = 128;
        thatcutoff = 64;
      } two_sin(); break;
    case 27: if (mc) {
        thisdelay = 30;
        thisindex = 192;
        thisdir = -1;
        thisrot = 0;
        bgclr = 50;
        bgbri = 0;
      } matrix_pal(); break;
    case 28: if (mc) {
        thisdelay = 20;
        allfreq = 20;
        bgclr = 0;
        bgbri = 0;
        startindex = 64;
        thisinc = 2;
        thiscutoff = 224;
        thisphase = 0;
        thiscutoff = 224;
        thisrot = 0;
        thisspeed = 4;
        wavebright = 255;
      } one_sin_pal(); break;
    case 29: if (mc) {
        thisdelay = 20;
        thisinc = 2;
        thisfade = 8;
        thisdiff = 64;
      } confetti_pal(); break;
    case 30: if (mc) {
        thisdelay = 10;
      } plasma(8, 7, 9, 13); break;
    case 31: if (mc) {
        thisdelay = 10;
        numdots = 4;
        thisfade = 32;
        thisbeat = 12;
        thisdiff = 20;
      } juggle_pal(); break;
    case 32: if (mc) {
        thisdelay = 30;
        allfreq = 4;
        bgclr = 64;
        bgbri = 4;
        startindex = 64;
        thisinc = 2;
        thiscutoff = 224;
        thisphase = 0;
        thiscutoff = 128;
        thisrot = 1;
        thisspeed = 8;
        wavebright = 255;
      } one_sin_pal(); break;
    case 33: if (mc) {
        thisdelay = 50;
        mul1 = 3;
        mul2 = 4;
        mul3 = 5;
      } three_sin_pal(); break;
    case 34: if (mc) {
        thisdelay = 10;
        palchg = 0;
        thisdir = -1;
        thisrot = 1;
        thisdiff = 5;
      } rainbow_march(); break;
    case 35: if (mc) {
        thisdelay = 10;
      } plasma(11, 17, 20, 23); break;
    case 36: if (mc) {
        thisdelay = 20;
        thisinc = 1;
        thisfade = 1;
      } confetti_pal(); break;
    case 37: if (mc) {
        thisdelay = 20;
        palchg = 0;
        octaves = 1;
        hue_octaves = 2;
        hxy = 6000;
        x = 5000;
        xscale = 3000;
        hue_scale = 50;
        hue_speed = 15;
        x_speed = 100;
      } noise16_pal(); break;
    case 38: if (mc) {
        thisdelay = 10;
      } noise8_pal(); break;
    case 39: if (mc) {
        thisdelay = 10;
        palchg = 0;
      } fire(); break;
    case 40: if (mc) {
        thisdelay = 10;
        palchg = 0;
      } candles(); break;
    case 41: if (mc) {
        thisdelay = 10;
      } colorwaves(); break;
    case 100: if (mc) {
        palchg = 0;
      } fill_solid(leds, NUM_LEDS,  solid); break;    //Установить цвет
    case 200: fill_solid(leds, MAX_LEDS, CRGB::Black); fill_solid(leds, NUM_LEDS, CRGB(255, 255, 255)); break; //Зажеч гирлянду длинной NUM_LEDS (регулировка длинны гирлянды)
    case 201: fill_solid(leds, MAX_LEDS, CRGB::Black); fill_solid(leds, meshdelay, CRGB(255, 255, 255)); break; //Зажеч гирлянду длинной meshdelay
    default : ledMode = 0;  break;        //нет такого режима принудительно ставим нулевой

  } // switch mode

#if LOG_ON == 1
  if (mc) {
    if ( palchg == 0 ) Serial.println(F("Change palette off"));
    else if ( palchg == 1 ) Serial.println(F("Change palette Stop"));
    else if ( palchg == 3 ) Serial.println(F("Change palette ON"));
  }
#endif

} // strobe_mode()

void demo_check() {

  if (demorun) {
    if ((millis() - demo_time) >= (DEMO_TIME * 1000L )) {         //Наступило время смены эффекта
      demo_time = millis();                                       //Запомним время
      gCurrentPaletteNumber = random8(0, gGradientPaletteCount);  //Случайно поменяем палитру
#if CHANGE_ON == 1
      switch (demorun)  {
        case 2:   newMode = random8(0, maxMode);                // демо 2
          break;
#if MY_MODE
        case 3:   if (tek_my_mode >= (my_mode_count - 1)) tek_my_mode = 0; // демо 3
          else tek_my_mode++;
          newMode = pgm_read_byte(my_mode + tek_my_mode);
          break;
        case 4:   newMode = pgm_read_byte(my_mode + random8(my_mode_count));  // демо 4
          break;
#endif
        default : if (newMode >= maxMode) newMode = 0;          // демо 1
          else newMode++;
          break;
      }
      StepMode = 1;
#if CHANGE_SPARK == 4
      rand_spark = random8(3) + 1;
#endif

#if LOG_ON == 1
      Serial.println(F("Start SetMode"));
#endif
#else
      gTargetPalette = gGradientPalettes[gCurrentPaletteNumber];  //Применим палитру
#if LOG_ON == 1
      Serial.print(F("New Palette: "));  Serial.println(gCurrentPaletteNumber);
#endif
      switch (demorun)  {
        case 2:   ledMode = random8(0, maxMode);                // демо 2
          break;
#if MY_MODE
        case 3:   if (tek_my_mode >= (my_mode_count - 1)) tek_my_mode = 0; // демо 3
          else tek_my_mode++;
          ledMode = pgm_read_byte(my_mode + tek_my_mode);
          break;
        case 4:   ledMode = pgm_read_byte(my_mode + random8(my_mode_count));  // демо 4
          break;
#endif
        default : if (ledMode >= maxMode) ledMode = 0;          // демо 1
          else ledMode++;
          break;
      }
      strobe_mode(ledMode, 1);                                // Does NOT reset to 0.
#if CANDLE_KOL >0
      PolCandle = random8(CANDLE_KOL);
#endif
#endif
    } // if lastSecond
  } // if demorun

} // demo_check()
