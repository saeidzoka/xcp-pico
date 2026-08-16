/**
 * @file    servo.h
 * @brief   SG90 servo driver for xcp-pico.
 *
 * Generates a 50 Hz PWM signal on GP15. Pulse width maps linearly
 * from 500 us (-90 deg) through 1500 us (0 deg) to 2500 us (+90 deg).
 *
 * PWM configuration:
 *   System clock : 150 MHz
 *   Divider      : 150  ->  counter clock = 1 MHz (1 count = 1 us)
 *   Wrap         : 19999 -> period = 20 ms -> 50 Hz
 *
 * The commanded angle is exposed as c_servo_angle in xcp_app.h and
 * can be written live via XCP DOWNLOAD.
 */

#ifndef SERVO_H
#define SERVO_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the PWM peripheral and set servo to 0 degrees.
 */
void servo_init(void);

/**
 * @brief Command the servo to move to angle_deg.
 *
 * @param angle_deg  Target angle in degrees. Clamped to [-90, +90].
 */
void servo_set_angle(float angle_deg);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_H */