#ifndef RELAYMEGA_H
#define RELAYMEGA_H

#include <Arduino.h>

void roleSetup() {
  Serial.begin(115200);
  Serial.println(F("Mega 1 relay ready"));
}

void roleLoop() {
}

#endif
