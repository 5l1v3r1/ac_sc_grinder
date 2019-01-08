#ifndef __CALIBRATOR_PID__
#define __CALIBRATOR_PID__

#include "../math/fix16_math.h"
#include "../app.h"
#include "../sensors.h"
#include "../triac_driver.h"
#include "../speed_controller.h"

#include <math.h>
#include <limits.h>

extern Sensors sensors;
extern TriacDriver triacDriver;
extern SpeedController speedController;

#define MIN_P 1.0
#define INIT_P_ITERATION_STEP 4.0

// Scale down PID to this value for safety
#define PID_SAFETY_SCALE 0.9

#define PID_P_SETPOINT 0.3
#define PID_I_OVERSHOOT_SETPOINT 0.4
#define PID_I_START_SETPOINT 0.3

// Setpoint values for start and
// stop time measurement.
#define LOW_SPEED_SETPOINT 0.35
#define HIGH_SPEED_SETPOINT 0.7

// Speed data is noisy, so start/stop
// time is measured when speed crosses
// SPEED_MEASURE_THRESHOLD and then recalculated
// to more narrow SPEED_IDEAL_THRESHOLD.
#define SPEED_MEASURE_THRESHOLD 0.15
#define SPEED_IDEAL_THRESHOLD 0.02

// Assume that motor is 1-st order dynamic system.
// It will cross SPEED_IDEAL_THRESHOLD
// at ideal time = measured time * start_stop_adjust
static const fix16_t start_stop_adjust = fix16_from_float(
  log(SPEED_IDEAL_THRESHOLD) /
  log(SPEED_MEASURE_THRESHOLD)
);

// Frequency scale for data save when measure start/stop time.
// Helps to save used memory
#define SPEED_DATA_SAVE_RATIO 10

// Max 10 seconds to store data on start/stop time measure
#define SPEED_DATA_SAVE_TIME_MAX 10

// Lowpass coefficient value for 5 Hz
// cutoff frequency
#define LOWPASS_FILTER_ALPHA 0.3859

class CalibratorPID {
public:

  bool tick() {
    switch (state)
    {
      // Before start time measure
      // motor must run at steady low speed
      case INIT_WAIT_STABLE_LOW_SPEED:
        setpoint = F16(LOW_SPEED_SETPOINT);

        triacDriver.setpoint = setpoint;

        // Init local iteration vars.
        wait_stable_speed_init();
        set_state(WAIT_STABLE_LOW_SPEED);

      case WAIT_STABLE_LOW_SPEED:
        triacDriver.tick();
        if (!sensors.zero_cross_up) break;

        wait_stable_speed(sensors.speed, INIT_MEASURE_START_TIME, INT_MAX);
        break;

      case INIT_MEASURE_START_TIME:
        setpoint = F16(HIGH_SPEED_SETPOINT);

        triacDriver.setpoint = setpoint;

        // Init local iteration vars.
        wait_stable_speed_init();
        speed_data_idx = 0;
        start_time_ticks = 0;
        start_stop_scaler_cnt = 0;
        set_state(WAIT_STABLE_SPEED_UP);

      case WAIT_STABLE_SPEED_UP:
        triacDriver.tick();
        start_time_ticks++;

        if (!sensors.zero_cross_up) break;

        if (speed_data_idx < speed_data_length)
        {
          // Apply lowpass filtration
          filtered_speed = fix16_mul(
            F16(LOWPASS_FILTER_ALPHA),
            sensors.speed
          ) + fix16_mul(
            F16(1.0 - LOWPASS_FILTER_ALPHA),
            (speed_data_idx == 0) ? sensors.speed : filtered_speed
          );

          if (start_stop_scaler_cnt == 0) {
            start_speed_data[speed_data_idx++] = filtered_speed;
          }

          start_stop_scaler_cnt++;
          if (start_stop_scaler_cnt == SPEED_DATA_SAVE_RATIO) start_stop_scaler_cnt = 0;
        }

        wait_stable_speed(sensors.speed, INIT_MEASURE_STOP_TIME, INT_MAX);

        break;

      case INIT_MEASURE_STOP_TIME:
        start_speed_data_len = speed_data_idx;

        setpoint = F16(LOW_SPEED_SETPOINT);
        triacDriver.setpoint = setpoint;

        // Init local iteration vars.
        wait_stable_speed_init();
        speed_data_idx = 0;
        stop_time_ticks = 0;
        start_stop_scaler_cnt = 0;
        set_state(WAIT_STABLE_SPEED_DOWN);

      case WAIT_STABLE_SPEED_DOWN:
        triacDriver.tick();
        stop_time_ticks++;

        if (!sensors.zero_cross_up) break;

        if (speed_data_idx < speed_data_length)
        {
          // Apply lowpass filtration
          filtered_speed = fix16_mul(
            F16(LOWPASS_FILTER_ALPHA),
            sensors.speed
          ) + fix16_mul(
            F16(1.0 - LOWPASS_FILTER_ALPHA),
            (speed_data_idx == 0) ? sensors.speed : filtered_speed
          );

          if (start_stop_scaler_cnt == 0) {
            stop_speed_data[speed_data_idx++] = filtered_speed;
          }

          start_stop_scaler_cnt++;
          if (start_stop_scaler_cnt == SPEED_DATA_SAVE_RATIO) start_stop_scaler_cnt = 0;

        }

        wait_stable_speed(sensors.speed, INIT_P_CALIBRATION, INT_MAX);

        break;

      case INIT_P_CALIBRATION:
        stop_speed_data_len = speed_data_idx;
        motor_start_stop_time = calculate_start_stop_time(
          start_speed_data_len,
          start_speed_data,
          stop_speed_data_len,
          stop_speed_data
        );

        iterations_count = 0;
        iteration_step = F16(INIT_P_ITERATION_STEP);

        pid_param_attempt_value = F16(MIN_P);

        speedController.cfg_pid_i_inv = fix16_div(
          F16(1.0 / APP_PID_FREQUENCY),
          motor_start_stop_time
        );

        set_state(WAIT_STABLE_SPEED_P_CALIBRATION_START);

      case WAIT_STABLE_SPEED_P_CALIBRATION_START:
        wait_stable_speed_init();
        set_state(WAIT_STABLE_SPEED_P_CALIBRATION_CYCLE);

      case WAIT_STABLE_SPEED_P_CALIBRATION_CYCLE:
        // Wait for stable speed with minimal
        // PID_P and maximal PID_I
        speedController.cfg_pid_p = F16(MIN_P);
        speedController.cfg_pid_i_inv = fix16_div(
          F16(1.0 / APP_PID_FREQUENCY),
          motor_start_stop_time
        );

        speedController.in_knob = F16(PID_P_SETPOINT);

        speedController.in_speed = sensors.speed;
        speedController.tick();
        triacDriver.setpoint = speedController.out_power;
        triacDriver.tick();

        if (!sensors.zero_cross_up) break;

        wait_stable_speed(sensors.speed, MEASURE_VARIANCE_START, 13);

        break;

      case MEASURE_VARIANCE_START:
        speedController.cfg_pid_p = pid_param_attempt_value;
        speed_buffer_idx = 0;
        set_state(MEASURE_VARIANCE_CYCLE);

      case MEASURE_VARIANCE_CYCLE:
        speedController.in_knob = F16(PID_P_SETPOINT);
        speedController.in_speed = sensors.speed;
        speedController.tick();
        triacDriver.setpoint = speedController.out_power;
        triacDriver.tick();

        if (!sensors.zero_cross_up) break;

        speed_buffer[speed_buffer_idx++] = sensors.speed;

        if (speed_buffer_idx >= speed_buffer_length)
        {
          fix16_t variance = calculate_variance(speed_buffer_idx, speed_buffer);

          // Save variance of first iteration as reference
          // to compare values of next iterations to this value
          if (iterations_count == 0) first_iteration_variance = variance;

          // If variance is less than margin value
          // step for next iteration should be positive,
          // otherwise - negative
          if (variance <= first_iteration_variance * 2) iteration_step = abs(iteration_step);
          else iteration_step = -abs(iteration_step);

          pid_param_attempt_value += iteration_step;

          iteration_step /= 2;

          iterations_count++;
          if (iterations_count >= max_iterations)
          {
            pid_p_calibrated_value = pid_param_attempt_value;
            set_state(INIT_I_CALIBRATION);
            break;
          }
          set_state(WAIT_STABLE_SPEED_P_CALIBRATION_START);
        }

        break;

      case INIT_I_CALIBRATION:
        iterations_count = 0;
        // Start calibration with safe
        // PID_I value = motor_start_stop_time
        // Motor will work without overshoot
        // with this value.
        iteration_step = -(motor_start_stop_time / 2);

        pid_param_attempt_value = motor_start_stop_time;
        first_iteration_overshoot_speed = 0;

        // Set overshoot measurement time interval
        // to motor_start_stop_time
        // for reability of measurement.
        measure_overshoot_ticks_max = fix16_to_int(motor_start_stop_time * 50);

        speedController.cfg_pid_p = pid_p_calibrated_value;

        set_state(WAIT_STABLE_SPEED_I_CALIBRATION_START);

      case WAIT_STABLE_SPEED_I_CALIBRATION_START:
        wait_stable_speed_init();
        set_state(WAIT_STABLE_SPEED_I_CALIBRATION_CYCLE);

      case WAIT_STABLE_SPEED_I_CALIBRATION_CYCLE:
        // Wait for stable speed with minimal
        // PID_P and maximal PID_I
        speedController.cfg_pid_p = F16(MIN_P);
        speedController.cfg_pid_i_inv = fix16_div(
          F16(1.0 / APP_PID_FREQUENCY),
          motor_start_stop_time
        );

        speedController.in_knob = F16(PID_I_START_SETPOINT);

        speedController.in_speed = sensors.speed;
        speedController.tick();
        triacDriver.setpoint = speedController.out_power;
        triacDriver.tick();

        if (!sensors.zero_cross_up) break;

        wait_stable_speed(sensors.speed, MEASURE_OVERSHOOT_START, 13);

        break;

      case MEASURE_OVERSHOOT_START:
        speedController.cfg_pid_p = pid_p_calibrated_value;
        speedController.cfg_pid_i_inv = fix16_div(
          F16(1.0 / APP_PID_FREQUENCY),
          pid_param_attempt_value
        );

        measure_overshoot_ticks = 0;
        overshoot_speed = 0;
        set_state(MEASURE_OVERSHOOT_CYCLE);

      case MEASURE_OVERSHOOT_CYCLE:
        speedController.in_knob = F16(PID_I_OVERSHOOT_SETPOINT);
        speedController.in_speed = sensors.speed;
        speedController.tick();
        triacDriver.setpoint = speedController.out_power;
        triacDriver.tick();

        if (!sensors.zero_cross_up) break;

        measure_overshoot_ticks++;

        // Find max speed value
        if (overshoot_speed < sensors.speed) overshoot_speed = sensors.speed;

        if (measure_overshoot_ticks >= measure_overshoot_ticks_max)
        {
          // Save overshoot of first iteration as reference
          // to compare next iterations values to this value
          if (iterations_count == 0) first_iteration_overshoot_speed = overshoot_speed;

          fix16_t overshoot = fix16_div(
            overshoot_speed - first_iteration_overshoot_speed,
            first_iteration_overshoot_speed
          );

          // If overshoot is greater than margin value
          // step for next iteration should be positive,
          // otherwise - negative
          if (overshoot > F16(0.10)) iteration_step = abs(iteration_step);
          else iteration_step = -abs(iteration_step);

          pid_param_attempt_value += iteration_step;

          iteration_step /= 2;

          iterations_count++;
          if (iterations_count >= max_iterations)
          {
            pid_i_calibrated_value = pid_param_attempt_value;
            set_state(STOP);
            break;
          }
          set_state(WAIT_STABLE_SPEED_I_CALIBRATION_START);
        }

        break;

      case STOP:
        // Store in flash
        eeprom_float_write(
          CFG_PID_P_ADDR,
          fix16_to_float(pid_p_calibrated_value) * PID_SAFETY_SCALE
        );
        eeprom_float_write(
          CFG_PID_I_ADDR,
          fix16_to_float(pid_i_calibrated_value) / PID_SAFETY_SCALE
        );
        speedController.configure();
        set_state(INIT_WAIT_STABLE_LOW_SPEED); // Reset state to initial
        return true;
    }
    return false;
  }

private:

  enum State {
    INIT_WAIT_STABLE_LOW_SPEED,
    WAIT_STABLE_LOW_SPEED,
    INIT_MEASURE_START_TIME,
    WAIT_STABLE_SPEED_UP,
    INIT_MEASURE_STOP_TIME,
    WAIT_STABLE_SPEED_DOWN,
    INIT_P_CALIBRATION,
    WAIT_STABLE_SPEED_P_CALIBRATION_START,
    WAIT_STABLE_SPEED_P_CALIBRATION_CYCLE,
    MEASURE_VARIANCE_START,
    MEASURE_VARIANCE_CYCLE,
    INIT_I_CALIBRATION,
    WAIT_STABLE_SPEED_I_CALIBRATION_START,
    WAIT_STABLE_SPEED_I_CALIBRATION_CYCLE,
    MEASURE_OVERSHOOT_START,
    MEASURE_OVERSHOOT_CYCLE,
    STOP
  } state = INIT_WAIT_STABLE_LOW_SPEED;

  // Desireable accuracy of PID calibration is 0.1
  // We need 7 iterations to achieve this accuracy
  // because (10 - 1)/2^7 = 0.07 < 0.1
  enum {
    max_iterations = 7
  };

  int ticks_cnt = 0;

  // Holds speed values for variance calculation
  enum {
    speed_buffer_length = 50,
    speed_data_length = (int)(SPEED_DATA_SAVE_TIME_MAX * 60 / SPEED_DATA_SAVE_RATIO)
  };

  fix16_t speed_buffer[speed_buffer_length];
  int speed_buffer_idx = 0;

  // Buffers for speed data for start/stop
  // time measurement
  fix16_t start_speed_data[speed_data_length];
  fix16_t stop_speed_data[speed_data_length];
  int speed_data_idx = 0;
  int start_time_ticks = 0;
  int stop_time_ticks = 0;
  int start_stop_scaler_cnt;
  fix16_t filtered_speed = 0;

  fix16_t prev_speed = 0;

  int start_speed_data_len;
  int stop_speed_data_len;

  fix16_t pid_param_attempt_value;

  fix16_t pid_p_calibrated_value;
  fix16_t pid_i_calibrated_value;

  fix16_t iteration_step;

  // Holds value calculated during first attempt
  fix16_t first_iteration_variance;

  int iterations_count = 0;

  fix16_t setpoint = 0;

  // History of measured speed. Used to detect stable values.
  fix16_t speed_log[3] = { 0, 0, fix16_minimum };
  int measure_attempts = 0;

  MedianIteratorTemplate<fix16_t, 32> median_filter;

  fix16_t motor_start_stop_time;

  int measure_overshoot_ticks = 0;
  // Measure overshoot during period
  // equal to motor start/stop time.
  // One tick is 0.02 sec if power supply frequency is 50 Hz
  int measure_overshoot_ticks_max;

  fix16_t overshoot_speed = 0;
  fix16_t first_iteration_overshoot_speed = 0;

  void set_state(State st)
  {
    state = st;
    ticks_cnt = 0;
  }

  // Calculate variance of speed values
  // in speed_buffer
  fix16_t calculate_variance(int num_samples, fix16_t buffer[])
  {
    fix16_t speed_sum = 0;
    fix16_t variance_sum = 0;

    for (int i = 0; i < num_samples; i++) speed_sum += buffer[i];

    fix16_t speed_mean = speed_sum / num_samples;

    for (int i = 0; i < num_samples; i++)
    {
      fix16_t difference = buffer[i] - speed_mean;
      variance_sum += fix16_mul(difference, difference);
    }

    return variance_sum / (num_samples - 1);
  }

  // Returns sum of start and stop times
  fix16_t calculate_start_stop_time(
    int start_len,
    fix16_t start_data[],
    int stop_len,
    fix16_t stop_data[]
  )
  {
    fix16_t start_steady_speed = start_data[start_len - 1];
    fix16_t start_initial_speed = start_data[0];
    fix16_t speed_at_measure_threshold = fix16_mul(
      start_steady_speed,
      F16(1.0 - SPEED_MEASURE_THRESHOLD)
    ) + fix16_mul(
      start_initial_speed,
      F16(SPEED_MEASURE_THRESHOLD)
    );

    fix16_t start_time_measured = 0;

    for (int i = 0; i < start_len; i++)
    {
      if (start_data[i] > speed_at_measure_threshold)
      {
        start_time_measured = fix16_from_float((float)i / start_len * start_time_ticks / APP_TICK_FREQUENCY);
        break;
      }
    }

    fix16_t stop_steady_speed = stop_data[stop_len - 1];
    fix16_t stop_initial_speed = stop_data[0];
    speed_at_measure_threshold = fix16_mul(
      stop_steady_speed,
      F16(1.0 - SPEED_MEASURE_THRESHOLD)
    ) + fix16_mul(
      stop_initial_speed,
      F16(SPEED_MEASURE_THRESHOLD)
    );

    fix16_t stop_time_measured = 0;

    for (int i = 0; i < stop_len; i++)
    {
      if (stop_data[i] < speed_at_measure_threshold)
      {
        stop_time_measured = fix16_from_float(float(i) / stop_len * stop_time_ticks / APP_TICK_FREQUENCY);
        break;
      }
    }

    fix16_t start_stop_time_measured = start_time_measured + stop_time_measured;

    // Assume that motor is 1-st order dynamic system,
    // calculate time when it will cross SPEED_IDEAL_THRESHOLD
    fix16_t start_stop_time = fix16_mul(
      start_stop_time_measured,
      start_stop_adjust
    );

    return start_stop_time;
  }

  void wait_stable_speed_init() {
    speed_log[2] = fix16_minimum;  // prevent false positives on empty data
    measure_attempts = 0;
    ticks_cnt = 0;
    median_filter.reset();
  }

  void wait_stable_speed(
    fix16_t current_speed,
    State next_state_on_success,
    int max_attempts
  ) {
    median_filter.add(current_speed);
    ticks_cnt++;

    // ~ 0.25s
    if (ticks_cnt >= 12)
    {
      speed_log[0] = speed_log[1];
      speed_log[1] = speed_log[2];
      speed_log[2] = median_filter.result();

      // if speed stable OR waited > 3 sec => record data

      if (measure_attempts > max_attempts) {
        set_state(next_state_on_success);
        return;
      }

      // Speed stable if difference <= 5 %.
      fix16_t a = speed_log[0], b = speed_log[1], c = speed_log[2];

      fix16_t min = (a <= b && a <= c) ? a : ((b <= a && b <= c) ? b : c);
      fix16_t max = (a >= b && a >= c) ? a : ((b >= a && b >= c) ? b : c);

      fix16_t diff = max - min;

      fix16_t abs_max = max > 0 ? max : - max;
      fix16_t abs_diff = diff > 0 ? diff : - diff;

      if (abs_diff <= abs_max * 5 / 100) {
        set_state(next_state_on_success);
        return;
      }

      // Prepare for next measure attempt
      measure_attempts++;
      ticks_cnt = 0;
      median_filter.reset();
    }
  }
};



#endif
