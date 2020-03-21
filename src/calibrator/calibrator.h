#ifndef __CALIBRATOR_H__
#define __CALIBRATOR_H__

// Detect when user dials knob 3 times, start calibration sequence and
// update configuration.

#include "math/fix16_math.h"
#include "yield.h"

#include "../app.h"
#include "../triac_driver.h"
#include "calibrator_wait_knob_dial.h"
#include "calibrator_static.h"
#include "calibrator_speed.h"
#include "calibrator_pid.h"

extern TriacDriver triacDriver;


class Calibrator
{
public:

    // Returns:
    //
    // - false: we should continue in normal mode
    // - true:  calibration started, we should stop other actions in main
    //          loop until finished.
    //
    bool tick() {
        YIELDABLE;

        YIELD_UNTIL(wait_knob_dial.tick(), false);
        YIELD_UNTIL(calibrate_static.tick(), true);
        YIELD_UNTIL(calibrate_speed.tick(), true);
        YIELD_UNTIL(calibrate_pid.tick(), true);

        return false;
    }

private:
    // Nested FSM-s
    CalibratorWaitKnobDial wait_knob_dial;
    CalibratorStatic calibrate_static;
    CalibratorSpeed calibrate_speed;
    CalibratorPID calibrate_pid;
};

#endif
