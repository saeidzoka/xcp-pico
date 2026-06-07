/**
 * @file    mpu6050.c
 * @brief   MPU-6050 IMU driver implementation.
 *
 * Refs: InvenSense MPU-6050 Product Specification Rev 3.4.
 *       Register map: Section 4 (MPU-6050 Register Map and Descriptions).
 */

#include "mpu6050.h"
#include "xcp_app.h"

#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

/* -------------------------------------------------------------------------
 * Hardware configuration
 * ------------------------------------------------------------------------- */

#define I2C_PORT        i2c0
#define I2C_SDA_PIN     4
#define I2C_SCL_PIN     5
#define I2C_FREQ_HZ     400000U
#define MPU6050_ADDR    0x68U

/* -------------------------------------------------------------------------
 * MPU-6050 register addresses
 * Ref: MPU-6050 Register Map Rev 4.2
 * ------------------------------------------------------------------------- */

#define REG_SMPLRT_DIV    0x19U   /* Sample rate divider                    */
#define REG_CONFIG        0x1AU   /* DLPF configuration                     */
#define REG_GYRO_CONFIG   0x1BU   /* Gyroscope full-scale range             */
#define REG_ACCEL_CONFIG  0x1CU   /* Accelerometer full-scale range         */
#define REG_ACCEL_XOUT_H  0x3BU   /* First byte of accel/temp/gyro burst    */
#define REG_PWR_MGMT_1    0x6BU   /* Power management (sleep bit)           */
#define REG_WHO_AM_I      0x75U   /* Device ID: always 0x68 for MPU-6050    */

/* -------------------------------------------------------------------------
 * Scale factors
 * Accel ±2 g:       16384 LSB/g   -> 1/16384
 * Gyro  ±250 deg/s: 131   LSB/(deg/s) -> 1/131
 * Ref: MPU-6050 PS Rev 3.4, Section 6.1, 6.2
 * ------------------------------------------------------------------------- */

#define ACCEL_SCALE     (1.0f / 16384.0f)
#define GYRO_SCALE      (1.0f / 131.0f)
#define DEG_PER_RAD     (180.0f / (float)M_PI)
#define DEFAULT_DT_S    0.01f     /* 10 ms fallback for first call          */
#define MAX_DT_S        0.1f      /* Clamp: beyond this, filter is unstable */

/* -------------------------------------------------------------------------
 * XCP-visible variable definitions
 * Declared as extern in xcp_app.h
 * ------------------------------------------------------------------------- */

volatile float g_accel_x = 0.0f;
volatile float g_accel_y = 0.0f;
volatile float g_accel_z = 0.0f;

volatile float g_gyro_x  = 0.0f;
volatile float g_gyro_y  = 0.0f;
volatile float g_gyro_z  = 0.0f;

volatile float g_pitch_deg = 0.0f;

volatile float c_alpha = 0.98f;

/* -------------------------------------------------------------------------
 * Module-private state
 * ------------------------------------------------------------------------- */

static absolute_time_t s_last_time;
static bool s_initialised = false;

/* -------------------------------------------------------------------------
 * I2C helpers
 * ------------------------------------------------------------------------- */

static bool write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_write_blocking(I2C_PORT, MPU6050_ADDR, buf, 2, false) == 2;
}

static bool read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    if (i2c_write_blocking(I2C_PORT, MPU6050_ADDR, &reg, 1, true) != 1) {
        return false;
    }
    return i2c_read_blocking(I2C_PORT, MPU6050_ADDR, buf, len, false) == (int)len;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

bool mpu6050_init(void)
{
    i2c_init(I2C_PORT, I2C_FREQ_HZ);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    sleep_ms(100);  /* Wait for sensor power-up */

    /* Verify device identity */
    uint8_t who_am_i = 0;
    if (!read_regs(REG_WHO_AM_I, &who_am_i, 1) || who_am_i != 0x68U) {
        return false;
    }

    /* Wake from sleep: clear SLEEP bit in PWR_MGMT_1 */
    if (!write_reg(REG_PWR_MGMT_1, 0x00U)) return false;
    sleep_ms(10);

    /* Gyroscope full-scale range: ±250 deg/s (FS_SEL = 0) */
    if (!write_reg(REG_GYRO_CONFIG, 0x00U)) return false;

    /* Accelerometer full-scale range: ±2 g (AFS_SEL = 0) */
    if (!write_reg(REG_ACCEL_CONFIG, 0x00U)) return false;

    s_last_time   = get_absolute_time();
    s_initialised = true;
    return true;
}

void mpu6050_task(void)
{
    if (!s_initialised) return;

    /* ------------------------------------------------------------------ */
    /* 1. Compute dt                                                        */
    /* ------------------------------------------------------------------ */
    absolute_time_t now = get_absolute_time();
    int64_t dt_us = absolute_time_diff_us(s_last_time, now);
    s_last_time   = now;

    float dt_s = (float)dt_us * 1.0e-6f;
    if (dt_s <= 0.0f || dt_s > MAX_DT_S) {
        dt_s = DEFAULT_DT_S;
    }

    /* ------------------------------------------------------------------ */
    /* 2. Read 14 bytes: ACCEL_XOUT_H through GYRO_ZOUT_L                  */
    /*    Layout: ax_H ax_L  ay_H ay_L  az_H az_L                          */
    /*            temp_H temp_L                                             */
    /*            gx_H gx_L  gy_H gy_L  gz_H gz_L                          */
    /* ------------------------------------------------------------------ */
    uint8_t raw[14];
    if (!read_regs(REG_ACCEL_XOUT_H, raw, sizeof(raw))) return;

    int16_t ax = (int16_t)((uint16_t)(raw[0]  << 8) | raw[1]);
    int16_t ay = (int16_t)((uint16_t)(raw[2]  << 8) | raw[3]);
    int16_t az = (int16_t)((uint16_t)(raw[4]  << 8) | raw[5]);
    /* raw[6], raw[7]: temperature - not used */
    int16_t gx = (int16_t)((uint16_t)(raw[8]  << 8) | raw[9]);
    int16_t gy = (int16_t)((uint16_t)(raw[10] << 8) | raw[11]);
    int16_t gz = (int16_t)((uint16_t)(raw[12] << 8) | raw[13]);

    /* ------------------------------------------------------------------ */
    /* 3. Scale to physical units                                           */
    /* ------------------------------------------------------------------ */
    g_accel_x = (float)ax * ACCEL_SCALE;
    g_accel_y = (float)ay * ACCEL_SCALE;
    g_accel_z = (float)az * ACCEL_SCALE;

    g_gyro_x = (float)gx * GYRO_SCALE;
    g_gyro_y = (float)gy * GYRO_SCALE;
    g_gyro_z = (float)gz * GYRO_SCALE;

    /* ------------------------------------------------------------------ */
    /* 4. Complementary filter                                             */
    /*    pitch_accel: angle from gravity (slow, no drift)                 */
    /*    pitch_gyro:  integrated rate   (fast, drifts over time)          */
    /*    g_pitch_deg = alpha * gyro_estimate + (1-alpha) * accel_estimate */
    /* ------------------------------------------------------------------ */
    float pitch_accel = atan2f(g_accel_y, g_accel_z) * DEG_PER_RAD;
    float pitch_gyro  = g_pitch_deg + g_gyro_x * dt_s;

    float alpha = c_alpha;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    g_pitch_deg = alpha * pitch_gyro + (1.0f - alpha) * pitch_accel;
}