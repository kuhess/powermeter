#include <arduino.h>
#include "EmonLib.h"
#include <driver/adc.h>

EnergyMonitor emon1;
#define ADC_INPUT 36
#define HOME_VOLTAGE 239.0

// Force EmonLib to use 10bit ADC resolution
#define ADC_BITS 10
#define ADC_COUNTS (1 << ADC_BITS)

unsigned long currentMillis;
unsigned long previousMillis = 0;

void setup()
{
    Serial.begin(115200);

    // Setup the ADC
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
    analogReadResolution(ADC_BITS);
    pinMode(ADC_INPUT, INPUT);

    // Setup the Energy Monitor
    emon1.current(ADC_INPUT, 30); // calibration = 30 in theory)
}

void loop()
{
    currentMillis = millis();
    if (currentMillis - previousMillis > 200)
    {
        previousMillis = currentMillis;

        double amps = emon1.calcIrms(1480);
        double watt = amps * HOME_VOLTAGE;

        Serial.print("mA:");
        Serial.println(amps * 1000);
    }
}
