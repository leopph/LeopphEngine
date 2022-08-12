#include "Renderer.hpp"

#include "AmbientLight.hpp"
#include "Camera.hpp"
#include "Context.hpp"
#include "GlCore.hpp"
#include "Math.hpp"
#include "RenderSettings.hpp"
#include "Window.hpp"
#include "../../../include/Entity.hpp"

#include <algorithm>


namespace leopph::internal
{
	void Renderer::render()
	{
		mCameraData.clear();

		for (auto const* const camera : mCameras)
		{
			extract_camera_data(*camera, mCameraData);
		}

		if (mCameraData.empty())
		{
			// TODO Render some default image?
			return;
		}

		if (!extract())
		{
			return;
		}

		prepare();
		update_resources();

		if (mRenderingPath == RenderingPath::Forward)
		{
			forward_render();
		}
		else
		{
			deferred_render();
		}

		++mFrameCount;
	}



	u64 Renderer::create_static_mesh(StaticModelComponent const* component, std::span<StaticModelData const> const data)
	{
		static u64 id{1};

		for (auto const& [vertices, indices, material] : data)
		{
			//mIdToMeshGroup[id].meshes.emplace_back(new StaticMesh{vertices, indices});
			//mMaterialToMeshes[material.get()]
		}

		return id++;
	}



	void Renderer::register_material(StaticMaterial const* material)
	{
		mMaterialsToBuffers[material];
	}



	void Renderer::unregister_material(StaticMaterial const* material)
	{
		mMaterialsToBuffers.erase(material);
	}



	void Renderer::register_dir_light(DirectionalLight const* dirLight)
	{
		mDirLights.push_back(dirLight);
	}



	void Renderer::unregister_dir_light(DirectionalLight const* dirLight)
	{
		std::erase(mDirLights, dirLight);
	}



	void Renderer::register_spot_light(SpotLight const* spotLight)
	{
		mSpotLights.push_back(spotLight);
	}



	void Renderer::unregister_spot_light(SpotLight const* spotLight)
	{
		std::erase(mSpotLights, spotLight);
	}



	void Renderer::register_point_light(PointLight const* pointLight)
	{
		mPointLights.push_back(pointLight);
	}



	void Renderer::unregister_point_light(PointLight const* pointLight)
	{
		std::erase(mPointLights, pointLight);
	}



	void Renderer::register_camera(Camera const* camera)
	{
		mCameras.push_back(camera);
	}



	void Renderer::unregister_camera(Camera const* camera)
	{
		std::erase(mCameras, camera);
	}



	void Renderer::extract_camera_data(Camera const& camera, std::vector<CameraData>& out)
	{
		out.emplace_back(camera.get_window_extents(), camera.get_owner()->get_position(), camera.build_view_matrix(), camera.build_projection_matrix());
	}



	bool Renderer::extract()
	{
		// Extract main camera

		auto const* const cam = Camera::Current();

		// If there is no camera, abort extract and signal abort for the parent process too
		if (!cam)
		{
			return false;
		}

		mCamData.position = cam->get_owner()->get_position();
		mCamData.viewMat = cam->ViewMatrix();
		mCamData.projMat = cam->ProjectionMatrix();

		mRenderingPath = cam->get_rendering_path();


		// Extract screen data

		auto const renderMult = GetWindowImpl()->get_render_multiplier();
		auto const windowWidth = GetWindowImpl()->get_width();
		auto const windowHeight = GetWindowImpl()->get_height();
		mScreenData.renderWidth = static_cast<u32>(static_cast<f32>(windowWidth) / renderMult);
		mScreenData.renderHeight = static_cast<u32>(static_cast<f32>(windowHeight) / renderMult);
		mScreenData.width = windowWidth;
		mScreenData.height = windowHeight;
		mScreenData.gamma = GetSettingsImpl()->Gamma();


		// Extract ambient light data

		mAmbientLightData.intensity = AmbientLight::Instance().Intensity();


		// Extract directional light data

		if (auto const* const dirLight = GetDataManager()->DirectionalLight())
		{
			UboDirLight dirData;
			dirData.direction = dirLight->get_direction();
			dirData.lightBase.color = dirLight->get_color();
			dirData.lightBase.intensity = dirLight->get_intensity();
			dirData.shadow = false; // temporary
			mDirLightData = dirData;
		}
		else
		{
			mDirLightData.reset();
		}


		// Extract SpotLights

		mSpotLightData.clear();
		for (auto const* const spotLight : GetDataManager()->ActiveSpotLights())
		{
			UboSpotLight uboSpot;
			uboSpot.lightBase.color = spotLight->get_color();
			uboSpot.lightBase.intensity = spotLight->get_intensity();
			uboSpot.direction = spotLight->get_owner()->get_transform().get_forward_axis();
			uboSpot.position = spotLight->get_owner()->get_transform().get_position();
			uboSpot.range = spotLight->get_range();
			uboSpot.innerCos = math::Cos(math::ToRadians(spotLight->get_inner_angle()));
			uboSpot.outerCos = math::Cos(math::ToRadians(spotLight->get_outer_angle()));
			mSpotLightData.push_back(uboSpot);
		}

		// Extract PointLights

		mPointLightData.clear();
		for (auto const* const pointLight : GetDataManager()->ActivePointLights())
		{
			UboPointLight uboPoint;
			uboPoint.lightBase.color = pointLight->get_color();
			uboPoint.lightBase.intensity = pointLight->get_intensity();
			uboPoint.position = pointLight->get_owner()->get_transform().get_position();
			uboPoint.range = pointLight->get_range();
			mPointLightData.push_back(uboPoint);
		}

		// Extract render nodes

		/*if (mRenderNodes.size() > mStaticMeshes.size())
		{
			mRenderNodes.resize(mStaticMeshes.size());
		}

		std::size_t renderNodeInd = 0;

		for (auto const& [mesh, component] : mStaticMeshes)
		{
			auto const subMeshes = mesh->get_sub_meshes();
			auto const materials = mesh->get_materials();

			for (std::size_t subMeshInd = 0; subMeshInd < subMeshes.size(); subMeshInd++)
			{
				mRenderNodes[renderNodeInd].vao = mesh->get_vao();
				mRenderNodes[renderNodeInd].isCastingShadow = 
			}
			auto const& transform = component->Owner()->get_transform();
			auto const meshes = mesh->get_meshes();
			auto const materials = staticMeshGroup->get_materials();

			for (std::size_t i = 0; i < meshes.size() && i < materials.size(); i++)
			{
				mRenderNodes.emplace_back(meshes[i]->vao, materials[i].get(), component->is_casting_shadow(), transform.get_model_matrix(), transform.get_normal_matrix());
			}
		}

		return true;*/
	}



	void Renderer::update_resources()
	{
		// Update Ping-Pong buffers

		for (auto& buf : mPingPongBuffers)
		{
			if (buf.width != mScreenData.renderWidth || buf.height != mScreenData.renderHeight)
			{
				glDeleteFramebuffers(1, &buf.framebuffer);
				glDeleteBuffers(1, &buf.depthStencilBuffer);
				glDeleteBuffers(1, &buf.colorBuffer);

				buf.width = mScreenData.renderWidth;
				buf.height = mScreenData.renderHeight;


				glCreateTextures(GL_TEXTURE_2D, 1, &buf.colorBuffer);
				glTextureStorage2D(buf.colorBuffer, 1, GL_RGBA8, buf.width, buf.height);

				glCreateTextures(GL_TEXTURE_2D, 1, &buf.depthStencilBuffer);
				glTextureStorage2D(buf.depthStencilBuffer, 1, GL_DEPTH24_STENCIL8, buf.width, buf.height);

				glCreateFramebuffers(1, &buf.framebuffer);
				glNamedFramebufferTexture(buf.framebuffer, GL_COLOR_ATTACHMENT0, buf.colorBuffer, 0);
				glNamedFramebufferTexture(buf.framebuffer, GL_DEPTH_STENCIL_ATTACHMENT, buf.depthStencilBuffer, 0);
				glNamedFramebufferDrawBuffer(buf.framebuffer, GL_COLOR_ATTACHMENT0);
			}
		}
	}



	void Renderer::prepare()
	{
		// Calculate additional camera matrices
		mCamData.viewMatInv = mCamData.viewMat.inverse();
		mCamData.projMatInv = mCamData.projMat.inverse();
		mCamData.viewProjMat = mCamData.viewMat * mCamData.projMat;
		mCamData.viewProjMatInv = mCamData.viewProjMat.inverse();

		// Sort punctual lights by distance to camera

		auto const distToCam = [this](auto const& lightData)
		{
			return Vector3::distance(lightData.position, this->mCamData.position);
		};

		std::ranges::sort(mSpotLightData, std::ranges::less{}, distToCam);
		std::ranges::sort(mPointLightData, std::ranges::less{}, distToCam);

		// Select first N of the lights
		mSpotLightData.resize(RenderSettings::get_max_spot_light_count());
		mPointLightData.resize(RenderSettings::get_max_point_light_count());
	}



	void Renderer::submit_common_data() const
	{
		auto const uboIndex = mFrameCount % NUM_UNIFORM_BUFFERS;

		// Fill camera ubo with data
		*reinterpret_cast<UboCameraData*>(mCameraBuffers[uboIndex].mapping) = mCamData;

		// Bind camera ubo
		glBindBufferRange(GL_UNIFORM_BUFFER, 0, mCameraBuffers[uboIndex].name, 0, mCameraBuffers[uboIndex].size);

		// Fill lighting ubo with data

		auto offset{0};

		// ambient light
		*reinterpret_cast<UboAmbientLight*>(mLightingBuffers[uboIndex].mapping + offset) = mAmbientLightData;
		offset += sizeof UboAmbientLight;

		// is there a dirlight
		*reinterpret_cast<u32*>(mLightingBuffers[uboIndex].mapping + offset) = mDirLightData.has_value();
		offset += sizeof u32;

		// dirlight data
		if (mDirLightData)
		{
			*reinterpret_cast<UboDirLight*>(mLightingBuffers[uboIndex].mapping + offset) = *mDirLightData;
		}
		offset += sizeof(UboDirLight);

		// spotlight data
		for (auto const& spotData : mSpotLightData)
		{
			*reinterpret_cast<UboSpotLight*>(mLightingBuffers[uboIndex].mapping + offset) =
				spotData;
			offset += sizeof UboSpotLight;
		}

		// pointlight data
		for (auto const& pointData : mPointLightData)
		{
			*reinterpret_cast<UboPointLight*>(mLightingBuffers[uboIndex].mapping + offset) =
				pointData;
			offset += sizeof UboPointLight;
		}

		glBindBufferRange(GL_UNIFORM_BUFFER, 1, mLightingBuffers[uboIndex].name, 0, mLightingBuffers[uboIndex].size);
	}



	void Renderer::forward_render() const
	{
		// Opaque pass
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_TRUE);
		glDisable(GL_STENCIL_TEST);
		glViewport(0, 0, mScreenData.renderWidth, mScreenData.renderHeight);

		GLfloat constexpr clearColor[]{0, 0, 0, 1};
		GLfloat constexpr clearDepth{1};
		glClearNamedFramebufferfv(mPingPongBuffers[0].framebuffer, GL_COLOR, 0, clearColor);
		glClearNamedFramebufferfv(mPingPongBuffers[0].framebuffer, GL_DEPTH, 0, &clearDepth);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mPingPongBuffers[0].framebuffer);



		glBlitNamedFramebuffer(mPingPongBuffers[1].framebuffer, 0, 0, 0, mScreenData.renderWidth, mScreenData.renderHeight, 0, 0, mScreenData.width, mScreenData.height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	}



	void Renderer::deferred_render() const
	{}



	void Renderer::OnEventReceived(EventReceiver<WindowEvent>::EventParamType)
	{
		mResUpdateFlags.renderRes = true;
	}



	Renderer::Renderer()
	{
		// Create uniform buffers

		auto const camBufSz = 6 * sizeof Matrix4 + sizeof Vector3;

		for (auto& camBuf : mCameraBuffers)
		{
			glCreateBuffers(1, &camBuf.name);
			glNamedBufferStorage(camBuf.name, camBufSz, nullptr, GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
			camBuf.size = camBufSz;
			camBuf.mapping = static_cast<u8*>(glMapNamedBufferRange(camBuf.name, 0, camBufSz, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT));
		}

		auto const lightBufSz = sizeof UboAmbientLight + sizeof UboDirLight + RenderSettings::get_max_spot_light_count() * sizeof UboSpotLight + RenderSettings::get_max_point_light_count() * sizeof UboPointLight;

		for (auto& lightBuf : mLightingBuffers)
		{
			glCreateBuffers(1, &lightBuf.name);
			glNamedBufferStorage(lightBuf.name, lightBufSz, nullptr, GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
			lightBuf.size = lightBufSz;
			lightBuf.mapping = static_cast<u8*>(glMapNamedBufferRange(lightBuf.name, 0, lightBufSz, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT));
		}

		glDepthFunc(GL_LEQUAL);
		glFrontFace(GL_CCW);
		glCullFace(GL_BACK);
		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	}



	Renderer::~Renderer()
	{
		// Delete uniform buffers

		for (auto const& buffers : {mCameraBuffers, mLightingBuffers})
		{
			for (auto const& buffer : buffers)
			{
				glUnmapNamedBuffer(buffer.name);
				glDeleteBuffers(1, &buffer.name);
			}
		}

		// Delete ping-pong buffers

		for (auto const& pingPongBuf : mPingPongBuffers)
		{
			glDeleteFramebuffers(1, &pingPongBuf.framebuffer);
			glDeleteTextures(1, &pingPongBuf.colorBuffer);
			glDeleteTextures(1, &pingPongBuf.depthStencilBuffer);
		}
	}



	/*void Renderer::calculate_shadow_cascades(std::vector<ShadowCascade>& out)
	{
		auto const frust = (*mMainCam)->Frustum();
		auto const& frustNearZ = frust.NearBottomLeft[2];
		auto const& frustFarZ = frust.FarBottomLeft[2];
		auto const frustDepth = frustFarZ - frustNearZ;
		auto const numCascades = static_cast<u8>(mDirShadowRes.size());
		auto const lightViewMat = Matrix4::LookAt(Vector3{0}, (*mDirLight)->Direction(), Vector3::Up());
		auto const camViewToLightViewMat = (*mMainCam)->ViewMatrix().Inverse() * lightViewMat;

		out.resize(numCascades);

		// Calculate the cascade near and far planes on the Z axis in main camera space

		out[0].near = frustNearZ;
		for (auto i = 1; i < numCascades; i++)
		{
			out[i].near = mDirCorr * frustNearZ * math::Pow(frustFarZ / frustNearZ, static_cast<f32>(i) / static_cast<f32>(numCascades)) + (1 - mDirCorr) * (frustNearZ + static_cast<f32>(i) / static_cast<f32>(numCascades) * (frustFarZ - frustNearZ));
			// On bound borders the far plane is multiplied by this value to avoid precision problems.
			auto constexpr borderCorrection = 1.005f;
			out[i - 1].far = out[i].near * borderCorrection;
		}
		out[numCascades - 1].far = frustFarZ;

		for (auto& cascade : out)
		{
			// The normalized distance of the cascades faces from the near frustum face.

			auto const cascadeNearDist = (cascade.near - frustNearZ) / frustDepth;
			auto const cascadeFarDist = (cascade.far - frustNearZ) / frustDepth;

			// The cascade vertices in camera view space.
			std::array const cascadeVertsCam
			{
				Vector4{math::lerp(frust.NearTopLeft, frust.FarTopLeft, cascadeNearDist), 1.f},
				Vector4{math::lerp(frust.NearBottomLeft, frust.FarBottomLeft, cascadeNearDist), 1.f},
				Vector4{math::lerp(frust.NearBottomRight, frust.FarBottomRight, cascadeNearDist), 1.f},
				Vector4{math::lerp(frust.NearTopRight, frust.FarTopRight, cascadeNearDist), 1.f},
				Vector4{math::lerp(frust.NearTopLeft, frust.FarTopLeft, cascadeFarDist), 1.f},
				Vector4{math::lerp(frust.NearBottomLeft, frust.FarBottomLeft, cascadeFarDist), 1.f},
				Vector4{math::lerp(frust.NearBottomRight, frust.FarBottomRight, cascadeFarDist), 1.f},
				Vector4{math::lerp(frust.NearTopRight, frust.FarTopRight, cascadeFarDist), 1.f},
			};

			// The light view space mininum point of the bounding box of the cascade
			Vector3 bBoxMinLight{std::numeric_limits<float>::max()};

			// The light view space maximum point of the bounding box of the cascade
			Vector3 bBoxMaxLight{std::numeric_limits<float>::min()};

			// Calculate the bounding box min and max points by transforming the vertices to light space.
			std::ranges::for_each(cascadeVertsCam, [&](auto const& vertex)
			{
				auto const vertLight = vertex * camViewToLightViewMat;
				bBoxMinLight = Vector3{std::min(bBoxMinLight[0], vertLight[0]), std::min(bBoxMinLight[1], vertLight[1]), std::min(bBoxMinLight[2], vertLight[2])};
				bBoxMaxLight = Vector3{std::max(bBoxMaxLight[0], vertLight[0]), std::max(bBoxMaxLight[1], vertLight[1]), std::max(bBoxMaxLight[2], vertLight[2])};
			});

			// The projection matrix that uses the calculated min/max values of the bounding box. Essentially THE bounding box + the near clip offset of the DirectionalLight.
			auto const lightProjMat{Matrix4::Ortographic(bBoxMinLight[0], bBoxMaxLight[0], bBoxMaxLight[1], bBoxMinLight[1], bBoxMinLight[2] - (*mDirLight)->ShadowExtension(), bBoxMaxLight[2])};

			cascade.wordToClip = lightViewMat * lightProjMat;
		}
	}*/
}
