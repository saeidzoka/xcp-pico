/**
 * @file    pid.c
 * @brief   PID controller implementation (Milestone 6, v3: derivative-on-measurement).
 */

#include "pid.h"
#include "xcp_app.h"
#include <stdbool.h>

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
static float s_prev_measurement;
static bool  s_first_call;

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void pid_init(void)
{
    s_integral         = 0.0f;
    s_prev_measurement = 0.0f;
    s_first_call       = true;
    g_pid_output        = 0.0f;
}

float pid_update(float measurement, float dt_s)
{
    if (dt_s <= 0.0f) {
        return g_pid_output;
    }

    /* On the first call there is no previous measurement to differentiate
     * against. Seed it with the current value so D starts at zero instead
     * of producing a spike from an uninitialised (0.0f) baseline. */
    if (s_first_call) {
        s_prev_measurement = measurement;
        s_first_call = false;
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

    /* Derivative-on-measurement: differentiate the process value, not the
     * error, so a step change in c_setpoint does not spike D.
     * See pid.h "DERIVATIVE-ON-MEASUREMENT" note. */
    const float d_term = -c_Kd * (measurement - s_prev_measurement) / dt_s;
    s_prev_measurement = measurement;

    g_pid_output = p_term + i_term + d_term;
    return g_pid_output;
}