#ifndef UNOHOST_H
#define UNOHOST_H

#include <Arduino.h>

void roleSetup() {
  Serial.begin(115200);
  Serial.println(F("Uno host ready"));
}

void roleLoop() {
}

#endif
