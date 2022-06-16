/* ------------------------------------------------------------------------------ */
/* Includes */
/* ------------------------------------------------------------------------------ */

#include <Arduino.h>

/* Web server includes */
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

/* DS18B20 includes */
#include <OneWire.h>
#include <DallasTemperature.h>

/* ------------------------------------------------------------------------------ */
/* Macros */
/* ------------------------------------------------------------------------------ */

#define LOOP_WAIT_MS 200
#define ONE_WIRE_PIN 4
#define TEMPERATURE_SENSOR_INDEX 0

/* ------------------------------------------------------------------------------ */
/* Variables */
/* ------------------------------------------------------------------------------ */

/* WiFi variables */
const char* ssid = "SSID";
const char* password = "PASSWORD";

/* WebServer variables */
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <style>
        html {
            font-family: Arial, Helvetica, sans-serif;
            text-align: center;
            background-color: #161616;
        }
        h1 {
            font-size: 1.8rem;
            color: white;
        }
        h2 {
            font-size: 1.5rem;
            font-weight: bold;
            color: white;
        }
        .topnav {
            overflow: hidden;
            background-color: navy;
        }
        body {
            margin: 0;
        }
        .content {
            padding: 30px;
            max-width: 600px;
            margin: 0 auto;
        }
        .card {
            background-color: #202020;
            padding-top: 10px;
            padding-bottom: 20px;
            border-radius: 10px;
        }
        .button {
            padding: 15px 50px;
            font-size: 24px;
            text-align: center;
            outline: none;
            color: white;
            background-color: navy;
            border: none;
            border-radius: 10px;
        }
        .button:hover {
            background-color: midnightblue;
        }
        .button:active {
            background-color: midnightblue;
            transform: translateY(2px);
        }
    </style>
    <title>SmartCzajnik</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
</head>
<body>
    <div class="topnav">
        <h1>Obecna temperatura wody: <output id="output_temperature_display" value="Add order" ></output> &deg;C</h1>
    </div>
    <div class="content">
        <div class="card">
            <form id="postform" action="/" method="POST" oninput="input_temperature_display.value=parseInt(temperature.value)">
                <h2>
                    Temperatura: <output name="input_temperature_display" for="temperature"></output>
                </h2>
                <p>
                    <input type="range" id="temperature" min="20" max="100" step="1" name="input_temperature" required>
                </p>
                <p>
                    <button id="button" class="button"
                        onclick="document.getElementById('postform').submit();">Zagotuj</button>
                </p>
                <h2>%TEMPSTATE%</h2>
            </form>
        </div>
    </div>
    <div class="content">
        <div class="card">
            <form id="test_postform" action="/" method="POST" oninput="test_input_temperature_display.value=parseInt(test_temperature.value)">
                <h2>
                    Temperatura testu: <output name="test_input_temperature_display" for="test_temperature"></output>
                </h2>
                <p>
                    <input type="range" id="test_temperature" min="20" max="100" step="1" name="test_input_temperature" required>
                </p>
                <p>
                    <button id="button" class="button"
                        onclick="document.getElementById('test_postform').submit();">Rozpocznij test</button>
                </p>
                <h2>%TESTSTATE%</h2>
            </form>
        </div>
    </div>
    <script>
    if (!!window.EventSource) {
        var source = new EventSource('/events');
        source.addEventListener('update', function(e) {
            var newData = JSON.parse(e.data);
            console.log(newData);
            document.getElementById("output_temperature_display").innerHTML = newData;
            }, false);
        source.addEventListener('reload', function(e) {
            window.location.href="../";
            }, false);
    }
    </script>
</body>
</html>
)rawliteral";
const char* TEMPERATURE_INPUT_VAR = "input_temperature";
const char* TEST_TEMPERATURE_INPUT_VAR = "test_input_temperature";
String inputTemperature = "None";
String currentTemperature = "None";
AsyncWebServer server(80);
AsyncEventSource events("/events");

/* Temperature sensor variables */
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

/* System variables */
unsigned long previousTimestamp = 0;
bool testInProgress = false;
bool testDone = false;
unsigned long testMillis = 0;
unsigned long lastTempTimestamp = 0;
unsigned long lastChangeMillis = 0;
int lastTemp = 0;
bool noWater = false;

typedef struct {
    bool isUp;
    const int index;
} GPIOpin;

GPIOpin heatPin = {
    .isUp = false,
    .index = 2
};

GPIOpin wifiPin = {
    .isUp = false,
    .index = 15
};

GPIOpin waterPin = {
    .isUp = false,
    .index = 5
};

/* ------------------------------------------------------------------------------ */
/* Private Definitions */
/* ------------------------------------------------------------------------------ */

String processor(const String& var) {
    if (var == "TEMPSTATE") {
        if (testInProgress) {
            return "";
        } else if (inputTemperature == "None") {
            if (noWater) {
                return "Brakuje wody!";
            } else {
                return "Oczekuje na polecenie";
            }
        } else {
            return "Gotuje do " + inputTemperature + " stopni.";
        }
    } else if (var == "TESTSTATE") {
        if (testInProgress) {
            return "Trwa testowanie. Gotuje do " + inputTemperature + " stopni.";
        } else if (testDone) {
            if (noWater) {
                return "Brakuje wody! Test zakonczony: " + String(testMillis) + "ms";
            } else {
                return "Test zakonczony: " + String(testMillis) + "ms";
            }
        } else {
            return "";
        }
    }
    return "Wystapil blad";
}

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "ERROR 404: Page not found!");
}

bool setupWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    return WiFi.waitForConnectResult() == WL_CONNECTED;
}

void setupWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", index_html, processor);
    });

    server.on("/", HTTP_POST, [] (AsyncWebServerRequest *request) {
        if (!testInProgress) {
            for (int param = 0; param < request->params(); param++) {
                AsyncWebParameter* p = request->getParam(param);
                if (p->isPost()) {
                    if (p->name() == TEMPERATURE_INPUT_VAR) {
                        inputTemperature = p->value();
                    } else if (p->name() == TEST_TEMPERATURE_INPUT_VAR) {
                        inputTemperature = p->value();
                        testInProgress = true;
                        testMillis = millis();
                    }
                }
            }
        }
        request->send_P(200, "text/html", index_html, processor);
    });

    events.onConnect([](AsyncEventSourceClient *client) {
        /* send current data */
        client->send(currentTemperature.c_str(), "update", millis());
        /* maintain heartbeat */
        client->send("ping", NULL, millis(), 10000);
    });

    server.addHandler(&events);
    server.onNotFound(notFound);
    server.begin();
}

void setup() {
    pinMode(heatPin.index, OUTPUT);
    digitalWrite(heatPin.index, LOW);
    pinMode(wifiPin.index, OUTPUT);
    digitalWrite(wifiPin.index, LOW);
    pinMode(waterPin.index, OUTPUT);
    digitalWrite(waterPin.index, LOW);

    Serial.begin(115200);

    sensors.begin();

    if (!setupWifi()) {
        Serial.println("WiFi Failed!");
        return;
    }
    Serial.printf("\n Connected to http://%s\n", WiFi.localIP().toString().c_str());
    digitalWrite(wifiPin.index, HIGH);
    wifiPin.isUp = true;

    setupWebServer();
}

void updateTemperature(int input) {
    currentTemperature = String(input);
    events.send(currentTemperature.c_str(), "update", millis());
}

void endHeat(bool success) {
    noWater = !success;
    if (noWater) {
        digitalWrite(waterPin.index, HIGH);
    } else {
        digitalWrite(waterPin.index, LOW);
    }
    inputTemperature = "None";
    digitalWrite(heatPin.index, LOW);
    heatPin.isUp = false;
    if (testInProgress) {
        testInProgress = false;
        testMillis = millis() - testMillis;
        testDone = true;
    }
    events.send("reload", "reload", millis());
}

void startHeat() {
    digitalWrite(heatPin.index, HIGH);
    lastChangeMillis = 0;
    lastTempTimestamp = millis();
    heatPin.isUp = true;
    events.send("reload", "reload", millis());
}

void loop() {
    unsigned long currentTimestamp = millis();
    if (currentTimestamp - previousTimestamp >= LOOP_WAIT_MS) {
        previousTimestamp = currentTimestamp;
        sensors.requestTemperatures();
        int temperature = static_cast<int>(sensors.getTempCByIndex(TEMPERATURE_SENSOR_INDEX));
        updateTemperature(temperature);
        if (lastTemp != temperature) {
            lastTemp = temperature;
            lastTempTimestamp = millis();
            lastChangeMillis = 0;
        } else {
            lastChangeMillis = millis() - lastTempTimestamp;
        }
        if (inputTemperature != "None") {
            if (temperature >= inputTemperature.toInt()) {
                endHeat(true);
            } else {
                if (!heatPin.isUp) {
                    startHeat();
                } else if (lastChangeMillis > 8000) {
                    endHeat(false);
                }
            }
        }
    }
}
