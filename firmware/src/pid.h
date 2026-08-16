/**
 * @file    pid.h
 * @brief   PID controller for xcp-pico (Milestone 6, v1: textbook form).
 *
 * Implements a standard discrete-time PID controller:
 *
 *   error  = setpoint - measurement
 *   P      = Kp * error
 *   I      = I + Ki * error * dt
 *   D      = Kd * (error - prev_error) / dt
 *   output = P + I + D
 *
 * All gains and the setpoint are exposed as volatile float in xcp_app.h
 * and writable live via XCP DOWNLOAD.
 *
 * KNOWN LIMITATION (intentional, this commit only):
 * This version has no integral anti-windup. If error remains large for
 * an extended period (e.g. because the actuator cannot physically reach
 * the setpoint), the integral term grows without bound and causes severe
 * overshoot on recovery. Anti-windup (integral clamping) is added in a
 * following commit. The downstream actuator (servo_set_angle) clamps its
 * input to [-90, +90] regardless, so this limitation cannot damage
 * hardware, only degrade transient response.
 *
 * Refs: standard discrete PID form, e.g. Astrom & Hagglund,
 * "PID Controllers: Theory, Design, and Tuning", 2nd ed., Ch. 2.
 */

#ifndef PID_H
#define PID_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reset controller internal state (integral term, previous error).
 *
 * Does not touch c_Kp, c_Ki, c_Kd, or c_setpoint. Call once at startup.
 */
void pid_init(void);

/**
 * @brief Compute one PID update step.
 *
 * @param[in]  measurement  Current process value (e.g. g_pitch_deg).
 * @param[in]  dt_s         Time elapsed since the previous call, in seconds.
 *                          Must be > 0. Values <= 0 are ignored (returns
 *                          the previous output unchanged).
 *
 * @return  Controller output. Also stored in g_pid_output for XCP readout.
 */
float pid_update(float measurement, float dt_s);

#ifdef __cplusplus
}
#endif

#endif /* PID_H */