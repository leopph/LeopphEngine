#pragma once

#include "DirLight.hpp"
#include "GlRenderer.hpp"
#include "Matrix.hpp"
#include "PointLight.hpp"
#include "SpotLight.hpp"
#include "rendering/CascadedShadowMap.hpp"
#include "rendering/CubeShadowMap.hpp"
#include "rendering/GeometryBuffer.hpp"
#include "rendering/RenderBuffer.hpp"
#include "rendering/ScreenQuad.hpp"
#include "rendering/SpotShadowMap.hpp"
#include "rendering/TransparencyBuffer.hpp"
#include "rendering/opengl/OpenGl.hpp"
#include "rendering/shaders/ShaderFamily.hpp"

#include <cstddef>
#include <memory>
#include <vector>


namespace leopph::internal
{
	class GlDeferredRenderer final : public GlRenderer
	{
		public:
			GlDeferredRenderer();

			auto Render() -> void override;

		private:
			// Fill the GeometryBuffer with geometry data.
			auto RenderGeometry(Matrix4 const& camViewMat, Matrix4 const& camProjMat, std::vector<RenderNode> const& renderables) -> void;

			// Draw all lights in the RenderBuffer.
			auto RenderLights(Matrix4 const& camViewMat,
			                  Matrix4 const& camProjMat,
			                  std::span<RenderNode const> renderNodes,
			                  std::span<SpotLight const*> spotLights,
			                  std::span<PointLight const*> pointLights) -> void;

			// Draw skybox in the empty parts of the RenderBuffer.
			auto RenderSkybox(Matrix4 const& camViewMat, Matrix4 const& camProjMat) -> void;

			// Draws into the dirlight shadow map, binds it to light shader with the necessary data, and returns the next usable texture unit.
			[[nodiscard]] auto RenderDirShadowMap(DirectionalLight const* dirLight,
			                                      Matrix4 const& camViewInvMat,
			                                      Matrix4 const& camProjMat,
			                                      std::span<RenderNode const> renderNodes,
			                                      ShaderProgram& lightShader,
			                                      ShaderProgram& shadowShader,
			                                      GLuint nextTexUnit) const -> GLuint;

			// Draws into the first N shadow maps, binds them to the light shader with the necessary data, and returns the next usable texture unit.
			[[nodiscard]] auto RenderSpotShadowMaps(std::span<SpotLight const* const> spotLights,
			                                        std::span<RenderNode const> renderNodes,
			                                        ShaderProgram& lightShader,
			                                        ShaderProgram& shadowShader,
			                                        std::size_t numShadows,
			                                        GLuint nextTexUnit) -> GLuint;

			// Draws into the first N shadow maps, binds them to the light shader with the necessary data, and returns the next usable texture unit.
			[[nodiscard]] auto RenderPointShadowMaps(std::span<PointLight const* const> pointLights,
			                                         std::span<RenderNode const> renderNodes,
			                                         ShaderProgram& lightShader,
			                                         ShaderProgram& shadowShader,
			                                         std::size_t numShadows,
			                                         GLuint nextTexUnit) -> GLuint;

			// Transparent forward pass.
			auto RenderTransparent(Matrix4 const& camViewMat,
			                       Matrix4 const& camProjMat,
			                       std::vector<RenderNode> const& renderNodes,
			                       DirectionalLight const* dirLight,
			                       std::vector<SpotLight const*> const& spotLights,
			                       std::vector<PointLight const*> const& pointLights) -> void;

			GeometryBuffer m_GBuffer;

			RenderBuffer m_RenderBuffer;
			ScreenQuad m_ScreenQuad;
			TransparencyBuffer m_TransparencyBuffer;

			ShaderFamily m_ShadowShader;
			ShaderFamily m_CubeShadowShader;

			ShaderFamily m_GeometryShader;
			ShaderFamily m_LightShader;
			ShaderFamily m_SkyboxShader;

			ShaderFamily m_ForwardShader;
			ShaderFamily m_CompositeShader;

			CascadedShadowMap m_DirShadowMap;
			std::vector<std::unique_ptr<SpotShadowMap>> m_SpotShadowMaps;
			std::vector<std::unique_ptr<CubeShadowMap>> m_PointShadowMaps;

			static constexpr int STENCIL_DRAW_TAG{0};
	};
}
