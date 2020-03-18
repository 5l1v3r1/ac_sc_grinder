#ifndef __APP_HAL__
#define __APP_HAL__

#include <stdint.h>

namespace hal {

void setup();
void triac_ignition_on();
void triac_ignition_off();

} // namespace

#endif