#ifndef __APP_H__
#define __APP_H__

#include <stdint.h>

// Oversampling ratio. Used to define buffer sizes
#define ADC_FETCH_PER_TICK 8

// How many channels are sampled "in parallel".
// Used to define global DMA buffer size.
#define ADC_CHANNELS_COUNT 4

// Frequency of measurements & state updates.
// Currently driven by ADC for simplicity.
#define APP_TICK_FREQUENCY 17857

// "EEPROM" location and size for RPM non-linearity conmensation table.
// Starts after other config variables.
#define CFG_RPM_INTERP_TABLE_START_ADDR 10
#define CFG_RPM_INTERP_TABLE_LENGTH 16

#define CFG_R_INTERP_TABLE_START_ADDR 26
#define CFG_R_INTERP_TABLE_LENGTH 7

float eeprom_float_read(uint16_t addr, float dflt);
void eeprom_float_write(uint16_t addr, float val);

#endif
