#pragma once

#include "Renderer.hpp"
#include "../CascadedShadowMap.hpp"
#include "../CubeShadowMap.hpp"
#include "../GeometryBuffer.hpp"
#include "../RenderBuffer.hpp"
#include "../SpotLightShadowMap.hpp"
#include "../../components/lighting/AmbientLight.hpp"
#include "../../components/lighting/DirLight.hpp"
#include "../../components/lighting/PointLight.hpp"
#include "../../components/lighting/SpotLight.hpp"
#include "../../math/Matrix.hpp"
#include "../shaders/ShaderFamily.hpp"

#include <memory>
#include <vector>


namespace leopph::internal
{
	class DeferredRenderer final : public Renderer
	{
		public:
			DeferredRenderer();

			auto Render() -> void override;

		private:
			// Fill the gbuffer with geometry data
			auto RenderGeometry(const Matrix4& camViewMat, const Matrix4& camProjMat, const std::vector<RenderableData>& renderables) -> void;

			auto RenderSkybox(const Matrix4& camViewMat, const Matrix4& camProjMat) -> void;

			// Draws into the dirlight shadow map, binds it to light shader with the necessary data, and returns the next usable texture unit.
			[[nodiscard]] auto RenderDirShadowMap(const DirectionalLight* dirLight,
			                                      const Matrix4& camViewInvMat,
			                                      const Matrix4& camProjMat,
			                                      std::span<const RenderableData> renderables,
			                                      ShaderProgram& lightShader,
			                                      ShaderProgram& shadowShader,
			                                      int nextTexUnit) -> int;
			// Draws into the first N shadow maps, binds them to the light shader with the necessary data, and returns the next usable texture unit.
			[[nodiscard]] auto RenderSpotShadowMaps(std::span<const SpotLight* const> spotLights,
			                                        std::span<const RenderableData> renderables,
			                                        ShaderProgram& lightShader,
			                                        ShaderProgram& shadowShader,
			                                        std::size_t numShadows,
			                                        int nextTexUnit) -> int;
			// Draws into the first N shadow maps, binds them to the light shader with the necessary data, and returns the next usable texture unit.
			[[nodiscard]] auto RenderPointShadowMaps(std::span<const PointLight* const> pointLights,
			                                         std::span<const RenderableData> renderables,
			                                         ShaderProgram& lightShader,
			                                         ShaderProgram& shadowShader,
			                                         std::size_t numShadows,
			                                         int nextTexUnit) -> int;

			static auto SetAmbientData(const AmbientLight& light, ShaderProgram& lightShader) -> void;
			static auto SetDirectionalData(const DirectionalLight* dirLight, ShaderProgram& lightShader) -> void;
			static auto SetSpotData(std::span<const SpotLight* const> spotLights, ShaderProgram& lightShader) -> void;
			static auto SetPointData(std::span<const PointLight* const> pointLights, ShaderProgram& lightShader) -> void;

			GeometryBuffer m_GBuffer;
			RenderBuffer m_RenderBuffer;

			ShaderFamily m_ShadowShader;
			ShaderFamily m_CubeShadowShader;

			ShaderFamily m_GeometryShader;
			ShaderFamily m_LightShader;

			ShaderFamily m_SkyboxShader;

			ShaderFamily m_DirLightShader;
			ShaderFamily m_SpotLightShader;
			ShaderFamily m_PointLightShader;

			CascadedShadowMap m_DirShadowMap;
			std::vector<std::unique_ptr<SpotLightShadowMap>> m_SpotShadowMaps;
			std::vector<std::unique_ptr<CubeShadowMap>> m_PointShadowMaps;

			static constexpr int STENCIL_REF{0};
			static constexpr int STENCIL_AND_MASK{1};
	};
}
