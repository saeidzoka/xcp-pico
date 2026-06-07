/**
 * @file    mpu6050.h
 * @brief   MPU-6050 IMU driver for xcp-pico.
 *
 * Provides initialisation and a polling task for the InvenSense MPU-6050
 * 6-axis IMU. Reads raw accelerometer and gyroscope data over I2C and
 * computes a pitch angle via a complementary filter.
 *
 * Hardware:
 *   I2C0, GP4 (SDA) / GP5 (SCL), 400 kHz.
 *   I2C address: 0x68 (AD0 = GND).
 *
 * All output variables are declared in xcp_app.h and accessible via XCP.
 *
 * Refs: InvenSense MPU-6050 Product Specification Rev 3.4, Section 4.
 */

#ifndef MPU6050_H
#define MPU6050_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the MPU-6050 and the I2C peripheral.
 *
 * Configures I2C0 at 400 kHz on GP4/GP5, wakes the sensor from sleep,
 * sets accelerometer range to ±2 g and gyroscope range to ±250 deg/s.
 * Verifies the WHO_AM_I register before returning.
 *
 * @return true if the sensor was found and configured successfully.
 *         false if no ACK received at address 0x68 (check wiring).
 */
bool mpu6050_init(void);

/**
 * @brief Read sensor data and update all XCP-visible variables.
 *
 * Must be called repeatedly from the main loop. Each call:
 *   1. Reads 14 bytes of raw data (accel + temp + gyro) in one I2C burst.
 *   2. Scales to physical units (g, deg/s).
 *   3. Applies the complementary filter to update g_pitch_deg.
 *
 * The filter time step dt is computed internally from the time elapsed
 * since the previous call. First-call dt defaults to 10 ms.
 */
void mpu6050_task(void);

#ifdef __cplusplus
}
#endif

#endif /* MPU6050_H */