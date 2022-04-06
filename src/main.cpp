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
                    <input type="range" id="temperature" min="70" max="100" step="1" name="input_temperature" required>
                </p>
                <p>
                    <button id="button" class="button"
                        onclick="document.getElementById('postform').submit();">Zagotuj</button>
                </p>
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
    }
    </script>
</body>
</html>
)rawliteral";
const char* TEMPERATURE_INPUT_VAR = "input_temperature";
String inputTemperature = "70";
String currentTemperature = "None";
AsyncWebServer server(80);
AsyncEventSource events("/events");

/* WiFi variables */
const char* ssid = "SSID";
const char* password = "PASSWORD";

/* Temperature sensor variables */
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

/* System variables */
unsigned long previousTimestamp = 0;

/* ------------------------------------------------------------------------------ */
/* Private Definitions */
/* ------------------------------------------------------------------------------ */

void updateTemperature(String input)
{
    currentTemperature = input;
    events.send(currentTemperature.c_str(), "update", millis());
}

void notFound(AsyncWebServerRequest *request)
{
    request->send(404, "text/plain", "ERROR 404: Page not found!");
}

bool setupWifi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    return WiFi.waitForConnectResult() == WL_CONNECTED;
}

void setupWebServer()
{
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", index_html);
    });

    server.on("/", HTTP_POST, [] (AsyncWebServerRequest *request) {
        for (int param = 0; param < request->params(); param++)
        {
            AsyncWebParameter* p = request->getParam(param);
            if (p->isPost())
            {
                if (p->name() == TEMPERATURE_INPUT_VAR)
                {
                    inputTemperature = p->value();
                }
                /* TODO: add test trigger */
            }
        }
        request->send(200, "text/html", index_html);
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

void setup()
{
    Serial.begin(115200);

    sensors.begin();

    if (!setupWifi())
    {
        Serial.println("WiFi Failed!");
        return;
    }
    Serial.printf("\n Connected to http://%s\n", WiFi.localIP().toString().c_str());

    setupWebServer();
}

void loop()
{
    unsigned long currentTimestamp = millis();
    if (currentTimestamp - previousTimestamp >= LOOP_WAIT_MS)
    {
        previousTimestamp = currentTimestamp;
        sensors.requestTemperatures();
        // float temperature = sensors.getTempCByIndex(TEMPERATURE_SENSOR_INDEX);

        /* TODO: temperature algo */
        updateTemperature(inputTemperature);
    }
}
