/* Mahony AHRS attitude filter (C port of the Arduino MahonyAHRS library) */
#ifndef G1_AHRS_H_
#define G1_AHRS_H_

#include <stdint.h>

/*
 * attitude filter state: the members of the Arduino Mahony class,
 * with Madgwick's beta added in front, invSampleFreq replaced by a measured
 * period and anglesComputed moved before the angles.
 */
struct ahrs {
	float beta;		/* 0.1 (unused by the Mahony update) */
	float twoKp;		/* 2 * proportional gain (Kp) */
	float twoKi;		/* 2 * integral gain (Ki) */
	float q0, q1, q2, q3;	/* quaternion of sensor frame relative to auxiliary frame */
	float integralFBx, integralFBy, integralFBz;	/* integral error terms scaled by Ki */
	char anglesComputed;
	uint8_t pad_29[3];
	float roll;		/* rad */
	float pitch;
	float yaw;
	uint32_t Now;		/* ms */
	uint32_t lastUpdate;	/* ms */
	float deltat;		/* s */
};

void Mahony_init(struct ahrs *a);
float Mahony_invSqrt(float x);
void Mahony_updateIMU(float gx, float gy, float gz, float ax, float ay, float az, float dt,
		      struct ahrs *a);
float Mahony_deltatUpdate(struct ahrs *a);
float Mahony_getRoll(struct ahrs *a);
float Mahony_getPitch(struct ahrs *a);
float Mahony_getYaw(struct ahrs *a);
void Mahony_computeAngles(struct ahrs *a);

#endif /* G1_AHRS_H_ */
