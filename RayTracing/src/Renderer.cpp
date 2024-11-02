#include "Renderer.h"

#include "Walnut/Random.h"

#include <execution>

namespace Utils
{
	static uint32_t ConvertVec4ToInt(glm::vec4& color)
	{
		uint8_t r = (uint8_t)(color.r * 255.0f);
		uint8_t g = (uint8_t)(color.g * 255.0f);
		uint8_t b = (uint8_t)(color.b * 255.0f);
		uint8_t a = (uint8_t)(color.a * 255.0f);

		uint32_t result = (a << 24) | (b << 16) | (g << 8) | r;

		return result;
	}

	static uint32_t PCG_Hash(uint32_t input)
	{
		uint32_t state = input * 747796405u + 2891336453u;
		uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
		return (word >> 22u) ^ word;
	}

	static float RandomFloat(uint32_t& seed)
	{
		seed = PCG_Hash(seed);
		return (float)seed / (float)std::numeric_limits<uint32_t>::max();
	}

	static glm::vec3 InUnitSphere(uint32_t& seed)
	{
		return glm::normalize(glm::vec3
		(
			RandomFloat(seed) * 2.0f - 1.0f,
			RandomFloat(seed) * 2.0f - 1.0f,
			RandomFloat(seed) * 2.0f - 1.0f)
		);
	}
}

void Renderer::OnResize(uint32_t width, uint32_t height)
{
	// 如果不存在图像或者视口宽高发生改变，则创建（重新创建）图像、图像颜色缓冲区。
	if (m_FinalImage)
	{
		if (width == m_FinalImage->GetWidth() && height == m_FinalImage->GetHeight())
			return;

		m_FinalImage->Resize(width, height);
	}
	else
	{
		m_FinalImage = std::make_shared<Walnut::Image>(width, height, Walnut::ImageFormat::RGBA);
	}
	
	delete[] m_ImageData;
	m_ImageData = new uint32_t[width * height];

	delete[] m_AccumulationData;
	m_AccumulationData = new glm::vec4[width * height];

	m_ImageHorizontalIterator.resize(width);
	m_ImageVerticalIterator.resize(height);

	for (uint32_t i = 0; i < width; i++)
		m_ImageHorizontalIterator[i] = i;
	for (uint32_t i = 0; i < height; i++)
		m_ImageVerticalIterator[i] = i;
}

void Renderer::Render(const Scene& scene, const Camera& camera)
{
	m_ActiveScene = &scene;
	m_ActiveCamera = &camera;

	if (m_FrameIndex == 1)
	{
		memset(m_AccumulationData, 0, m_FinalImage->GetWidth() * m_FinalImage->GetHeight() * sizeof(glm::vec4));
	}

	// 多线程优化
#define MT 1
#if MT

	std::for_each(std::execution::par, m_ImageVerticalIterator.begin(), m_ImageVerticalIterator.end(),
		[this](uint32_t y) 
		{
			std::for_each(std::execution::par, m_ImageHorizontalIterator.begin(), m_ImageHorizontalIterator.end(), 
				[this, y](uint32_t x)
				{
					glm::vec4 color = PerPixel(x, y);
					m_AccumulationData[x + y * m_FinalImage->GetWidth()] += color;

					glm::vec4 accumulatedColor = m_AccumulationData[x + y * m_FinalImage->GetWidth()];
					accumulatedColor /= (float)m_FrameIndex;

					accumulatedColor = glm::clamp(accumulatedColor, glm::vec4(0.0f), glm::vec4(1.0f)); // 将rgba的值固定到0-1区间
					m_ImageData[x + y * m_FinalImage->GetWidth()] = Utils::ConvertVec4ToInt(accumulatedColor); // 将vec4转化为uint32输出给颜色缓冲区
				});
		});

#else

	// 设置像素颜色
	for (uint32_t y = 0; y < m_FinalImage->GetHeight(); y++)
	{
		for (uint32_t x = 0; x < m_FinalImage->GetWidth(); x++)
		{	
			glm::vec4 color = PerPixel(x, y);
			m_AccumulationData[x + y * m_FinalImage->GetWidth()] += color;

			glm::vec4 accumulatedColor = m_AccumulationData[x + y * m_FinalImage->GetWidth()];
			accumulatedColor /= (float)m_FrameIndex;

			accumulatedColor = glm::clamp(accumulatedColor, glm::vec4(0.0f), glm::vec4(1.0f)); // 将rgba的值固定到0-1区间
			m_ImageData[x + y * m_FinalImage->GetWidth()] = Utils::ConvertVec4ToInt(accumulatedColor); // 将vec4转化为uint32输出给颜色缓冲区
		}
	}

#endif
	// 将图像颜色缓冲区传递给图像
	m_FinalImage->SetData(m_ImageData);

	if (m_Settings.Accumulate)
		m_FrameIndex++;
	else m_FrameIndex = 1;
}

// 光线追踪
Renderer::HitMessage Renderer::TraceRay(const Ray& ray)
{
	// (bx^2 + by^2)t^2 + (2(axbx + ayby))t + (ax^2 + ay^2 - r^2) = 0
	// a = 光线的起点
	// b = 光线的方向
	// r = 圆的半径
	// t = 交点距离

	int closestSphere = -1;
	float hitDistance = FLT_MAX;
	for (size_t i = 0; i < m_ActiveScene->Sphere.size(); i++)
	{
		Sphere sphere = m_ActiveScene->Sphere[i];

		glm::vec3 origin = ray.Origin - sphere.Position;

		float a = glm::dot(ray.Direction, ray.Direction);
		float b = 2.0f * glm::dot(origin, ray.Direction);
		float c = glm::dot(origin, origin) - sphere.Radius * sphere.Radius;

		// 判别式 b^2 - 4ac
		float discriminant = b * b - 4.0f * a * c;

		if (discriminant < 0.0f)
			continue;
		// 求t
		// (-b +- 判别式) / 2a
		float tclose = (-b - glm::sqrt(discriminant)) / (2.0f * a); // 仅记录距离屏幕最近的t

		// 将距离屏幕最近的物体记录
		if (tclose < hitDistance && tclose > 0.0f)
		{
			hitDistance = tclose;
			closestSphere = (int)i;
		}
	}

	if (closestSphere < 0)
		return MissHit(ray);

	return ClosestHit(ray, hitDistance, closestSphere);
}

// 像素着色器
glm::vec4 Renderer::PerPixel(int x, int y)
{
	Ray ray;
	ray.Origin = m_ActiveCamera->GetPosition(); // 光线（视线）的起点，假设光线是从摄像机出发
	ray.Direction = m_ActiveCamera->GetRayDirections()[x + y * m_FinalImage->GetWidth()]; // 光线（视线）的方向

	// glm::vec3 color{ 0.0f };
	// float multiplier = 1.0f;
	glm::vec3 light{ 0.0f };
	glm::vec3 contribution{ 1.0f };

	uint32_t seed = x + y * m_FinalImage->GetWidth();
	seed *= m_FrameIndex;

	int bounces = 5; // 光线弹射次数
	for (size_t i = 0; i < bounces; i++)
	{
		seed += i;

		HitMessage hitMessage = TraceRay(ray);
		if (hitMessage.HitDistance < 0.0f)
		{
			glm::vec3 skyColor = glm::vec3( 0.6f, 0.7f, 0.9f );
			light += skyColor * contribution;
			break;
		}

		// glm::vec3 lightDir = glm::normalize(glm::vec3(-1.0f)); // 光线方向
		// float d = glm::max(glm::dot(hitMessage.WorldNormal, -lightDir), 0.0f); // 光线与物体表面法线程度

		const Sphere& closestSphere = m_ActiveScene->Sphere[hitMessage.ObjectIndex];
		const Material& material = m_ActiveScene->Materials[closestSphere.MaterialIndex];
		glm::vec3 sphereColor = material.Albedo;
		// sphereColor *= d;
		// color += sphereColor * multiplier;
		light += material.GetEmission();

		// 光线 弹射后
		contribution *= sphereColor;

		ray.Origin = hitMessage.WorldPosition + hitMessage.WorldNormal * 0.0001f; // 将反射后的光线起始点更改成交点
		// ray.Direction = glm::reflect(ray.Direction, 
		// 	 hitMessage.WorldNormal + material.Roughness * Walnut::Random::Vec3(-0.5f, 0.5f)); // 将反射后光线方向改为镜面反射方向
		// ray.Direction = glm::normalize(hitMessage.WorldNormal + Walnut::Random::InUnitSphere()); // 将反射后光线方向改为漫反射方向 原方案随机数
		ray.Direction = glm::normalize(hitMessage.WorldNormal + Utils::InUnitSphere(seed)); // 将反射后光线方向改为漫反射方向
	}

	return glm::vec4(light, 1.0f);
}

Renderer::HitMessage Renderer::ClosestHit(const Ray& ray, float hitDistance, int objectIndex)
{
	HitMessage hitMessage;
	hitMessage.HitDistance = hitDistance;
	hitMessage.ObjectIndex = objectIndex;

	const Sphere& closestsphere = m_ActiveScene->Sphere[objectIndex];

	// 求交点坐标
	// a + bt
	glm::vec3 origin = ray.Origin - closestsphere.Position;
	hitMessage.WorldPosition = origin + ray.Direction * hitMessage.HitDistance;
	hitMessage.WorldNormal = glm::normalize(hitMessage.WorldPosition); // 法线为交点-原点
	hitMessage.WorldPosition += closestsphere.Position;

	return hitMessage;
}

Renderer::HitMessage Renderer::MissHit(const Ray& ray)
{
	HitMessage hitMessage;
	hitMessage.HitDistance = -1;

	return hitMessage;
}
