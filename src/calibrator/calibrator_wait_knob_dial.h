#ifndef __CALIBRATOR_WAIT_KNOB_DIAL_H__
#define __CALIBRATOR_WAIT_KNOB_DIAL_H__

// Detects when user quickly dials knob 3 times. This sequence is used
// to start calibration sequence.
//
// .tick() should be called with APP_TICK_FREQUENCY/sec, as everything else.
// It returns `true` when dials detected, and `false` in other cases.


#include "../math/fix16_math.h"

#include "../app.h"
#include "../sensors.h"


extern Sensors sensors;

#define KNOB_TRESHOLD F16(0.05)

#define IS_KNOB_LOW(val)  (val < KNOB_TRESHOLD)
#define IS_KNOB_HIGH(val) (val >= KNOB_TRESHOLD)

constexpr int knob_wait_min = (int)(APP_TICK_FREQUENCY * 0.2f);
constexpr int knob_wait_max = (int)(APP_TICK_FREQUENCY * 1.0f);


class CalibratorWaitKnobDial
{
public:

    bool tick() {
        YIELDABLE;

        do {
            // First, check knob is at sstart position (zero),
            // prior to start detect dial sequence
            ticks_cnt = 0;
            dials_cnt = 0;

            while (IS_KNOB_LOW(sensors.knob)) {
                ticks_cnt++;
                YIELD(false);
            }

            if (ticks_cnt < knob_wait_min) continue;

            // If knob is zero long enougth => can start detect dials

            do {
                // Measure UP interval
                ticks_cnt = 0;

                while (IS_KNOB_HIGH(sensors.knob)) {
                    ticks_cnt++;
                    YIELD(false);
                }

                // Terminate on invalid length
                if (ticks_cnt < knob_wait_min || ticks_cnt > knob_wait_max) break;

                dials_cnt++;

                // Terminate on success
                if (dials_cnt >= 3) break;

                // Measure DOWN interval
                ticks_cnt = 0;

                while (IS_KNOB_LOW(sensors.knob)) {
                    ticks_cnt++;
                    YIELD(false);
                }

                // Terminate on invalid length
                if (ticks_cnt < knob_wait_min || ticks_cnt > knob_wait_max) break;
            } while (1);
        } while (dials_cnt < 3);

        return true;
    }

private:

    int ticks_cnt = 0;
    int dials_cnt = 0;

};


#endif
