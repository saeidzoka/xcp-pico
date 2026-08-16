/**
 * @file    xcp_app.h
 * @brief   Catalog of all application variables accessible via XCP.
 *
 * This header is the single reference point for every variable that the
 * XCP master can read or write. All variables are declared extern here;
 * their definitions live in the module that owns the data.
 *
 * Convention (AUTOSAR-inspired):
 *   Measurement variables  - read-only from master perspective (g_ prefix)
 *   Calibration parameters - read/write from master perspective (c_ prefix)
 *
 * The XCP protocol layer accesses these by address (SET_MTA / UPLOAD /
 * SHORT_UPLOAD / DOWNLOAD). Include this header only in modules that need
 * to reference these variables by name, such as the A2L generator or
 * application-level glue code.
 *
 * All variables are volatile to prevent compiler optimisation from caching
 * values that the XCP master may modify asynchronously.
 */

#ifndef XCP_APP_H
#define XCP_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * MPU-6050 measurements (read-only)
 * Defined in: mpu6050.c
 * ------------------------------------------------------------------------- */

/** Raw accelerometer readings in units of g (±2 g range). */
extern volatile float g_accel_x;
extern volatile float g_accel_y;
extern volatile float g_accel_z;

/** Raw gyroscope readings in degrees per second (±250 deg/s range). */
extern volatile float g_gyro_x;
extern volatile float g_gyro_y;
extern volatile float g_gyro_z;

/** Computed platform pitch angle in degrees.
 *  Positive = nose up. Updated every mpu6050_task() call.
 */
extern volatile float g_pitch_deg;

/* -------------------------------------------------------------------------
 * MPU-6050 calibration parameters (read/write)
 * Defined in: mpu6050.c
 * ------------------------------------------------------------------------- */

/** Complementary filter blending coefficient.
 *  Range: 0.0 - 1.0. Higher = more gyroscope weight, less drift correction.
 *  Default: 0.98. Writable via XCP DOWNLOAD for live tuning.
 */
extern volatile float c_alpha;

/* -------------------------------------------------------------------------
 * Servo calibration parameters (read/write)
 * Defined in: servo.c
 * ------------------------------------------------------------------------- */

/** Commanded servo angle in degrees. Range: [-90, +90].
 *  Writable via XCP DOWNLOAD for manual positioning and PID testing.
 */
extern volatile float c_servo_angle;

/* -------------------------------------------------------------------------
 * PID controller (read/write for gains and setpoint, read-only for output)
 * Defined in: pid.c
 * ------------------------------------------------------------------------- */

/** PID gains. Writable via XCP DOWNLOAD for live tuning. */
extern volatile float c_Kp;
extern volatile float c_Ki;
extern volatile float c_Kd;

/** Target pitch angle in degrees. Writable via XCP DOWNLOAD. */
extern volatile float c_setpoint;

/** Current controller output (before servo clamping). Read-only. */
extern volatile float g_pid_output;

#ifdef __cplusplus
}
#endif

#endif /* XCP_APP_H */