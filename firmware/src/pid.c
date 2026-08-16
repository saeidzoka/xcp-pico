/**
 * @file    pid.c
 * @brief   PID controller implementation (Milestone 6, v2: anti-windup).
 */

#include "pid.h"
#include "xcp_app.h"

/* -------------------------------------------------------------------------
 * Anti-windup limit
 *
 * The integral accumulator is clamped to +-90 deg, matching the servo's
 * physical travel limit (servo_set_angle clamps its input to the same
 * range). See pid.h "ANTI-WINDUP" note.
 * ------------------------------------------------------------------------- */
#define INTEGRAL_LIMIT_DEG   90.0f

/* -------------------------------------------------------------------------
 * XCP-visible variable definitions
 * Declared as extern in xcp_app.h
 * ------------------------------------------------------------------------- */

/* Calibration parameters (read/write via XCP) */
volatile float c_Kp       = 1.0f;
volatile float c_Ki       = 0.0f;
volatile float c_Kd       = 0.0f;
volatile float c_setpoint = 0.0f;

/* Measurement / diagnostic output (read-only via XCP) */
volatile float g_pid_output = 0.0f;

/* -------------------------------------------------------------------------
 * Module-private state
 * ------------------------------------------------------------------------- */

static float s_integral;
static float s_prev_error;

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void pid_init(void)
{
    s_integral   = 0.0f;
    s_prev_error = 0.0f;
    g_pid_output = 0.0f;
}

float pid_update(float measurement, float dt_s)
{
    if (dt_s <= 0.0f) {
        return g_pid_output;
    }

    const float error = c_setpoint - measurement;

    const float p_term = c_Kp * error;

    /* Anti-windup: clamp the accumulator itself, not the resulting I term.
     * See pid.h "ANTI-WINDUP" note for rationale. */
    s_integral += error * dt_s;

    if (s_integral > INTEGRAL_LIMIT_DEG) {
        s_integral = INTEGRAL_LIMIT_DEG;
    } else if (s_integral < -INTEGRAL_LIMIT_DEG) {
        s_integral = -INTEGRAL_LIMIT_DEG;
    }

    const float i_term = c_Ki * s_integral;

    /* Derivative-on-error (still has derivative kick on setpoint steps;
     * see pid.h "KNOWN LIMITATION" note, addressed in a following commit). */
    const float d_term = c_Kd * (error - s_prev_error) / dt_s;
    s_prev_error = error;

    g_pid_output = p_term + i_term + d_term;
    return g_pid_output;
}