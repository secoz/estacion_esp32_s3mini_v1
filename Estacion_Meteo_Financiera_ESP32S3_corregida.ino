
/*
  ================================================
  ESTACION METEOROLOGICA Y FINANCIERA
  ESP32-S3 SUPER MINI
  OLED SH1106 SPI 128x64 - 7 PINES
  SENSOR DHT11
  SENSOR BMP280

  FUNCIONES:
  1. Clima de Internet
  2. Dolar / Peso mexicano - 30 dias
  3. Yen / Peso mexicano - 30 dias
  4. CETES 28 dias - 30 dias
  5. Bolsa Mexicana IPC
  6. Sensor DHT11
  7. Sensor BMP280
  8. Estado del sistema

  Rotacion automatica cada 6 segundos.
  ================================================
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <Adafruit_BMP280.h>
#include <Wire.h>
#include <time.h>

// ================================================
// 1. CONFIGURACION WIFI Y BANXICO
// ================================================

const char* WIFI_SSID = "MEGACABLE-2.4G-3BA5";
const char* WIFI_PASS = "Chavalina1610";
const char* BANXICO_TOKEN = "f400e827132b30c89dc38dead6bce00446b8bb90c3a8842981596918a88fd087";


// Coordenadas aproximadas de Salamanca, Guanajuato
const float LATITUD = 20.5736;
const float LONGITUD = -101.1917;
// ================================================
// 2. PINES ESP32-S3 SUPER MINI
// ================================================

// OLED SH1106 SPI
#define OLED_CLK  12
#define OLED_MOSI 11
#define OLED_CS   8
#define OLED_DC   9
#define OLED_RST  10

// Sensor DHT11
#define DHT_PIN 4
#define DHT_TYPE DHT11

// Sensor BMP280 I2C
#define I2C_SDA 6
#define I2C_SCL 7

// ================================================
// 3. PANTALLA OLED SH1106
// ================================================

U8G2_SH1106_128X64_NONAME_F_4W_SW_SPI oled(
  U8G2_R0,
  OLED_CLK,
  OLED_MOSI,
  OLED_CS,
  OLED_DC,
  OLED_RST
);

// ================================================
// 4. SENSORES
// ================================================

DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_BMP280 bmp;
bool bmpOK = false;
bool wifiOK = false;
// ================================================
// 5. VARIABLES
// ================================================
// DHT11
float dhtTemp = NAN;
float dhtHum = NAN;
// BMP280
float bmpTemp = NAN;
float bmpPressure = NAN;
// Clima Internet
float weatherTemp = NAN;
float weatherHum = NAN;
float weatherWind = NAN;

int weatherCode = -1;

String weatherDescription = "Sin datos";

// Dolar
float usdMXN = NAN;

// Yen
float jpyMXN = NAN;

// CETES
float cetes = NAN;

// Bolsa Mexicana
float ipc = NAN;
float ipcChange = NAN;

long ipcVolume = 0;

// ================================================
// 6. HISTORIALES DE 30 DIAS
// ================================================

const int MAX_DATA = 35;

float usdHistory[MAX_DATA];
float jpyHistory[MAX_DATA];
float cetesHistory[MAX_DATA];

int usdCount = 0;
int jpyCount = 0;
int cetesCount = 0;

// ================================================
// 7. TEMPORIZADORES
// ================================================

unsigned long lastPage = 0;
unsigned long lastSensors = 0;
unsigned long lastWeather = 0;
unsigned long lastFinance = 0;
unsigned long lastWifiAttempt = 0;

const unsigned long PAGE_TIME = 6000;
const unsigned long SENSOR_TIME = 3000;
const unsigned long WEATHER_TIME = 900000;
const unsigned long FINANCE_TIME = 1800000;
const unsigned long WIFI_RETRY_TIME = 30000;

int currentPage = 0;
const int TOTAL_PAGES = 8;

// ================================================
// 8. FECHA Y HORA
// ================================================

String fechaISO(int daysAgo) {
  time_t now = time(nullptr);
  now -= (time_t)daysAgo * 86400;
  struct tm t;
  gmtime_r(&now, &t);
  char buffer[12];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d", &t);
  return String(buffer);
}

String fechaActual() {
  struct tm t;
  if (!getLocalTime(&t, 100)) {
    return "Sin fecha";
  }

  char buffer[20];
  strftime(buffer, sizeof(buffer), "%d/%m/%Y", &t);
  return String(buffer);
}

// ================================================
// 9. CONEXION WIFI
// ================================================

void conectarWiFi() {

  if (WiFi.status() == WL_CONNECTED) {
    wifiOK = true;
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("Conectando a WiFi...");

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - inicio < 15000) {
    delay(300);

    Serial.print(".");
  }

  Serial.println();
  wifiOK = WiFi.status() == WL_CONNECTED;
  if (wifiOK) {
    Serial.println("WiFi conectado");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    configTzTime(
      "CST6",
      "pool.ntp.org",
      "time.google.com"
    );

  } else {

    Serial.println("No se pudo conectar a WiFi");
  }
}

// ================================================
// 10. DESCARGAR DATOS JSON
// ================================================

bool descargarJSON(
  String url,
  JsonDocument &doc,
  String token = ""
) {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("JSON: WiFi desconectado");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();  // Pruebas; validar certificados en una instalacion definitiva.

  HTTPClient http;
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(client, url)) {
    Serial.println("JSON: no se pudo iniciar HTTPS");
    return false;
  }

  http.addHeader("User-Agent", "ESP32-S3-Super-Mini");
  http.addHeader("Accept", "application/json");

  if (token.length() > 0) {
    http.addHeader("Bmx-Token", token);
  }

  Serial.println("Solicitando: " + url);
  int codigo = http.GET();
  bool resultado = false;

  if (codigo == HTTP_CODE_OK) {
    // Igual que en la prueba independiente: primero descargar el cuerpo completo
    // y despues entregarlo a ArduinoJson.
    String respuesta = http.getString();

    if (respuesta.length() == 0) {
      Serial.println("JSON: respuesta vacia");
    } else {
      DeserializationError error = deserializeJson(doc, respuesta);
      if (!error) {
        resultado = true;
      } else {
        Serial.print("Error al interpretar JSON: ");
        Serial.println(error.c_str());
        Serial.print("Inicio de respuesta: ");
        Serial.println(respuesta.substring(0, 180));
      }
    }
  } else {
    Serial.print("Error HTTP: ");
    Serial.println(codigo);
    if (codigo < 0) {
      Serial.println(http.errorToString(codigo));
    } else {
      Serial.println(http.getString().substring(0, 250));
    }
  }

  http.end();
  return resultado;
}

// ================================================
// 11. OBTENER CLIMA DE INTERNET
// ================================================

void obtenerClima() {

  String url =
    "https://api.open-meteo.com/v1/forecast";

  url += "?latitude=" + String(LATITUD, 4);

  url += "&longitude=" + String(LONGITUD, 4);

  url += "&current=temperature_2m,relative_humidity_2m,";
  url += "apparent_temperature,precipitation,weather_code,";
  url += "wind_speed_10m";

  url += "&timezone=America%2FMexico_City";

  JsonDocument doc;

  if (!descargarJSON(url, doc)) {

    Serial.println("Error obteniendo clima");

    return;
  }

  JsonObject actual = doc["current"];

  if (actual.isNull()) {
    Serial.println("Clima: la respuesta no contiene el objeto current");
    Serial.println("Revisa la respuesta JSON de Open-Meteo en el Monitor Serial");
    return;
  }

  weatherTemp =
    actual["temperature_2m"] | NAN;

  weatherHum =
    actual["relative_humidity_2m"] | NAN;

  weatherWind =
    actual["wind_speed_10m"] | NAN;

  weatherCode =
    actual["weather_code"] | -1;

  switch (weatherCode) {

    case 0:
      weatherDescription = "Despejado";
      break;

    case 1:
    case 2:
      weatherDescription = "Poco nublado";
      break;

    case 3:
      weatherDescription = "Nublado";
      break;

    case 45:
    case 48:
      weatherDescription = "Niebla";
      break;

    case 51:
    case 53:
    case 55:
    case 61:
    case 63:
    case 65:
      weatherDescription = "Lluvia";
      break;

    case 71:
    case 73:
    case 75:
      weatherDescription = "Nieve";
      break;

    case 80:
    case 81:
    case 82:
      weatherDescription = "Chubascos";
      break;

    case 95:
    case 96:
    case 99:
      weatherDescription = "Tormenta";
      break;

    default:
      weatherDescription = "Variable";
      break;
  }

  if (!isfinite(weatherTemp) || !isfinite(weatherHum)) {
    Serial.println("Clima: se recibio JSON, pero faltan temperatura o humedad");
    return;
  }

  Serial.println("Clima de Salamanca actualizado correctamente");
  Serial.print("Temperatura: ");
  Serial.print(weatherTemp, 1);
  Serial.println(" C");
  Serial.print("Humedad: ");
  Serial.print(weatherHum, 0);
  Serial.println(" %");
  Serial.print("Viento: ");
  Serial.print(weatherWind, 1);
  Serial.println(" km/h");
  Serial.print("Condicion: ");
  Serial.println(weatherDescription);
}

// ================================================
// 12. AGREGAR DATOS A HISTORIAL
// ================================================

void agregarDato(
  float* array,
  int &count,
  float value
) {

  if (!isfinite(value)) return;

  if (count < MAX_DATA) {

    array[count] = value;

    count++;

  } else {

    for (int i = 1; i < MAX_DATA; i++) {

      array[i - 1] = array[i];
    }

    array[MAX_DATA - 1] = value;
  }
}

// ================================================
// 13. DOLAR VS PESO MEXICANO
// ================================================

void obtenerDolar() {

  String inicio = fechaISO(30);

  String fin = fechaISO(0);

  String url =
    "https://api.frankfurter.dev/v1/";

  url += inicio;

  url += "..";

  url += fin;

  url += "?base=USD&symbols=MXN";

  JsonDocument doc;

  if (!descargarJSON(url, doc)) {

    Serial.println("Error cotizacion dolar");

    return;
  }

  JsonObject rates = doc["rates"];

  if (rates.isNull()) return;

  usdCount = 0;

  usdMXN = NAN;

  for (JsonPair item : rates) {

    JsonObject valor = item.value();

    float cambio = valor["MXN"] | NAN;

    if (isfinite(cambio)) {

      agregarDato(
        usdHistory,
        usdCount,
        cambio
      );

      usdMXN = cambio;
    }
  }

  Serial.print("USD/MXN: ");

  Serial.println(usdMXN, 4);
}

// ================================================
// 14. YEN VS PESO MEXICANO
// ================================================

void obtenerYen() {

  String inicio = fechaISO(30);

  String fin = fechaISO(0);

  String url =
    "https://api.frankfurter.dev/v1/";

  url += inicio;

  url += "..";

  url += fin;

  url += "?base=JPY&symbols=MXN";

  JsonDocument doc;

  if (!descargarJSON(url, doc)) {

    Serial.println("Error cotizacion yen");

    return;
  }

  JsonObject rates = doc["rates"];

  if (rates.isNull()) return;

  jpyCount = 0;

  jpyMXN = NAN;

  for (JsonPair item : rates) {

    JsonObject valor = item.value();

    float cambio = valor["MXN"] | NAN;

    if (isfinite(cambio)) {

      agregarDato(
        jpyHistory,
        jpyCount,
        cambio
      );

      jpyMXN = cambio;
    }
  }

  Serial.print("JPY/MXN: ");

  Serial.println(jpyMXN, 5);
}

// ================================================
// 15. CETES 28 DIAS - BANXICO
// ================================================

void obtenerCetes() {

  String inicio = fechaISO(30);

  String fin = fechaISO(0);

  String url =
    "https://www.banxico.org.mx/SieAPIRest/service/v1/";

  url += "series/SF60633/datos/";

  url += inicio;

  url += "/";

  url += fin;

  JsonDocument doc;

  if (!descargarJSON(
    url,
    doc,
    BANXICO_TOKEN
  )) {

    Serial.println("Error CETES");

    return;
  }

  JsonArray datos =
    doc["bmx"]["series"][0]["datos"];

  if (datos.isNull()) return;

  cetesCount = 0;

  cetes = NAN;

  for (JsonObject dato : datos) {

    String valor = dato["dato"] | "";

    if (valor.length() > 0 &&
        valor != "N/E") {

      float v = valor.toFloat();

      if (v > 0) {

        agregarDato(
          cetesHistory,
          cetesCount,
          v
        );

        cetes = v;
      }
    }
  }

  Serial.print("CETES 28 dias: ");

  Serial.println(cetes);
}

// ================================================
// 16. BOLSA MEXICANA DE VALORES IPC
// ================================================

void obtenerIPC() {

  String url =
    "https://query1.finance.yahoo.com/v8/finance/chart/%5EMXX";

  url += "?range=1mo&interval=1d";

  JsonDocument doc;

  if (!descargarJSON(url, doc)) {

    Serial.println("Error IPC");

    return;
  }

  JsonObject result =
    doc["chart"]["result"][0];

  if (result.isNull()) return;

  JsonObject meta = result["meta"];

  ipc = meta["regularMarketPrice"] | NAN;

  float anterior =
    meta["chartPreviousClose"] |
    meta["previousClose"] | NAN;

  if (isfinite(ipc) &&
      isfinite(anterior) &&
      anterior != 0) {

    ipcChange =
      ((ipc - anterior) / anterior) * 100.0;
  }

  JsonVariant vol =
    result["indicators"]["quote"][0]["volume"];

  if (vol.is<JsonArray>()) {

    JsonArray volumes = vol.as<JsonArray>();

    if (volumes.size() > 0) {

      ipcVolume =
        volumes[volumes.size() - 1] | 0;
    }
  }

  Serial.print("IPC: ");

  Serial.println(ipc);
}

// ================================================
// 17. ACTUALIZAR FINANZAS
// ================================================

void actualizarFinanzas() {

  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("Actualizando datos financieros...");

  obtenerDolar();

  obtenerYen();

  obtenerCetes();

  obtenerIPC();

  Serial.println("Fin actualizacion financiera");
}

// ================================================
// 18. LECTURA DE SENSORES
// ================================================

void leerSensores() {

  float t = dht.readTemperature();

  float h = dht.readHumidity();

  if (isfinite(t)) {

    dhtTemp = t;
  }

  if (isfinite(h)) {

    dhtHum = h;
  }

  if (bmpOK) {

    float bt = bmp.readTemperature();

    float bp = bmp.readPressure() / 100.0;

    if (isfinite(bt)) {

      bmpTemp = bt;
    }

    if (isfinite(bp)) {

      bmpPressure = bp;
    }
  }
}

// ================================================
// 19. GRAFICAS OLED
// ================================================

void dibujarGrafica(
  float* data,
  int count
) {

  const int x0 = 4;

  const int y0 = 63;

  const int ancho = 120;

  const int alto = 22;

  oled.drawFrame(
    x0,
    y0 - alto,
    ancho,
    alto
  );

  if (count < 2) {

    oled.setFont(u8g2_font_5x8_tf);

    oled.drawStr(
      15,
      54,
      "Esperando datos..."
    );

    return;
  }

  float minimo = data[0];

  float maximo = data[0];

  for (int i = 1; i < count; i++) {

    if (data[i] < minimo) minimo = data[i];

    if (data[i] > maximo) maximo = data[i];
  }

  float rango = maximo - minimo;

  if (rango < 0.000001) {

    rango = 0.01;
  }

  int px = 0;

  int py = 0;

  for (int i = 0; i < count; i++) {

    int x = x0 + 1 +
      (i * (ancho - 3)) / (count - 1);

    int y = y0 - 2 -
      ((data[i] - minimo) / rango) * (alto - 4);

    if (i > 0) {

      oled.drawLine(
        px,
        py,
        x,
        y
      );
    }

    px = x;

    py = y;
  }
}

// ================================================
// 20. PAGINA CLIMA INTERNET
// ================================================

void paginaClima() {

  oled.setFont(u8g2_font_6x12_tf);

  oled.drawStr(0, 10, "CLIMA INTERNET");

  oled.drawHLine(0, 13, 128);

  oled.setFont(u8g2_font_7x14B_tf);

  oled.setCursor(0, 31);

  if (isfinite(weatherTemp)) {

    oled.print(weatherTemp, 1);

    oled.print(" C");

  } else {

    oled.print("Sin datos");
  }

  oled.setFont(u8g2_font_6x12_tf);

  oled.setCursor(0, 45);

  oled.print(weatherDescription);

  oled.setCursor(0, 59);

  oled.print("H:");

  if (isfinite(weatherHum)) {

    oled.print(weatherHum, 0);

  } else {

    oled.print("--");
  }

  oled.print("% V:");

  if (isfinite(weatherWind)) {

    oled.print(weatherWind, 1);

  } else {

    oled.print("--");
  }

  oled.print("km/h");
}

// ================================================
// 21. PAGINA DOLAR
// ================================================

void paginaDolar() {

  oled.setFont(u8g2_font_6x12_tf);

  oled.drawStr(0, 10, "DOLAR / PESO MXN");

  oled.drawHLine(0, 13, 128);

  oled.setFont(u8g2_font_7x14B_tf);

  oled.setCursor(0, 29);

  if (isfinite(usdMXN)) {

    oled.print("$");

    oled.print(usdMXN, 4);

  } else {

    oled.print("Sin datos");
  }

  oled.setFont(u8g2_font_5x8_tf);

  oled.drawStr(0, 38, "HISTORIAL 30 DIAS");

  dibujarGrafica(
    usdHistory,
    usdCount
  );
}

// ================================================
// 22. PAGINA YEN
// ================================================

void paginaYen() {

  oled.setFont(u8g2_font_6x12_tf);

  oled.drawStr(0, 10, "YEN / PESO MXN");

  oled.drawHLine(0, 13, 128);

  oled.setFont(u8g2_font_7x14B_tf);

  oled.setCursor(0, 29);

  if (isfinite(jpyMXN)) {

    oled.print("$");

    oled.print(jpyMXN, 5);

  } else {

    oled.print("Sin datos");
  }

  oled.setFont(u8g2_font_5x8_tf);

  oled.drawStr(0, 38, "HISTORIAL 30 DIAS");

  dibujarGrafica(
    jpyHistory,
    jpyCount
  );
}

// ================================================
// 23. PAGINA CETES
// ================================================

void paginaCetes() {

  oled.setFont(u8g2_font_6x12_tf);

  oled.drawStr(0, 10, "CETES 28 DIAS");

  oled.drawHLine(0, 13, 128);

  oled.setFont(u8g2_font_7x14B_tf);

  oled.setCursor(0, 29);

  if (isfinite(cetes)) {

    oled.print(cetes, 2);

    oled.print(" %");

  } else {

    oled.print("Sin datos");
  }

  oled.setFont(u8g2_font_5x8_tf);

  oled.drawStr(0, 38, "HISTORIAL 30 DIAS");

  dibujarGrafica(
    cetesHistory,
    cetesCount
  );
}

// ================================================
// 24. PAGINA BOLSA MEXICANA
// ================================================

void paginaBolsa() {

  oled.setFont(u8g2_font_6x12_tf);

  oled.drawStr(0, 10, "BOLSA MEXICANA");

  oled.drawHLine(0, 13, 128);

  oled.setFont(u8g2_font_7x14B_tf);

  oled.setCursor(0, 31);

  if (isfinite(ipc)) {

    oled.print(ipc, 0);

  } else {

    oled.print("Sin datos");
  }

  oled.setFont(u8g2_font_6x12_tf);

  oled.setCursor(0, 46);

  oled.print("IPC: ");

  if (isfinite(ipcChange)) {

    if (ipcChange >= 0) oled.print("+");

    oled.print(ipcChange, 2);

    oled.print("%");

  } else {

    oled.print("--");
  }

  oled.setCursor(0, 60);

  oled.print("Vol: ");

  if (ipcVolume > 0) {

    oled.print(ipcVolume);

  } else {

    oled.print("N/D");
  }
}

// ================================================
// 25. PAGINA DHT11
// ================================================

void paginaDHT() {

  oled.setFont(u8g2_font_6x12_tf);

  oled.drawStr(0, 10, "SENSOR DHT11");

  oled.drawHLine(0, 13, 128);

  oled.setFont(u8g2_font_7x14B_tf);

  oled.setCursor(0, 33);

  oled.print("Temp: ");

  if (isfinite(dhtTemp)) {

    oled.print(dhtTemp, 1);

    oled.print(" C");

  } else {

    oled.print("--");
  }

  oled.setCursor(0, 55);

  oled.print("Hum: ");

  if (isfinite(dhtHum)) {

    oled.print(dhtHum, 1);

    oled.print("%");

  } else {

    oled.print("--");
  }
}

// ================================================
// 26. PAGINA BMP280
// ================================================

void paginaBMP() {

  oled.setFont(u8g2_font_6x12_tf);

  oled.drawStr(0, 10, "SENSOR BMP280");

  oled.drawHLine(0, 13, 128);

  oled.setFont(u8g2_font_7x14B_tf);

  oled.setCursor(0, 31);

  oled.print("T: ");

  if (isfinite(bmpTemp)) {

    oled.print(bmpTemp, 1);

    oled.print(" C");

  } else {

    oled.print("--");
  }

  oled.setCursor(0, 53);

  oled.print("P: ");

  if (isfinite(bmpPressure)) {

    oled.print(bmpPressure, 1);

    oled.print("hPa");

  } else {

    oled.print("--");
  }
}

// ================================================
// 27. PAGINA ESTADO DEL SISTEMA
// ================================================

void paginaEstado() {

  oled.setFont(u8g2_font_6x12_tf);

  oled.drawStr(0, 10, "ESTADO DEL SISTEMA");

  oled.drawHLine(0, 13, 128);

  oled.setCursor(0, 28);

  oled.print("WiFi: ");

  oled.print(wifiOK ? "CONECTADO" : "SIN WIFI");

  oled.setCursor(0, 41);

  oled.print("BMP280: ");

  oled.print(bmpOK ? "OK" : "ERROR");

  oled.setCursor(0, 52);

  oled.print("Clima: ");
  oled.print(isfinite(weatherTemp) ? "OK" : "SIN DATOS");

  oled.setCursor(0, 63);

  oled.print("DHT11: ");

  oled.print(
    isfinite(dhtTemp) ? "OK" : "ERROR"
  );

}

// ================================================
// 28. MOSTRAR PAGINAS
// ================================================

void mostrarPagina() {

  oled.clearBuffer();

  switch (currentPage) {

    case 0:
      paginaClima();
      break;

    case 1:
      paginaDolar();
      break;

    case 2:
      paginaYen();
      break;

    case 3:
      paginaCetes();
      break;

    case 4:
      paginaBolsa();
      break;

    case 5:
      paginaDHT();
      break;

    case 6:
      paginaBMP();
      break;

    case 7:
      paginaEstado();
      break;
  }

  oled.sendBuffer();
}

// ================================================
// 29. CONFIGURAR HARDWARE
// ================================================

void setupHardware() {

  Wire.begin(
    I2C_SDA,
    I2C_SCL
  );

  oled.begin();

  dht.begin();

  bmpOK = bmp.begin(0x76);

  if (!bmpOK) {

    bmpOK = bmp.begin(0x77);
  }

  if (bmpOK) {

    Serial.println("BMP280 detectado");

  } else {

    Serial.println("BMP280 no detectado");
  }
}

// ================================================
// 30. SETUP
// ================================================

void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println();

  Serial.println("============================");

  Serial.println("ESTACION METEO FINANCIERA");

  Serial.println("ESP32-S3 SUPER MINI");

  Serial.println("============================");

  setupHardware();

  oled.clearBuffer();

  oled.setFont(u8g2_font_7x14B_tf);

  oled.drawStr(5, 25, "ESTACION");

  oled.drawStr(5, 44, "METEO-FINANCIERA");

  oled.sendBuffer();

  conectarWiFi();

  if (wifiOK) {

    struct tm t;

    getLocalTime(&t, 10000);

    obtenerClima();

    actualizarFinanzas();
  }

  leerSensores();

  lastSensors = millis();

  // Si el primer intento de clima falla, volver a intentarlo en 60 segundos.
  lastWeather = isfinite(weatherTemp) ? millis() : (millis() - WEATHER_TIME + 60000);

  lastFinance = millis();

  lastPage = millis();

  mostrarPagina();
}

// ================================================
// 31. LOOP PRINCIPAL
// ================================================

void loop() {

  unsigned long ahora = millis();

  // --------------------------------
  // WIFI Y RECONEXION
  // --------------------------------

  if (WiFi.status() == WL_CONNECTED) {

    wifiOK = true;

  } else {

    wifiOK = false;

    if (ahora - lastWifiAttempt >= WIFI_RETRY_TIME) {

      lastWifiAttempt = ahora;

      conectarWiFi();

      if (wifiOK) {

        lastWeather = 0;

        lastFinance = 0;
      }
    }
  }

  // --------------------------------
  // SENSORES LOCALES
  // --------------------------------

  if (ahora - lastSensors >= SENSOR_TIME) {

    lastSensors = ahora;

    leerSensores();
  }

  // --------------------------------
  // CLIMA INTERNET
  // --------------------------------

  if (wifiOK &&
      (lastWeather == 0 ||
       ahora - lastWeather >= WEATHER_TIME)) {

    obtenerClima();
    lastWeather = isfinite(weatherTemp) ? millis() : (millis() - WEATHER_TIME + 60000);
  }

  // --------------------------------
  // INFORMACION FINANCIERA
  // --------------------------------

  if (wifiOK &&
      (lastFinance == 0 ||
       ahora - lastFinance >= FINANCE_TIME)) {

    lastFinance = ahora;

    actualizarFinanzas();
  }

  // --------------------------------
  // ROTACION AUTOMATICA
  // --------------------------------

  if (ahora - lastPage >= PAGE_TIME) {

    lastPage = ahora;

    currentPage++;

    if (currentPage >= TOTAL_PAGES) {

      currentPage = 0;
    }

    mostrarPagina();
  }

  delay(10);
}