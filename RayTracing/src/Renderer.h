#pragma once

#include "Walnut/Image.h"

#include <memory>
#include <glm/glm.hpp>

#include "Camera.h"
#include "Scene.h"

#include "Ray.h"

class Renderer
{
public:
	struct Settings
	{
		bool Accumulate = true;
	};

	Renderer() = default;
	void OnResize(uint32_t width, uint32_t height);
	void Render(const Scene& scene,  const Camera& camera);

	std::shared_ptr<Walnut::Image> GetFinalImage() const { return m_FinalImage; }

	void ResetFrameIndex() { m_FrameIndex = 1; }
	Settings& GetSettings() { return m_Settings; }
private:
	struct HitMessage
	{
		float HitDistance;
		glm::vec3 WorldPosition;
		glm::vec3 WorldNormal;

		int ObjectIndex;
	};

	HitMessage TraceRay(const Ray& ray);
	glm::vec4 PerPixel(int x, int y);
	HitMessage ClosestHit(const Ray& ray, float hitDistance, int objectIndex);
	HitMessage MissHit(const Ray& ray);
	
private:
	std::shared_ptr<Walnut::Image> m_FinalImage;
	uint32_t* m_ImageData = nullptr;
	glm::vec4* m_AccumulationData = nullptr;
	const Scene* m_ActiveScene = nullptr;
	const Camera* m_ActiveCamera = nullptr;
	Settings m_Settings;
	
	uint32_t m_FrameIndex = 1;
	std::vector<uint32_t> m_ImageVerticalIterator, m_ImageHorizontalIterator;
};