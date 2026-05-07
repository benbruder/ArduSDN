#ifndef NANOSWITCH_H
#define NANOSWITCH_H

#include <Arduino.h>

void roleSetup() {
  Serial.begin(115200);
  Serial.println(F("Nano switch ready"));
}

void roleLoop() {
}

#endif
