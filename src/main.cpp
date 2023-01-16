#include <arduino.h>
#include <driver/adc.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include "EmonLib.h"
#include <WiFiMulti.h>

#include "wifi_credentials.h"
#if !(defined(WIFI_SSID) && defined(WIFI_PASSWORD))
#error "WiFi credentials not found! Please add a wifi_credentials.h file with your WiFi SSID and password."
#endif

WiFiMulti wifiMulti;

// Measurement config
#define MEASURE_DELAY_MS 500
#define MEASURE_BATCH_DELAY_MS 30000
#define EMON_CALIBRATION_FACTOR 30
#define EMON_SAMPLES 1480

// InfluxDB config
#include "influxdb_credentials.h"

// Set timezone string according to https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
// Central European Time (e.g. Paris) is "CET-1CEST,M3.5.0,M10.5.0/3"
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"

// NTP servers the for time synchronization.
// For the fastest time sync find NTP servers in your area: https://www.pool.ntp.org/zone/
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.nis.gov"
#define WRITE_PRECISION WritePrecision::MS
#define MAX_BATCH_SIZE (MEASURE_BATCH_DELAY_MS / MEASURE_DELAY_MS)
#define WRITE_BUFFER_SIZE (MAX_BATCH_SIZE * 3)

// InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
// InfluxDB client instance without preconfigured InfluxCloud certificate for insecure connection
// InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);

// Data point
Point sensorCurrent("powermeter_sensor");

// Number for loops to sync time using NTP
int iterations = 0;

// Energy monitor to measure current
EnergyMonitor emon1;
// Force EmonLib to use 10bit ADC resolution
#define ADC_BITS 10
#define ADC_COUNTS (1 << ADC_BITS)

// ESP32 pin for analog measurement
#define ADC_INPUT 36

unsigned long currentMillis;
unsigned long previousMillis = 0;

void setup()
{
    Serial.begin(115200);

    // Setup wifi
    WiFi.mode(WIFI_STA);
    wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Connecting to wifi");
    while (wifiMulti.run() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(500);
    }
    Serial.println();

    // Alternatively, set insecure connection to skip server certificate validation
    // client.setInsecure();

    // Accurate time is necessary for certificate validation and writing in batches
    // Syncing progress and the time will be printed to Serial.
    timeSync(TZ_INFO, NTP_SERVER1, NTP_SERVER2);

    // Check server connection
    if (client.validateConnection())
    {
        Serial.print("Connected to InfluxDB: ");
        Serial.println(client.getServerUrl());
    }
    else
    {
        Serial.print("InfluxDB connection failed: ");
        Serial.println(client.getLastErrorMessage());
    }

    // Enable messages batching and retry buffer
    client.setWriteOptions(WriteOptions().writePrecision(WRITE_PRECISION).batchSize(MAX_BATCH_SIZE).bufferSize(WRITE_BUFFER_SIZE));

    // Set tags
    sensorCurrent.addTag("version", "0.1");
    sensorCurrent.addTag("device", "ESP32");
    sensorCurrent.addTag("sensor_model", "SCT013-030");
    sensorCurrent.addTag("instrumented_device", "sump-pump");
    sensorCurrent.addTag("sensor_lib", "openenergymonitor/EmonLib@^1.1.0");

    // Setup the analog input
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
    analogReadResolution(ADC_BITS);
    pinMode(ADC_INPUT, INPUT);

    // Setup the energy monitor
    emon1.current(ADC_INPUT, EMON_CALIBRATION_FACTOR);
}

void loop()
{
    currentMillis = millis();
    if (currentMillis - previousMillis < MEASURE_DELAY_MS)
    {
        return;
    }
    previousMillis = currentMillis;

    // Sync time for batching once per hour
    if (iterations++ >= 3600 * 1000 / MEASURE_DELAY_MS)
    {
        timeSync(TZ_INFO, NTP_SERVER1, NTP_SERVER2);
        iterations = 0;
    }

    // Measure current
    double current = emon1.calcIrms(EMON_SAMPLES);

    // Set time for the measurement
    sensorCurrent.setTime(WritePrecision::MS);

    sensorCurrent.addField("current", current);

    // Print what are we exactly writing
    Serial.print("Writing: ");
    Serial.println(client.pointToLineProtocol(sensorCurrent));

    client.writePoint(sensorCurrent);

    // Clear fields for next usage. Tags remain the same.
    sensorCurrent.clearFields();

    // If no Wifi signal, try to reconnect it
    if (wifiMulti.run() != WL_CONNECTED)
    {
        Serial.println("Wifi connection lost");
    }
}
