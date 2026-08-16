/**
 * @file    pid.h
 * @brief   PID controller for xcp-pico (Milestone 6, v2: anti-windup).
 *
 * Implements a discrete-time PID controller with integral clamping:
 *
 *   error    = setpoint - measurement
 *   P        = Kp * error
 *   integral = clamp(integral + error * dt, -INTEGRAL_LIMIT_DEG, +INTEGRAL_LIMIT_DEG)
 *   I        = Ki * integral
 *   D        = Kd * (error - prev_error) / dt
 *   output   = P + I + D
 *
 * All gains and the setpoint are exposed as volatile float in xcp_app.h
 * and writable live via XCP DOWNLOAD.
 *
 * ANTI-WINDUP (this commit):
 * The integral accumulator is clamped to +-INTEGRAL_LIMIT_DEG (90 deg,
 * matching the servo's physical travel limit). This prevents the integral
 * term from growing without bound while the actuator cannot reach the
 * setpoint, which previously caused severe overshoot on recovery. Clamping
 * the accumulator itself (rather than the resulting I term) means changing
 * Ki live via XCP does not cause a discontinuous jump in output, since the
 * stored integral value stays bounded independent of the gain applied to it.
 *
 * KNOWN LIMITATION (intentional, addressed in a following commit):
 * The derivative term is still computed on the error signal:
 *   D = Kd * (error - prev_error) / dt
 * A step change in c_setpoint therefore causes a step change in error,
 * which produces a large instantaneous spike in the D term ("derivative
 * kick"). Derivative-on-measurement, which computes D from the negative
 * rate of change of the measurement instead of the error, eliminates this
 * and is the subject of the next commit.
 *
 * Refs: standard discrete PID form with integral clamping, e.g.
 * Astrom & Hagglund, "PID Controllers: Theory, Design, and Tuning",
 * 2nd ed., Ch. 2 and Ch. 6 (windup).
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