#pragma once

#include "DirLight.hpp"
#include "GlRenderer.hpp"
#include "Matrix.hpp"
#include "PointLight.hpp"
#include "SpotLight.hpp"
#include "rendering/CascadedShadowMap.hpp"
#include "rendering/RenderBuffer.hpp"
#include "rendering/ScreenQuad.hpp"
#include "rendering/TransparencyBuffer.hpp"
#include "rendering/shaders/ShaderFamily.hpp"


namespace leopph::internal
{
	class GlForwardRenderer final : public GlRenderer
	{
		public:
			GlForwardRenderer();

			auto Render() -> void override;

		private:
			auto RenderOpaque(Matrix4 const& camViewMat,
			                  Matrix4 const& camProjMat,
			                  std::vector<RenderNode> const& renderNodes,
			                  DirectionalLight const* dirLight,
			                  std::vector<SpotLight const*> const& spotLights,
			                  std::vector<PointLight const*> const& pointLights) -> void;

			auto RenderTransparent(Matrix4 const& camViewMat,
			                       Matrix4 const& camProjMat,
			                       std::vector<RenderNode> const& renderNodes,
			                       DirectionalLight const* dirLight,
			                       std::vector<SpotLight const*> const& spotLights,
			                       std::vector<PointLight const*> const& pointLights) -> void;

			auto Compose() -> void;

			auto RenderSkybox(Matrix4 const& camViewMat,
			                  Matrix4 const& camProjMat) -> void;

			ShaderFamily m_ObjectShader;
			ShaderFamily m_ShadowShader;
			ShaderFamily m_SkyboxShader;
			ShaderFamily m_TranspCompositeShader;

			CascadedShadowMap m_DirLightShadowMap;

			RenderBuffer m_RenderBuffer;
			TransparencyBuffer m_TransparencyBuffer;
			ScreenQuad m_ScreenQuad;
	};
}
