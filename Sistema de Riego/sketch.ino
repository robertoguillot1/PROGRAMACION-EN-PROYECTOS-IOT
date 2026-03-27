#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

// Configuración WiFi
const char *ssid = "Wokwi-GUEST";
const char *password = "";

// Pines y sensores
#define PIN_BOMBA_AGUA 13 // Usaremos este pin de forma virtual (sin motor físico)
#define PIN_HUMEDAD    35
#define PIN_DHT         4
#define TIPO_DHT       DHT22

// Umbrales de humedad del suelo
#define HUMEDAD_SECA   30
#define HUMEDAD_HUMEDA 70

// Intervalos de tiempo
#define INTERVALO_LECTURA   2000
#define INTERVALO_RIEGO     10000
#define INTERVALO_VERIFICACION 3000 // Reducido a 3 segundos para la simulación

// Pantalla LCD
LiquidCrystal_I2C lcd(0x27, 16, 4);
DHT dht(PIN_DHT, TIPO_DHT);

// Variables de control
unsigned long ultimaLectura = 0;
unsigned long ultimoRiego = 0;
bool riegoActivo = false;
String ipAddress = "No Conectado";

// Servidor web
WebServer server(80);

void actualizarLCD(float temperatura = -1, float humedad = -1, float humedadSuelo = -1) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Temp: " + String(temperatura, 1) + "C");
    lcd.setCursor(0, 1);
    lcd.print("Humedad: " + String(humedad, 1) + "%");
    lcd.setCursor(0, 2);
    lcd.print("Suelo: " + String(humedadSuelo, 1) + "%");
    lcd.setCursor(0, 3);
    lcd.print("Bomba: " + String(riegoActivo ? "ON " : "OFF"));
}

void actualizarEstadoBombaLCD() {
    lcd.setCursor(0, 3);
    lcd.print("Bomba: " + String(riegoActivo ? "ON   " : "OFF  "));
}

void setup() {
    Serial.begin(115200);

    // Inicializar LCD
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Conectando...");

    // Inicializar DHT
    dht.begin();

    pinMode(PIN_BOMBA_AGUA, OUTPUT);
    digitalWrite(PIN_BOMBA_AGUA, HIGH);  // Bomba desactivada por defecto

    conectarWiFi();
    configurarServidorWeb();
    actualizarLCD();
}

void loop() {
    manejarWiFi();
    server.handleClient();

    unsigned long tiempoActual = millis();

    if (tiempoActual - ultimaLectura >= INTERVALO_LECTURA) {
        ultimaLectura = tiempoActual;

        float temperatura = leerTemperatura();
        float humedad = leerHumedad();
        float humedadSuelo = leerHumedadSuelo();

        Serial.printf("Temp: %.1f°C, Humedad: %.1f%%, Humedad suelo: %.1f%%\n", 
                      temperatura, humedad, humedadSuelo);

        actualizarLCD(temperatura, humedad, humedadSuelo);

        // Siempre verificamos el riego automático ya que quitamos el modo manual físico
        verificarRiegoAutomatico(humedadSuelo, tiempoActual);
    }

    if (riegoActivo && (millis() - ultimoRiego >= INTERVALO_RIEGO)) {
        detenerRiego();
    }
}

void conectarWiFi() {
    WiFi.begin(ssid, password);
    Serial.println("Conectando a WiFi...");
    unsigned long inicio = millis();
    
    while (WiFi.status() != WL_CONNECTED && millis() - inicio < 10000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        ipAddress = WiFi.localIP().toString();
        Serial.println("\nWiFi conectado: " + ipAddress);
    } else {
        Serial.println("\nNo se pudo conectar a WiFi.");
    }
}

void manejarWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi desconectado, reconectando...");
        WiFi.disconnect();
        delay(1000);
        conectarWiFi();
    }
}

void configurarServidorWeb() {
    server.on("/", []() {
        String html = "<html><head><title>Simulacion IoT</title>";
        html += "<meta http-equiv='refresh' content='5'></head><body>";
        html += "<h1>Monitoreo Virtual</h1>";
        html += "<p>Estado Bomba: " + String(riegoActivo ? "ENCENDIDA" : "APAGADA") + "</p>";
        html += "<p>Temperatura: " + String(leerTemperatura()) + "°C</p>";
        html += "<p>Humedad Ambiente: " + String(leerHumedad()) + "%</p>";
        html += "<p>Humedad del Suelo: " + String(leerHumedadSuelo()) + "%</p>";
        html += "<button onclick=\"location.href='/toggle'\">Forzar Bomba Web</button>";
        html += "</body></html>";
        server.send(200, "text/html", html);
    });

    server.on("/toggle", []() {
        if (riegoActivo) {
            detenerRiego();
        } else {
            iniciarRiego();
        }
        server.sendHeader("Location", "/", true);
        server.send(303);
    });

    server.begin();
}

float leerTemperatura() {
    float t = dht.readTemperature();
    return isnan(t) ? -1 : t;
}

float leerHumedad() {
    float h = dht.readHumidity();
    return isnan(h) ? -1 : h;
}

float leerHumedadSuelo() {
    int suma = 0;
    for (int i = 0; i < 5; i++) {
        suma += analogReadMilliVolts(PIN_HUMEDAD);
        delay(10);
    }
    int valorBruto = suma / 5;
    float humedadSuelo = map(valorBruto, 3300, 0, 0, 100);
    return constrain(humedadSuelo, 0, 100);
}

void iniciarRiego() {
    if (!riegoActivo) {
        digitalWrite(PIN_BOMBA_AGUA, LOW); // Seguimos enviando la señal al Pin 13
        Serial.println("Bomba virtual ENCENDIDA");
        ultimoRiego = millis();
        riegoActivo = true;
        actualizarEstadoBombaLCD(); 
    }
}

void detenerRiego() {
    if (riegoActivo) {
        digitalWrite(PIN_BOMBA_AGUA, HIGH); // Seguimos cortando la señal al Pin 13
        Serial.println("Bomba virtual APAGADA");
        riegoActivo = false;
        actualizarEstadoBombaLCD(); 
    }
}

void verificarRiegoAutomatico(float humedadSuelo, unsigned long tiempoActual) {
    static unsigned long ultimaVerificacion = 0;

    if (tiempoActual - ultimaVerificacion < INTERVALO_VERIFICACION) return;
    ultimaVerificacion = tiempoActual;

    if (humedadSuelo <= HUMEDAD_SECA) {
        iniciarRiego();
    } else if (humedadSuelo >= HUMEDAD_HUMEDA && riegoActivo) {
        detenerRiego();
    }
}