/**
 * @file    servo.c
 * @brief   SG90 servo driver implementation.
 */

#include "servo.h"
#include "xcp_app.h"

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"

/* -------------------------------------------------------------------------
 * Hardware configuration
 * ------------------------------------------------------------------------- */

#define SERVO_PIN        15U

/* -------------------------------------------------------------------------
 * PWM timing constants
 *
 * System clock = 150 MHz
 * Divider      = 150  ->  counter clock = 1 MHz  (1 count = 1 us)
 * Wrap         = 19999 -> period = 20 ms = 50 Hz
 * ------------------------------------------------------------------------- */

#define PWM_DIVIDER      150U
#define PWM_WRAP         19999U

#define PULSE_MIN_US     500U    /* -90 deg */
#define PULSE_CENTER_US  1500U   /*   0 deg */
#define PULSE_MAX_US     2500U   /* +90 deg */

#define ANGLE_MAX_DEG    90.0f

/* -------------------------------------------------------------------------
 * XCP-visible variable definition
 * Declared as extern in xcp_app.h
 * ------------------------------------------------------------------------- */

volatile float c_servo_angle = 0.0f;

/* -------------------------------------------------------------------------
 * Module-private state
 * ------------------------------------------------------------------------- */

static uint s_slice;
static uint s_channel;

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void servo_init(void)
{
    gpio_set_function(SERVO_PIN, GPIO_FUNC_PWM);

    s_slice   = pwm_gpio_to_slice_num(SERVO_PIN);
    s_channel = pwm_gpio_to_channel(SERVO_PIN);

    pwm_set_clkdiv_int_frac(s_slice, PWM_DIVIDER, 0U);
    pwm_set_wrap(s_slice, PWM_WRAP);
    pwm_set_chan_level(s_slice, s_channel, PULSE_CENTER_US);
    pwm_set_enabled(s_slice, true);
}

void servo_set_angle(float angle_deg)
{
    /* Clamp to valid range */
    if (angle_deg >  ANGLE_MAX_DEG) angle_deg =  ANGLE_MAX_DEG;
    if (angle_deg < -ANGLE_MAX_DEG) angle_deg = -ANGLE_MAX_DEG;

    /*
     * Map [-90, +90] -> [500, 2500] us
     * pulse_us = 1500 + (angle / 90) * 1000
     */
    float pulse_us = (float)PULSE_CENTER_US
                   + (angle_deg / ANGLE_MAX_DEG) * 1000.0f;

    pwm_set_chan_level(s_slice, s_channel, (uint16_t)pulse_us);
}