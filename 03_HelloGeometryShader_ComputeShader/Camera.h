#pragma once

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <glm/glm.hpp>

class Camera
{
public:
	void SetLookAt(const glm::vec3& eyePos, const glm::vec3& target, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));

	void OnMouseButtonDown(int buttonType);
	void OnMouseButtonUp();
	void OnMouseMove(int dx, int dy);

	glm::mat4 GetViewMatrix() const { return m_view; }
	glm::vec3 GetPosition() const;

private:
	bool m_isDragged = false;
	int m_buttonType = -1;
	glm::mat4 m_view;
};

