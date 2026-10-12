#pragma once
#include "util/math/vector3.h"
#include "util/math/quaternion.h"

struct Quat
{
	float w;
	float x;
	float y;
	float z;

	Quat()
	{
		w = 1.0f;
		x = 0.0f;
		y = 0.0f;
		z = 0.0f;
	}

	Quat(float inW, float inX, float inY, float inZ)
	{
		w = inW;
		x = inX;
		y = inY;
		z = inZ;
	}

	static Quat AngleAxis(float inAngle, float inX, float inY, float inZ)
	{
		Quat result = Quat(cosf(inAngle * 0.5f), inX, inY, inZ);
		result.Normalize();
		return result;
	}

	void Set(float inW, float inX, float inY, float inZ)
	{
		w = inW;
		x = inX;
		y = inY;
		z = inZ;
	}

	Quat& operator*=(const Quat& rhs)
	{
		Set(w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z,
			w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
			w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x,
			w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w);
		return *this;
	}

	friend Quat operator*(Quat lhs, const Quat& rhs)
	{
		lhs *= rhs;
		return lhs;
	}

	void Normalize()
	{
		const float length = sqrtf(x * x + y * y + z * z);
		float targetLength = 1.0f - w * w;
		if (targetLength <= 0.0f || length <= 0.0f)
		{
			Set(1.0f, 0.0f, 0.0f, 0.0f);
			return;
		}
		targetLength = sqrtf(targetLength);
		const float fixFactor = targetLength / length;

		x *= fixFactor;
		y *= fixFactor;
		z *= fixFactor;

		return;
	}

	Quat Normalized() const
	{
		Quat result = *this;
		result.Normalize();
		return result;
	}

	void Conjugate()
	{
		x = -x;
		y = -y;
		z = -z;
		return;
	}

	Quat Conjugated() const
	{
		Quat result = *this;
		result.Conjugate();
		return result;
	}
};

class MotionSample
{
public:
	MotionSample() = default;

	MotionSample(float acc[3], float accAcceleration, float gyro[3], float orientation[3],
		float quaternion[4]
	)
	{
		m_acc[0] = acc[0];
		m_acc[1] = acc[1];
		m_acc[2] = acc[2];
		m_accAcceleration = accAcceleration;
		m_gyro[0] = gyro[0];
		m_gyro[1] = gyro[1];
		m_gyro[2] = gyro[2];
		m_orientation[0] = orientation[0];
		m_orientation[1] = orientation[1];
		m_orientation[2] = orientation[2];
		m_q[0] = quaternion[0];
		m_q[1] = quaternion[1];
		m_q[2] = quaternion[2];
		m_q[3] = quaternion[3];
		m_accMagnitude = sqrtf(m_acc[0] * m_acc[0] + m_acc[1] * m_acc[1] + m_acc[2] * m_acc[2]);
	}

	void getVPADOrientation(float orientation[3])
	{
		orientation[0] = m_orientation[0];
		orientation[1] = m_orientation[1];
		orientation[2] = m_orientation[2];
	}

	void getVPADGyroChange(float gyro[3])
	{
		if (fabs(gyro[0]) < 0.012f)
			gyro[0] = 0.0f;
		if (fabs(gyro[1]) < 0.012f)
			gyro[1] = 0.0f;
		if (fabs(gyro[2]) < 0.012f)
			gyro[2] = 0.0f;
		gyro[0] = _radToOrientation(-m_gyro[0]);
		gyro[1] = _radToOrientation(-m_gyro[1]);
		gyro[2] = _radToOrientation(m_gyro[2]);
	}

	void getVPADAccelerometer(float acc[3])
	{
		acc[0] = -m_acc[0];
		acc[1] = -m_acc[1];
		acc[2] = m_acc[2];
	}

	float getVPADAccMagnitude()
	{
		return m_accMagnitude;
	}

	float getVPADAccAcceleration()
	{
		return m_accAcceleration;
	}

	void getVPADAccXY(float accXY[2])
	{
		float invMag = 1.0f / m_accMagnitude;
		float normAcc[3];
		normAcc[0] = m_acc[0] * invMag;
		normAcc[1] = m_acc[1] * invMag;
		normAcc[2] = m_acc[2] * invMag;
		accXY[0] = sqrtf(normAcc[0] * normAcc[0] + normAcc[1] * normAcc[1]);
		accXY[1] = -sin(getAtanPitch(-normAcc[2], normAcc[0], -normAcc[1]));
	}

	void getXVector(float vOut[3], Quaternionf& q)
	{
		float X = q.x;
		float Y = q.y;
		float Z = q.z;
		float W = q.w;
		float xy = X * Y;
		float xz = X * Z;
		float yy = Y * Y;
		float yw = Y * W;
		float zz = Z * Z;
		float zw = Z * W;
		vOut[0] = 1.0f - 2.0f * (yy + zz);
		vOut[2] = 2.0f * (xy + zw);
		vOut[1] = 2.0f * (xz - yw);
	}

	void getVPADAttitudeMatrix(float mtx[9])
	{
		Quaternionf qImu(m_q[0], m_q[1], m_q[2], m_q[3]);
		Quaternionf qY = qImu * Quaternionf::FromAngleAxis(1.5708f * 1.0f, 0.0f, 0.0f, 1.0f);
		Quaternionf qZ = qImu * Quaternionf::FromAngleAxis(1.5708f * 1.0f, 0.0f, 1.0f, 0.0f);
 		getXVector(mtx + 0, qImu);
		getXVector(mtx + 3, qY);
		getXVector(mtx + 6, qZ);
	}

	static float calculateAccAcceleration(float prevAcc[3], float currentAcc[3])
	{
		float ax = currentAcc[0] - prevAcc[0];
		float ay = currentAcc[1] - prevAcc[1];
		float az = currentAcc[2] - prevAcc[2];
		return sqrtf(ax * ax + ay * ay + az * az);
	}

	void getAccelerometer(float acc[3])
	{
		acc[0] = m_acc[0];
		acc[1] = m_acc[1];
		acc[2] = m_acc[2];
	}
	void getGyrometer(float gyro[3])
	{
		gyro[0] = m_gyro[0];
		gyro[1] = m_gyro[1];
		gyro[2] = m_gyro[2];
	}

private:
	static float _radToOrientation(float rad)
	{
		return rad / (2.0f * 3.14159265f);
	}

	static float getAtanPitch(float X, float Y, float Z)
	{
		return atan2f(-X, sqrtf(Y * Y + Z * Z));
	}

	float m_gyro[3]{};
	float m_acc[3]{};
	float m_accAcceleration{};
	float m_orientation[3]{};
	float m_q[4]{};
	float m_accMagnitude{};
};

