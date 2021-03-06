/*
  LightningDetector.ino - AS3935 Franklin Lightning Sensor™ IC by AMS library demo code
  Copyright (c) 2012 Raivis Rengelis (raivis [at] rrkb.lv). All rights reserved.
  Modified in 2014 to use standard Wire library by Edward Gutting (gutting [at] gmail.com).

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 3 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <Wire.h>
#include <AS3935_Wire.h>
#include <LiquidCrystal.h>

void printAS3935Registers();


// Iterrupt handler for AS3935 irqs
// and flag variable that indicates interrupt has been triggered
// Variables that get changed in interrupt routines need to be declared volatile
// otherwise compiler can optimize them away, assuming they never get changed
void AS3935Irq();
volatile int AS3935IrqTriggered;

// Library object initialization First argument is interrupt pin, second is device I2C address
AS3935 AS3935(2, 0x03);

#define NOISE_COUNTER_MAX 5
#define NOISE_COUNTER_AGE 15000
#define NOISE_FLOOR_MAX 7

uint8_t noise_counter = 0;
uint32_t noise_timer = millis();
uint8_t noise_floor = 0;

LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

void setup()
{
  while (!Serial);
  Serial.begin(9600);
  Serial.println("START");

  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("LightningSensor");
  //I2C library initialization
  Wire.begin();

  // optional control of power for AS3935 via a PNP transistor
  // very useful for lockup prevention and power saving
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);

  // reset all internal register values to defaults
  AS3935.reset();

  lcd.setCursor(0, 1);
  lcd.print("Calibrating...");
  recalibrateAS3935();

  // since this is demo code, we just go on minding our own business and ignore the fact that someone divided by zero

  // first let's turn on disturber indication and print some register values from AS3935
  // tell AS3935 we are indoors, for outdoors use setOutdoors() function
  AS3935.setIndoors();
  // turn on indication of distrubers, once you have AS3935 all tuned, you can turn those off with disableDisturbers()
  AS3935.enableDisturbers();
  printAS3935Registers();
  AS3935IrqTriggered = 0;
  // Using interrupts means you do not have to check for pin being set continiously, chip does that for you and
  // notifies your code
  // demo is written and tested on ChipKit MAX32, irq pin is connected to max32 pin 2, that corresponds to interrupt 1
  // look up what pins can be used as interrupts on your specific board and how pins map to int numbers

  // ChipKit Max32 - irq connected to pin 2
  // attachInterrupt(1,AS3935Irq,RISING);
  // uncomment line below and comment out line above for Arduino Mega 2560, irq still connected to pin 2, same for atmega328p
  attachInterrupt(1, AS3935Irq, RISING);
  lcd.clear();
}

void loop()
{
  uint32_t blitz = 0;
  uint8_t blitz_min = 0;
  int strokeDistance;

  // here we go into loop checking if interrupt has been triggered, which kind of defeats
  // the whole purpose of interrupts, but in real life you could put your chip to sleep
  // and lower power consumption or do other nifty things
  if (AS3935IrqTriggered)
  {
    // reset the flag
    AS3935IrqTriggered = 0;
    // first step is to find out what caused interrupt
    // as soon as we read interrupt cause register, irq pin goes low
    int irqSource = AS3935.interruptSource();
    // returned value is bitmap field, bit 0 - noise level too high, bit 2 - disturber detected, and finally bit 3 - lightning!
    if (irqSource & 0b0001)
    {
      if (noise_counter > 0 && (millis() - noise_timer) > NOISE_COUNTER_AGE)
      {
        noise_counter = 0;
      }

      lcd.setCursor(0, 1);
      lcd.print("Noise high     ");

      noise_counter++;
      noise_timer = millis();
      if (noise_counter >= NOISE_COUNTER_MAX)
        recalibrateAS3935();
    }
    else if (irqSource & 0b0100)
    {
      Serial.println("Disturber detected");
      lcd.setCursor(0, 1);
      lcd.print("Disturber      ");
    }
    else if (irqSource & 0b1000)
    {
      // need to find how far that lightning stroke, function returns approximate distance in kilometers,
      // where value 1 represents storm in detector's near victinity, and 63 - very distant, out of range stroke
      // everything in between is just distance in kilometers
      strokeDistance = AS3935.lightningDistanceKm();
      if (strokeDistance == 1)
        Serial.println("Storm overhead, watch out!");
      if (strokeDistance == 63)
        Serial.println("Out of range lightning detected.");
      if (strokeDistance < 63 && strokeDistance > 1)
      {
        Serial.print("Lightning detected ");
        Serial.print(strokeDistance, DEC);
        Serial.println(" kilometers away.");

        blitz = millis();
      }
    }

    blitz_min = (blitz - millis() / 60000);
    if (blitz > 0 && blitz_min < 30)
    {
      char buf[11];
      lcd.setCursor(0, 0);
      lcd.print(itoa(strokeDistance, buf, 10));
      lcd.setCursor(5, 0);
      lcd.print(itoa(blitz_min, buf, 10));
    }
    else
    {
      lcd.setCursor(0, 0);
      lcd.print("                ");
      blitz = 0;
    }
    /*
      else
      {
      lcd.setCursor(0, 1);
      lcd.print("              ");
      }*/
  }
}

bool recalibrateAS3935(void)
{

  lcd.setCursor(0, 1);
  lcd.print("             ");

  Serial.print(F("Recalibrating with noise_floor="));
  Serial.print(noise_floor);
  Serial.print(F(": "));
  AS3935.setNoiseFloor(noise_floor);
  if (!AS3935.calibrate())
    Serial.println("Tuning out of range, check your wiring, your sensor and make sure physics laws have not changed!");
  else
    Serial.println(F("done."));

  noise_counter = 0;

  if (noise_floor < NOISE_FLOOR_MAX)
    noise_floor++;
}

void printAS3935Registers()
{
  int noiseFloor = AS3935.getNoiseFloor();
  int spikeRejection = AS3935.getSpikeRejection();
  int watchdogThreshold = AS3935.getWatchdogThreshold();
  Serial.print("Noise floor is: ");
  Serial.println(noiseFloor, DEC);
  Serial.print("Spike rejection is: ");
  Serial.println(spikeRejection, DEC);
  Serial.print("Watchdog threshold is: ");
  Serial.println(watchdogThreshold, DEC);
}

// this is irq handler for AS3935 interrupts, has to return void and take no arguments
// always make code in interrupt handlers fast and short
void AS3935Irq()
{
  AS3935IrqTriggered = 1;
}
