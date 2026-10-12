#pragma once

#include <math.h>
#include <cmath>
#include "util/math/quaternion.h"

class MahonySensorFusion
{
public:
	MahonySensorFusion()
	{
		m_imuQ.Assign(sqrtf(0.5), sqrtf(0.5), 0.0f, 0.0f);
	}

	void updateIMU(float deltaTime, float gx, float gy, float gz, float ax, float ay, float az)
	{
		Vector3f av(ax, ay, az);
		Vector3f gv(gx, gy, gz);
		if (deltaTime > 0.2f)
			deltaTime = 0.2f;
		updateGyroBias(gx, gy, gz);
		gv.x -= m_gyroBias[0];
		gv.y -= m_gyroBias[1];
		gv.z -= m_gyroBias[2];

		if (fabs(gv.x) < 0.015f)
			gv.x = 0.0f;
		if (fabs(gv.y) < 0.015f)
			gv.y = 0.0f;
		if (fabs(gv.z) < 0.015f)
			gv.z = 0.0f;

		if (fabs(av.x) > 0.000001f || fabs(av.y) > 0.000001f || fabs(av.z) > 0.000001f)
		{
			av.Normalize();
			Vector3f grav = m_imuQ.GetVectorZ();
			grav.Scale(0.5f);
			Vector3f errorFeedback = grav.Cross(av);
			gv -= errorFeedback;
		}
		gv.Scale(0.5f * deltaTime);
		m_imuQ += (m_imuQ * Quaternionf(0.0f, gv.x, gv.y, gv.z));
		m_imuQ.NormalizeXYZW();
		updateOrientationAngles();
	}

	float getRollRadians()
	{
		return m_roll + (float)m_rollWinding * 2.0f * 3.14159265f;
	}

	float getPitchRadians()
	{
		return m_pitch + (float)m_pitchWinding * 2.0f * 3.14159265f;
	}

	float getYawRadians()
	{
		return m_yaw + (float)m_yawWinding * 2.0f * 3.14159265f;
	}

	void getQuaternion(float q[4]) const
	{
		q[0] = m_imuQ.w;
		q[1] = m_imuQ.x;
		q[2] = m_imuQ.y;
		q[3] = m_imuQ.z;
	}

	void getGyroBias(float gBias[3]) const
	{
		gBias[0] = m_gyroBias[0];
		gBias[1] = m_gyroBias[1];
		gBias[2] = m_gyroBias[2];
	}

private:

	void calcOrientation()
	{
		float sinr_cosp = 2.0f * (m_imuQ.z * m_imuQ.w + m_imuQ.x * m_imuQ.y);
		float cosr_cosp = 1.0f - 2.0f * (m_imuQ.w * m_imuQ.w + m_imuQ.x * m_imuQ.x);
		m_roll = std::atan2(sinr_cosp, cosr_cosp);

		float sinp = 2.0f * (m_imuQ.z * m_imuQ.x - m_imuQ.y * m_imuQ.w);
		if (std::abs(sinp) >= 1.0)
			m_pitch = std::copysign(3.14159265359f / 2.0f, sinp);
		else
			m_pitch = std::asin(sinp);

		float siny_cosp = 2.0f * (m_imuQ.z * m_imuQ.y + m_imuQ.w * m_imuQ.x);
		float cosy_cosp = 1.0f - 2.0f * (m_imuQ.x * m_imuQ.x + m_imuQ.y * m_imuQ.y);
		m_yaw = std::atan2(siny_cosp, cosy_cosp);
	}

	void updateOrientationAngles()
	{
		auto calcWindingCountChange = [](float prevAngle, float newAngle) -> int
		{
			if (newAngle > prevAngle)
			{
				float angleDif = newAngle - prevAngle;
				if (angleDif > 3.14159265f)
					return -1;
			}
			else if (newAngle < prevAngle)
			{
				float angleDif = prevAngle - newAngle;
				if (angleDif > 3.14159265f)
					return 1;
			}
			return 0;
		};
		float prevRoll = m_roll;
		float prevPitch = m_pitch;
		float prevYaw = m_yaw;
		calcOrientation();
		m_rollWinding += calcWindingCountChange(prevRoll, m_roll);
		m_pitchWinding += calcWindingCountChange(prevPitch, m_pitch);
		m_yawWinding += calcWindingCountChange(prevYaw, m_yaw);
	}

	void updateGyroBias(float gx, float gy, float gz)
	{
		if (fabs(gx) >= 0.35f || fabs(gy) >= 0.35f || fabs(gz) >= 0.35f)
			return;

		m_gyroTotalSum[0] += gx;
		m_gyroTotalSum[1] += gy;
		m_gyroTotalSum[2] += gz;
		m_gyroTotalSampleCount++;
		if (m_gyroTotalSampleCount >= 200)
		{
			m_gyroBias[0] = (float)(m_gyroTotalSum[0] / (double)m_gyroTotalSampleCount);
			m_gyroBias[1] = (float)(m_gyroTotalSum[1] / (double)m_gyroTotalSampleCount);
			m_gyroBias[2] = (float)(m_gyroTotalSum[2] / (double)m_gyroTotalSampleCount);
		}
	}

	private:
		Quaternionf m_imuQ;
		float m_roll{};
		float m_pitch{};
		float m_yaw{};
		int m_rollWinding{};
		int m_pitchWinding{};
		int m_yawWinding{};
		float m_gyroBias[3]{};
		double m_gyroTotalSum[3]{};
		uint64 m_gyroTotalSampleCount{};
};
