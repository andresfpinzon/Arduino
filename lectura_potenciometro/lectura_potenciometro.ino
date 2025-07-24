#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "PruebaPot";
const char* password = "12345678";

const int potPin = 39;
int potValor = 0;

WebServer server(80);

const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Lectura Potenciometro</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; text-align: center; padding: 30px; }
    h1 { font-size: 24px; }
    #valor { font-size: 48px; color: #2c3e50; }
  </style>
</head>
<body>
  <h1>Lectura del Potenciometro</h1>
  <div id="valor">--</div>

  <script>
    setInterval(() => {
      fetch("/pot")
        .then(res => res.json())
        .then(data => {
          document.getElementById("valor").innerText = data.valor;
        });
    }, 1000);
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1000);

  analogReadResolution(12); // 0 - 4095

  WiFi.softAP(ssid, password);
  Serial.print("Red WiFi activa. IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", []() {
    server.send_P(200, "text/html", htmlPage);
  });

  server.on("/pot", []() {
    potValor = analogRead(potPin);
    server.send(200, "application/json", "{\"valor\":" + String(potValor) + "}");
  });

  server.begin();
}

void loop() {
  server.handleClient();
}

