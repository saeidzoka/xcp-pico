/**
 * @file    pid.h
 * @brief   PID controller for xcp-pico (Milestone 6, v3: derivative-on-measurement).
 *
 * Implements a discrete-time PID controller with integral clamping and
 * derivative-on-measurement:
 *
 *   error    = setpoint - measurement
 *   P        = Kp * error
 *   integral = clamp(integral + error * dt, -INTEGRAL_LIMIT_DEG, +INTEGRAL_LIMIT_DEG)
 *   I        = Ki * integral
 *   D        = -Kd * (measurement - prev_measurement) / dt
 *   output   = P + I + D
 *
 * All gains and the setpoint are exposed as volatile float in xcp_app.h
 * and writable live via XCP DOWNLOAD.
 *
 * ANTI-WINDUP (previous commit):
 * The integral accumulator is clamped to +-INTEGRAL_LIMIT_DEG (90 deg,
 * matching the servo's physical travel limit). See pid.c for detail.
 *
 * DERIVATIVE-ON-MEASUREMENT (this commit):
 * The derivative term is computed from the negative rate of change of the
 * measurement, not the error:
 *
 *   D = -Kd * (measurement - prev_measurement) / dt
 *
 * error = setpoint - measurement, so d(error)/dt = d(setpoint)/dt -
 * d(measurement)/dt. A step change to c_setpoint via XCP DOWNLOAD makes
 * d(setpoint)/dt an impulse, which the previous derivative-on-error form
 * fed directly into D ("derivative kick"): a large, physically meaningless
 * spike on every setpoint change. Dropping the setpoint's contribution and
 * differentiating only the measurement removes this. The measurement
 * (g_pitch_deg, from a physical sensor) changes continuously, so D no
 * longer has step-driven spikes; it now responds only to how fast the
 * platform is actually moving, which is what the D term is meant to damp.
 *
 * This completes Milestone 6's three-stage PID plan (textbook ->
 * anti-windup -> derivative-on-measurement).
 *
 * Refs: derivative-on-measurement is a standard technique for avoiding
 * derivative kick; see e.g. Astrom & Hagglund, "PID Controllers: Theory,
 * Design, and Tuning", 2nd ed., Ch. 2.5.
 */

#ifndef PID_H
#define PID_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reset controller internal state (integral term, previous
 *        measurement). Does not touch c_Kp, c_Ki, c_Kd, or c_setpoint.
 *        Call once at startup.
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