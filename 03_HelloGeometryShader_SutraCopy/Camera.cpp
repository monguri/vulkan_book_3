#include "Camera.h"
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_access.hpp>

void Camera::SetLookAt(const glm::vec3& eyePos, const glm::vec3& target, const glm::vec3& up)
{
	m_view = glm::lookAt(eyePos, target, up);
}

void Camera::OnMouseButtonDown(int buttonType)
{
	m_isDragged = true;
	m_buttonType = buttonType;
}

void Camera::OnMouseButtonUp()
{
	m_isDragged = false;
	m_buttonType = -1;
}

void Camera::OnMouseMove(int dx, int dy)
{
	if (!m_isDragged)
	{
		return;
	}

	switch (m_buttonType)
	{
		case 0:
		{
			const glm::vec3& axisX = glm::vec3(glm::row(m_view, 0));
			const glm::mat4& ry = glm::rotate(float(dx) * 0.01f, glm::vec3(0.0f, 1.0f, 0.0f));
			const glm::mat4& rx = glm::rotate(float(dy) * 0.01f, axisX);
			m_view = m_view * ry * rx;
		}
			break;
		case 1:
		{
			// TODO:åvéZÇÇÊÇ≠óùâÇµÇƒÇ»Ç¢
			glm::mat4 invMat = glm::inverse(m_view);
			const glm::vec3& forward = glm::vec3(glm::column(invMat, 2));
			glm::vec3 pos = glm::vec3(glm::column(invMat, 3));
			pos += forward * float(dy * 0.1f);
			invMat[3] = glm::vec4(pos, 1.0f);
			m_view = glm::inverse(invMat);
		}
			break;
		case 2:
		{
			const glm::mat4& m = glm::translate(glm::vec3(dx * 0.05f, dy * -0.05f, 0.0f));
			m_view = m_view * m;
		}
			break;
		default:
			assert(false);
			break;
	}
}

glm::vec3 Camera::GetPosition() const
{
	const glm::vec3& axis0 = glm::vec3(glm::column(m_view, 0));
	const glm::vec3& axis1 = glm::vec3(glm::column(m_view, 1));
	const glm::vec3& axis2 = glm::vec3(glm::column(m_view, 2));
	const glm::vec3& axis3 = glm::vec3(glm::column(m_view, 3));

	float x = -glm::dot(axis0, axis3);
	float y = -glm::dot(axis1, axis3);
	float z = -glm::dot(axis2, axis3);

	return glm::vec3(x, y, z);
}

