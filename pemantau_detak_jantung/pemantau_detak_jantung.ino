#include <Arduino.h>
#include <Wire.h>
#include "U8g2lib.h"
#include "MAX30105.h"
#include "spo2_algorithm.h"

MAX30105 sensor;
U8G2_SH1106_128X64_NONAME_F_HW_I2C OLED(U8G2_R0, U8X8_PIN_NONE);
#define buzzer 13

uint32_t irBuffer[100];
uint32_t redBuffer[100];

int32_t spo2;         
int8_t validSPO2;      
int32_t heartRate;      
int8_t validHeartRate;

void triggerBeep() {
    tone(buzzer, 1000, 30);
}

void setup() {
    Serial.begin(115200);

    OLED.begin();
    OLED.clearBuffer();
    OLED.setFont(u8g2_font_ncenB10_tr);
    OLED.setCursor(10, 35);
    OLED.print("Initializing...");
    OLED.sendBuffer();

    if (!sensor.begin(Wire, I2C_SPEED_STANDARD)) {
        OLED.clearBuffer();
        OLED.setCursor(10, 35);
        OLED.print("Sensor Error!");
        OLED.sendBuffer();
        while (1); 
    }

    byte ledBrightness = 60; 
    byte sampleAverage = 4;  
    byte ledMode = 2;       
    byte sampleRate = 100;   
    int pulseWidth = 411;    
    int adcRange = 4096;     

    sensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
}

void loop() {
    for (byte i = 0; i < 100; i++) {
        while (!sensor.available()) {
            sensor.check();
        }
        redBuffer[i] = sensor.getRed();
        irBuffer[i] = sensor.getIR();
        sensor.nextSample();
    }

    maxim_heart_rate_and_oxygen_saturation(
        irBuffer, 100, redBuffer, 
        &spo2, &validSPO2, 
        &heartRate, &validHeartRate
    );

    OLED.clearBuffer();

    if (irBuffer[99] < 50000) {
        OLED.setFont(u8g2_font_ncenB10_tr);
        OLED.setCursor(15, 38);
        OLED.print("Letakkan Jari");
    }
    else if (validSPO2 == 1 && validHeartRate == 1) {
        triggerBeep();
        OLED.setFont(u8g2_font_ncenB10_tr);
        
        OLED.setCursor(5, 25);
        OLED.print("BPM: ");
        OLED.setCursor(65, 25);
        OLED.print(heartRate);

        OLED.setCursor(5, 55);
        OLED.print("SpO2: ");
        OLED.setCursor(65, 55);
        OLED.print(spo2);
        OLED.print("%");
    }
    else {
        OLED.setFont(u8g2_font_ncenB10_tr);
        OLED.setCursor(10, 38);
        OLED.print("Measuring...");
    }

    OLED.sendBuffer(); 
}