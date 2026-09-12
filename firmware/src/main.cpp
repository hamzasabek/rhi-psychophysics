#include <Arduino.h>
#include "HostLink.h"

HostLink host;

void setup() {
  host.begin();
}

void loop() {
  host.poll();
}
