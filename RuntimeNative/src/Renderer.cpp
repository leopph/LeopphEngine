#include "Renderer.hpp"

#include <dxgi1_6.h>

#include "Platform.hpp"
#include "Entity.hpp"
#include "Systems.hpp"
#include "Util.hpp"
#include "TransformComponent.hpp"
#include "Mesh.hpp"

#ifndef NDEBUG
#include "shaders/generated/MeshPbrPSBinDebug.h"
#include "shaders/generated/MeshVSBinDebug.h"
#include "shaders/generated/ToneMapGammaPSBinDebug.h"
#include "shaders/generated/SkyboxPSBinDebug.h"
#include "shaders/generated/SkyboxVSBinDebug.h"
#include "shaders/generated/ShadowVSBinDebug.h"
#include "shaders/generated/ScreenVSBinDebug.h"
#include "shaders/generated/GizmoPSBinDebug.h"
#include "shaders/generated/LineGizmoVSBinDebug.h"

#else
#include "shaders/generated/MeshPbrPSBin.h"
#include "shaders/generated/MeshVSBin.h"
#include "shaders/generated/ToneMapGammaPSBin.h"
#include "shaders/generated/SkyboxPSBin.h"
#include "shaders/generated/SkyboxVSBin.h"
#include "shaders/generated/ShadowVSBin.h"
#include "shaders/generated/ScreenVSBin.h"
#include "shaders/generated/GizmoPSBin.h"
#include "shaders/generated/LineGizmoVSBin.h"
#endif

#include "shaders/ShaderInterop.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <memory>
#include <tuple>

using Microsoft::WRL::ComPtr;


namespace leopph::renderer {
namespace {
// Normalized to [0, 1]
std::array<float, MAX_CASCADE_COUNT - 1> gCascadeSplits{ 0.1f, 0.3f, 0.6f };
int gCascadeCount{ 4 };
float gShadowDistance{ 100 };
bool gVisualizeShadowCascades{ false };
bool gUseStableShadowCascadeProjection{ false };
ShadowFilteringMode gShadowFilteringMode{ ShadowFilteringMode::PCFTent5x5 };


struct ShadowCascadeBoundary {
	float nearClip;
	float farClip;
};


using ShadowCascadeBoundaries = std::array<ShadowCascadeBoundary, MAX_CASCADE_COUNT>;


[[nodiscard]] auto CalculateCameraShadowCascadeBoundaries(Camera const& cam) -> ShadowCascadeBoundaries {
	auto const camNear{ cam.GetNearClipPlane() };
	auto const shadowDistance{ std::min(cam.GetFarClipPlane(), gShadowDistance) };
	auto const shadowedFrustumDepth{ shadowDistance - camNear };

	ShadowCascadeBoundaries boundaries;

	boundaries[0].nearClip = camNear;

	for (int i = 0; i < gCascadeCount - 1; i++) {
		boundaries[i + 1].nearClip = camNear + gCascadeSplits[i] * shadowedFrustumDepth;
		boundaries[i].farClip = boundaries[i + 1].nearClip * 1.005f;
	}

	boundaries[gCascadeCount - 1].farClip = shadowDistance;

	for (int i = gCascadeCount; i < MAX_CASCADE_COUNT; i++) {
		boundaries[i].nearClip = std::numeric_limits<float>::infinity();
		boundaries[i].farClip = std::numeric_limits<float>::infinity();
	}

	return boundaries;
}


struct ShadowAtlasSubcellData {
	Matrix4 shadowViewProjMtx;
	// Index into the array of indices to the visible lights, use lights[visibleLights[visibleLightIdxIdx]] to get to the light
	int visibleLightIdxIdx;
	int shadowMapIdx;
};


class GridLike {
	int mSubdivSize;


	auto ThrowIfSubdivInvalid() const {
		if (!IsPowerOfTwo(mSubdivSize)) {
			throw std::runtime_error{ "GridLike subdivision size must be power of 2." };
		}
	}

protected:
	auto ThrowIfIndexIsInvalid(int const idx) const -> void {
		if (idx < 0 || idx >= GetElementCount()) {
			throw std::runtime_error{ "Invalid GridLike element index." };
		}
	}


	auto SetSubdivisionSize(int const subdivSize) -> void {
		mSubdivSize = subdivSize;
		ThrowIfSubdivInvalid();
	}

public:
	explicit GridLike(int const subdivSize) :
		mSubdivSize{ subdivSize } {
		ThrowIfSubdivInvalid();
	}


	// The grid has N*N cells, this is the N of that.
	[[nodiscard]] auto GetSubdivisionSize() const noexcept -> int {
		return mSubdivSize;
	}


	[[nodiscard]] auto GetElementCount() const noexcept -> int {
		return mSubdivSize * mSubdivSize;
	}


	[[nodiscard]] auto GetNormalizedElementSize() const noexcept -> float {
		return 1.0f / static_cast<float>(mSubdivSize);
	}


	[[nodiscard]] auto GetNormalizedElementOffset(int const idx) const -> Vector2 {
		ThrowIfIndexIsInvalid(idx);

		Vector2 offset{ GetNormalizedElementSize() };
		offset[0] *= static_cast<float>(static_cast<int>(idx % mSubdivSize));
		offset[1] *= static_cast<float>(static_cast<int>(idx / mSubdivSize));

		return offset;
	}
};


#pragma warning(push)
#pragma warning(disable: 4324)
class ShadowAtlasCell : public GridLike {
	std::vector<std::optional<ShadowAtlasSubcellData>> mSubcells;

public:
	explicit ShadowAtlasCell(int const subdivSize) :
		GridLike{ subdivSize } {
		mSubcells.resize(GetElementCount());
	}


	[[nodiscard]] auto GetSubcell(int const idx) const -> std::optional<ShadowAtlasSubcellData> const& {
		ThrowIfIndexIsInvalid(idx);
		return mSubcells[idx];
	}


	[[nodiscard]] auto GetSubcell(int const idx) -> std::optional<ShadowAtlasSubcellData>& {
		return const_cast<std::optional<ShadowAtlasSubcellData>&>(const_cast<ShadowAtlasCell const*>(this)->GetSubcell(idx));
	}


	auto Resize(int const subdivSize) -> void {
		SetSubdivisionSize(subdivSize);
		mSubcells.resize(GetElementCount());
	}
};


class ShadowAtlas : public GridLike {
protected:
	ComPtr<ID3D11Texture2D> mTex;
	ComPtr<ID3D11ShaderResourceView> mSrv;
	ComPtr<ID3D11DepthStencilView> mDsv;
	int mSize;


	ShadowAtlas(ID3D11Device* const device, int const size, int const subdivSize) :
		GridLike{ subdivSize },
		mSize{ size } {
		if (!IsPowerOfTwo(mSize)) {
			throw std::runtime_error{ "Shadow Atlas size must be power of 2." };
		}

		D3D11_TEXTURE2D_DESC const texDesc{
			.Width = static_cast<UINT>(size),
			.Height = static_cast<UINT>(size),
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R32_TYPELESS,
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};

		if (FAILED(device->CreateTexture2D(&texDesc, nullptr, mTex.GetAddressOf()))) {
			throw std::runtime_error{ "Failed to create Shadow Atlas texture." };
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC constexpr srvDesc{
			.Format = DXGI_FORMAT_R32_FLOAT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};

		if (FAILED(device->CreateShaderResourceView(mTex.Get(), &srvDesc, mSrv.GetAddressOf()))) {
			throw std::runtime_error{ "Failed to create Shadow Atlas SRV." };
		}

		D3D11_DEPTH_STENCIL_VIEW_DESC constexpr dsvDesc{
			.Format = DXGI_FORMAT_D32_FLOAT,
			.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D,
			.Flags = 0,
			.Texture2D = { .MipSlice = 0 }
		};

		if (FAILED(device->CreateDepthStencilView(mTex.Get(), &dsvDesc, mDsv.GetAddressOf()))) {
			throw std::runtime_error{ "Failed to create Shadow Atlas DSV." };
		}
	}

public:
	ShadowAtlas(ShadowAtlas const&) = delete;
	ShadowAtlas(ShadowAtlas&&) = delete;

	virtual ~ShadowAtlas() = default;

	auto operator=(ShadowAtlas const&) -> void = delete;
	auto operator=(ShadowAtlas&&) -> void = delete;


	[[nodiscard]] auto GetDsv() const noexcept -> ID3D11DepthStencilView* {
		return mDsv.Get();
	}


	[[nodiscard]] auto GetSrv() const noexcept -> ID3D11ShaderResourceView* {
		return mSrv.Get();
	}


	[[nodiscard]] auto GetSize() const noexcept -> int {
		return mSize;
	}


	auto SetLookUpInfo(std::span<ShaderLight> lights) const -> void {
		for (int i = 0; i < GetElementCount(); i++) {
			auto const& cell{ GetCell(i) };

			for (int j = 0; j < cell.GetElementCount(); j++) {
				if (auto const& subcell{ cell.GetSubcell(j) }) {
					lights[subcell->visibleLightIdxIdx].isCastingShadow = TRUE;
					lights[subcell->visibleLightIdxIdx].sampleShadowMap[subcell->shadowMapIdx] = TRUE;
					lights[subcell->visibleLightIdxIdx].shadowViewProjMatrices[subcell->shadowMapIdx] = subcell->shadowViewProjMtx;
					lights[subcell->visibleLightIdxIdx].shadowAtlasCellOffsets[subcell->shadowMapIdx] = GetNormalizedElementOffset(i) + cell.GetNormalizedElementOffset(j) * GetNormalizedElementSize();
					lights[subcell->visibleLightIdxIdx].shadowAtlasCellSizes[subcell->shadowMapIdx] = GetNormalizedElementSize() * cell.GetNormalizedElementSize();
				}
			}
		}
	}


	[[nodiscard]] virtual auto GetCell(int idx) const -> ShadowAtlasCell const& = 0;

	virtual auto Update(std::span<LightComponent const* const> allLights, Visibility const& visibility, Camera const& cam, Matrix4 const& camViewProjMtx, ShadowCascadeBoundaries const& shadowCascadeBoundaries, float aspectRatio) -> void = 0;
};
#pragma warning(pop)


class PunctualShadowAtlas final : public ShadowAtlas {
	std::array<ShadowAtlasCell, 4> mCells;

public:
	PunctualShadowAtlas(ID3D11Device* const device, int const size) :
		ShadowAtlas{ device, size, 2 },
		mCells{ ShadowAtlasCell{ 1 }, ShadowAtlasCell{ 2 }, ShadowAtlasCell{ 4 }, ShadowAtlasCell{ 8 } } {}


	auto Update(std::span<LightComponent const* const> const allLights, Visibility const& visibility, Camera const& cam, Matrix4 const& camViewProjMtx, [[maybe_unused]] ShadowCascadeBoundaries const& shadowCascadeBoundaries, [[maybe_unused]] float const aspectRatio) -> void override {
		struct LightCascadeIndex {
			int lightIdxIdx;
			int shadowIdx;
		};

		std::array<std::vector<LightCascadeIndex>, 4> static lightIndexIndicesInCell{};

		for (auto& cellLight : lightIndexIndicesInCell) {
			cellLight.clear();
		}

		auto const& camPos{ cam.GetPosition() };

		auto const determineScreenCoverage{
			[&camPos, &camViewProjMtx](std::span<Vector3 const> const vertices) -> std::optional<int> {
				std::optional<int> cellIdx;

				if (auto const [worldMin, worldMax]{ AABB::FromVertices(vertices) };
					worldMin[0] <= camPos[0] && worldMin[1] <= camPos[1] && worldMin[2] <= camPos[2] &&
					worldMax[0] >= camPos[0] && worldMax[1] >= camPos[1] && worldMax[2] >= camPos[2]) {
					cellIdx = 0;
				}
				else {
					Vector2 const bottomLeft{ -1, -1 };
					Vector2 const topRight{ 1, 1 };

					Vector2 min{ std::numeric_limits<float>::max() };
					Vector2 max{ std::numeric_limits<float>::lowest() };

					for (auto& vertex : vertices) {
						Vector4 vertex4{ vertex, 1 };
						vertex4 *= camViewProjMtx;
						auto const projected{ Vector2{ vertex4 } / vertex4[3] };
						min = Clamp(Min(min, projected), bottomLeft, topRight);
						max = Clamp(Max(max, projected), bottomLeft, topRight);
					}

					auto const width{ max[0] - min[0] };
					auto const height{ max[1] - min[1] };

					auto const area{ width * height };
					auto const coverage{ area / 4 };

					if (coverage >= 1) {
						cellIdx = 0;
					}
					else if (coverage >= 0.25f) {
						cellIdx = 1;
					}
					else if (coverage >= 0.0625f) {
						cellIdx = 2;
					}
					else if (coverage >= 0.015625f) {
						cellIdx = 3;
					}
				}

				return cellIdx;
			}
		};

		for (int i = 0; i < static_cast<int>(visibility.lightIndices.size()); i++) {
			if (auto const light{ allLights[visibility.lightIndices[i]] }; light->IsCastingShadow() && light->GetType() == LightComponent::Type::Spot || light->GetType() == LightComponent::Type::Point) {
				Vector3 const& lightPos{ light->GetEntity()->GetTransform().GetWorldPosition() };
				float const lightRange{ light->GetRange() };

				// Skip the light if its bounding sphere is farther than the shadow distance
				if (Vector3 const camToLightDir{ Normalize(lightPos - camPos) }; Distance(lightPos - camToLightDir * lightRange, cam.GetPosition()) > gShadowDistance) {
					continue;
				}

				if (light->GetType() == LightComponent::Type::Spot) {
					auto lightVertices{ CalculateSpotLightLocalVertices(*light) };

					for (auto const modelMtxNoScale{ CalculateModelMatrixNoScale(light->GetEntity()->GetTransform()) };
					     auto& vertex : lightVertices) {
						vertex = Vector3{ Vector4{ vertex, 1 } * modelMtxNoScale };
					}

					if (auto const cellIdx{ determineScreenCoverage(lightVertices) }) {
						lightIndexIndicesInCell[*cellIdx].emplace_back(i, 0);
					}
				}
				else if (light->GetType() == LightComponent::Type::Point) {
					for (auto j = 0; j < 6; j++) {
						std::array static const faceBoundsRotations{
							Quaternion::FromAxisAngle(Vector3::Up(), ToRadians(90)), // +X
							Quaternion::FromAxisAngle(Vector3::Up(), ToRadians(-90)), // -X
							Quaternion::FromAxisAngle(Vector3::Right(), ToRadians(-90)), // +Y
							Quaternion::FromAxisAngle(Vector3::Right(), ToRadians(90)), // -Y
							Quaternion{}, // +Z
							Quaternion::FromAxisAngle(Vector3::Up(), ToRadians(180)) // -Z
						};

						std::array const shadowFrustumVertices{
							faceBoundsRotations[i].Rotate(Vector3{ lightRange, lightRange, lightRange }) + lightPos,
							faceBoundsRotations[i].Rotate(Vector3{ -lightRange, lightRange, lightRange }) + lightPos,
							faceBoundsRotations[i].Rotate(Vector3{ -lightRange, -lightRange, lightRange }) + lightPos,
							faceBoundsRotations[i].Rotate(Vector3{ lightRange, -lightRange, lightRange }) + lightPos,
							lightPos,
						};

						if (auto const cellIdx{ determineScreenCoverage(shadowFrustumVertices) }) {
							lightIndexIndicesInCell[*cellIdx].emplace_back(i, j);
						}
					}
				}
			}
		}

		for (int i = 0; i < 4; i++) {
			std::ranges::sort(lightIndexIndicesInCell[i], [&visibility, &camPos, &allLights](LightCascadeIndex const lhs, LightCascadeIndex const rhs) {
				auto const leftLight{ allLights[visibility.lightIndices[lhs.lightIdxIdx]] };
				auto const rightLight{ allLights[visibility.lightIndices[rhs.lightIdxIdx]] };

				auto const leftLightPos{ leftLight->GetEntity()->GetTransform().GetWorldPosition() };
				auto const rightLightPos{ rightLight->GetEntity()->GetTransform().GetWorldPosition() };

				auto const leftDist{ Distance(leftLightPos, camPos) };
				auto const rightDist{ Distance(camPos, rightLightPos) };

				return leftDist > rightDist;
			});

			for (int j = 0; j < mCells[i].GetElementCount(); j++) {
				auto& subcell{ mCells[i].GetSubcell(j) };
				subcell.reset();

				if (lightIndexIndicesInCell[i].empty()) {
					continue;
				}

				auto const [lightIdxIdx, shadowIdx]{ lightIndexIndicesInCell[i].back() };
				auto const light{ allLights[visibility.lightIndices[lightIdxIdx]] };
				lightIndexIndicesInCell[i].pop_back();

				if (light->GetType() == LightComponent::Type::Spot) {
					auto const shadowViewMtx{ Matrix4::LookToLH(light->GetEntity()->GetTransform().GetWorldPosition(), light->GetEntity()->GetTransform().GetForwardAxis(), Vector3::Up()) };
					auto const shadowProjMtx{ Matrix4::PerspectiveAsymZLH(ToRadians(light->GetOuterAngle()), 1.f, light->GetRange(), light->GetShadowNearPlane()) };

					subcell.emplace(shadowViewMtx * shadowProjMtx, lightIdxIdx, shadowIdx);
				}
				else if (light->GetType() == LightComponent::Type::Point) {
					auto const lightPos{ light->GetEntity()->GetTransform().GetWorldPosition() };

					std::array const faceViewMatrices{
						Matrix4::LookToLH(lightPos, Vector3::Right(), Vector3::Up()), // +X
						Matrix4::LookToLH(lightPos, Vector3::Left(), Vector3::Up()), // -X
						Matrix4::LookToLH(lightPos, Vector3::Up(), Vector3::Backward()), // +Y
						Matrix4::LookToLH(lightPos, Vector3::Down(), Vector3::Forward()), // -Y
						Matrix4::LookToLH(lightPos, Vector3::Forward(), Vector3::Up()), // +Z
						Matrix4::LookToLH(lightPos, Vector3::Backward(), Vector3::Up()), // -Z
					};

					auto const shadowViewMtx{ faceViewMatrices[shadowIdx] };
					auto const shadowProjMtx{ Matrix4::PerspectiveAsymZLH(ToRadians(90), 1, light->GetRange(), light->GetShadowNearPlane()) };

					subcell.emplace(shadowViewMtx * shadowProjMtx, lightIdxIdx, shadowIdx);
				}
			}

			if (i + 1 < 4) {
				std::ranges::copy(lightIndexIndicesInCell[i], std::back_inserter(lightIndexIndicesInCell[i + 1]));
			}
		}
	}


	[[nodiscard]] auto GetCell(int const idx) const -> ShadowAtlasCell const& override {
		ThrowIfIndexIsInvalid(idx);
		return mCells[idx];
	}
};


class DirectionalShadowAtlas final : public ShadowAtlas {
	ShadowAtlasCell mCell;

public:
	explicit DirectionalShadowAtlas(ID3D11Device* const device, int const size) :
		ShadowAtlas{ device, size, 1 },
		mCell{ 1 } {}


	auto Update(std::span<LightComponent const* const> const allLights, Visibility const& visibility, Camera const& cam, [[maybe_unused]] Matrix4 const& camViewProjMtx, ShadowCascadeBoundaries const& shadowCascadeBoundaries, float const aspectRatio) -> void override {
		std::vector<int> static candidateLightIdxIndices;
		candidateLightIdxIndices.clear();

		for (int i = 0; i < std::ssize(visibility.lightIndices); i++) {
			if (auto const& light{ allLights[visibility.lightIndices[i]] }; light->IsCastingShadow() && light->GetType() == LightComponent::Type::Directional) {
				candidateLightIdxIndices.emplace_back(i);
			}
		}

		auto newCellSubdiv{ 1 };

		while (newCellSubdiv * newCellSubdiv < std::ssize(candidateLightIdxIndices) * gCascadeCount) {
			newCellSubdiv = NextPowerOfTwo(newCellSubdiv);
		}

		mCell.Resize(newCellSubdiv);

		for (int i = 0; i < mCell.GetElementCount(); i++) {
			mCell.GetSubcell(i).reset();
		}

		float const camNear{ cam.GetNearClipPlane() };
		float const camFar{ cam.GetFarClipPlane() };

		// Order of vertices is CCW from top right, near first
		auto const frustumVertsWS{
			[&cam, aspectRatio, camNear, camFar] {
				std::array<Vector3, 8> ret;

				Vector3 const nearWorldForward{ cam.GetPosition() + cam.GetForwardAxis() * camNear };
				Vector3 const farWorldForward{ cam.GetPosition() + cam.GetForwardAxis() * camFar };

				switch (cam.GetType()) {
				case Camera::Type::Perspective: {
					float const tanHalfFov{ std::tan(ToRadians(cam.GetHorizontalPerspectiveFov() / 2.0f)) };
					float const nearExtentX{ camNear * tanHalfFov };
					float const nearExtentY{ nearExtentX / aspectRatio };
					float const farExtentX{ camFar * tanHalfFov };
					float const farExtentY{ farExtentX / aspectRatio };

					ret[0] = nearWorldForward + cam.GetRightAxis() * nearExtentX + cam.GetUpAxis() * nearExtentY;
					ret[1] = nearWorldForward - cam.GetRightAxis() * nearExtentX + cam.GetUpAxis() * nearExtentY;
					ret[2] = nearWorldForward - cam.GetRightAxis() * nearExtentX - cam.GetUpAxis() * nearExtentY;
					ret[3] = nearWorldForward + cam.GetRightAxis() * nearExtentX - cam.GetUpAxis() * nearExtentY;
					ret[4] = farWorldForward + cam.GetRightAxis() * farExtentX + cam.GetUpAxis() * farExtentY;
					ret[5] = farWorldForward - cam.GetRightAxis() * farExtentX + cam.GetUpAxis() * farExtentY;
					ret[6] = farWorldForward - cam.GetRightAxis() * farExtentX - cam.GetUpAxis() * farExtentY;
					ret[7] = farWorldForward + cam.GetRightAxis() * farExtentX - cam.GetUpAxis() * farExtentY;
					break;
				}
				case Camera::Type::Orthographic: {
					float const extentX{ cam.GetHorizontalOrthographicSize() / 2.0f };
					float const extentY{ extentX / aspectRatio };

					ret[0] = nearWorldForward + cam.GetRightAxis() * extentX + cam.GetUpAxis() * extentY;
					ret[1] = nearWorldForward - cam.GetRightAxis() * extentX + cam.GetUpAxis() * extentY;
					ret[2] = nearWorldForward - cam.GetRightAxis() * extentX - cam.GetUpAxis() * extentY;
					ret[3] = nearWorldForward + cam.GetRightAxis() * extentX - cam.GetUpAxis() * extentY;
					ret[4] = farWorldForward + cam.GetRightAxis() * extentX + cam.GetUpAxis() * extentY;
					ret[5] = farWorldForward - cam.GetRightAxis() * extentX + cam.GetUpAxis() * extentY;
					ret[6] = farWorldForward - cam.GetRightAxis() * extentX - cam.GetUpAxis() * extentY;
					ret[7] = farWorldForward + cam.GetRightAxis() * extentX - cam.GetUpAxis() * extentY;
					break;
				}
				}

				return ret;
			}()
		};

		auto const frustumDepth{ camFar - camNear };

		for (auto i = 0; i < std::ssize(candidateLightIdxIndices); i++) {
			auto const lightIdxIdx{ candidateLightIdxIndices[i] };
			auto const& light{ *allLights[visibility.lightIndices[lightIdxIdx]] };

			for (auto cascadeIdx = 0; cascadeIdx < gCascadeCount; cascadeIdx++) {
				// cascade vertices in world space
				auto const cascadeVertsWS{
					[&frustumVertsWS, &shadowCascadeBoundaries, cascadeIdx, camNear, frustumDepth] {
						auto const [cascadeNear, cascadeFar]{ shadowCascadeBoundaries[cascadeIdx] };

						float const cascadeNearNorm{ (cascadeNear - camNear) / frustumDepth };
						float const cascadeFarNorm{ (cascadeFar - camNear) / frustumDepth };

						std::array<Vector3, 8> ret;

						for (int j = 0; j < 4; j++) {
							Vector3 const& from{ frustumVertsWS[j] };
							Vector3 const& to{ frustumVertsWS[j + 4] };

							ret[j] = Lerp(from, to, cascadeNearNorm);
							ret[j + 4] = Lerp(from, to, cascadeFarNorm);
						}

						return ret;
					}()
				};

				auto const calculateTightViewProj{
					[&cascadeVertsWS, &light] {
						Matrix4 const shadowViewMtx{ Matrix4::LookToLH(Vector3::Zero(), light.GetDirection(), Vector3::Up()) };

						// cascade vertices in shadow space
						auto const cascadeVertsSP{
							[&cascadeVertsWS, &shadowViewMtx] {
								std::array<Vector3, 8> ret;

								for (int j = 0; j < 8; j++) {
									ret[j] = Vector3{ Vector4{ cascadeVertsWS[j], 1 } * shadowViewMtx };
								}

								return ret;
							}()
						};

						auto const [aabbMin, aabbMax]{ AABB::FromVertices(cascadeVertsSP) };
						Matrix4 const shadowProjMtx{ Matrix4::OrthographicAsymZLH(aabbMin[0], aabbMax[0], aabbMax[1], aabbMin[1], aabbMax[2], aabbMin[2] - light.GetShadowExtension()) };

						return shadowViewMtx * shadowProjMtx;
					}
				};

				auto const calculateStableViewProj{
					[&cascadeVertsWS, &light, this] {
						Vector3 cascadeCenterWS{ Vector3::Zero() };

						for (Vector3 const& cascadeVertWS : cascadeVertsWS) {
							cascadeCenterWS += cascadeVertWS;
						}

						cascadeCenterWS /= 8.0f;

						float sphereRadius{ 0.0f };

						for (Vector3 const& cascadeVertWS : cascadeVertsWS) {
							sphereRadius = std::max(sphereRadius, Distance(cascadeCenterWS, cascadeVertWS));
						}

						float const shadowMapSize{ static_cast<float>(static_cast<int>(GetSize() / GetSubdivisionSize())) };
						float const texelsPerUnit{ shadowMapSize / (sphereRadius * 2.0f) };

						Matrix4 const lookAtMtx{ Matrix4::LookToLH(Vector3::Zero(), light.GetDirection(), Vector3::Up()) };
						Matrix4 const scaleMtx{ Matrix4::Scale(Vector3{ texelsPerUnit }) };
						Matrix4 const baseViewMtx{ scaleMtx * lookAtMtx };
						Matrix4 const baseViewInvMtx{ baseViewMtx.Inverse() };

						Vector3 correctedCascadeCenter{ Vector4{ cascadeCenterWS, 1 } * baseViewMtx };

						for (int j = 0; j < 2; j++) {
							correctedCascadeCenter[j] = std::floor(correctedCascadeCenter[j]);
						}

						correctedCascadeCenter = Vector3{ Vector4{ correctedCascadeCenter, 1 } * baseViewInvMtx };

						Matrix4 const shadowViewMtx{ Matrix4::LookToLH(correctedCascadeCenter, light.GetDirection(), Vector3::Up()) };
						Matrix4 const shadowProjMtx{ Matrix4::OrthographicAsymZLH(-sphereRadius, sphereRadius, sphereRadius, -sphereRadius, sphereRadius, -sphereRadius - light.GetShadowExtension()) };

						return shadowViewMtx * shadowProjMtx;
					}
				};

				auto const shadowViewProjMtx{ gUseStableShadowCascadeProjection ? calculateStableViewProj() : calculateTightViewProj() };

				mCell.GetSubcell(i * mCell.GetSubdivisionSize() + cascadeIdx).emplace(shadowViewProjMtx, lightIdxIdx, cascadeIdx);
			}
		}
	}


	[[nodiscard]] auto GetCell([[maybe_unused]] int const idx) const -> ShadowAtlasCell const& override {
		ThrowIfIndexIsInvalid(idx);
		return mCell;
	}
};


class RenderTarget {
	ComPtr<ID3D11Device> mDevice;

	ComPtr<ID3D11Texture2D> mHdrTex;
	ComPtr<ID3D11RenderTargetView> mHdrRtv;
	ComPtr<ID3D11ShaderResourceView> mHdrSrv;

	ComPtr<ID3D11Texture2D> mOutTex;
	ComPtr<ID3D11RenderTargetView> mOutRtv;
	ComPtr<ID3D11ShaderResourceView> mOutSrv;

	ComPtr<ID3D11Texture2D> mDepthTex;
	ComPtr<ID3D11DepthStencilView> mDsv;

	UINT mWidth;
	UINT mHeight;


	auto Recreate() -> void {
		D3D11_TEXTURE2D_DESC const hdrTexDesc{
			.Width = mWidth,
			.Height = mHeight,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};

		if (FAILED(mDevice->CreateTexture2D(&hdrTexDesc, nullptr, mHdrTex.ReleaseAndGetAddressOf()))) {
			throw std::runtime_error{ "Failed to recreate Render Target HDR texture." };
		}

		D3D11_RENDER_TARGET_VIEW_DESC const hdrRtvDesc{
			.Format = hdrTexDesc.Format,
			.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
			.Texture2D
			{
				.MipSlice = 0
			}
		};

		if (FAILED(mDevice->CreateRenderTargetView(mHdrTex.Get(), &hdrRtvDesc, mHdrRtv.ReleaseAndGetAddressOf()))) {
			throw std::runtime_error{ "Failed to recreate Render Target HDR RTV." };
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC const hdrSrvDesc{
			.Format = hdrTexDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D =
			{
				.MostDetailedMip = 0,
				.MipLevels = 1
			}
		};

		if (FAILED(mDevice->CreateShaderResourceView(mHdrTex.Get(), &hdrSrvDesc, mHdrSrv.ReleaseAndGetAddressOf()))) {
			throw std::runtime_error{ "Failed to recreate Render Target HDR SRV." };
		}

		D3D11_TEXTURE2D_DESC const outputTexDesc{
			.Width = mWidth,
			.Height = mHeight,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.SampleDesc
			{
				.Count = 1,
				.Quality = 0
			},
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};

		if (FAILED(mDevice->CreateTexture2D(&outputTexDesc, nullptr, mOutTex.ReleaseAndGetAddressOf()))) {
			throw std::runtime_error{ "Failed to recreate Render Target output texture." };
		}

		D3D11_RENDER_TARGET_VIEW_DESC const outputRtvDesc{
			.Format = outputTexDesc.Format,
			.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
			.Texture2D
			{
				.MipSlice = 0
			}
		};

		if (FAILED(mDevice->CreateRenderTargetView(mOutTex.Get(), &outputRtvDesc, mOutRtv.ReleaseAndGetAddressOf()))) {
			throw std::runtime_error{ "Failed to recreate Render Target output RTV." };
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC const outputSrvDesc{
			.Format = outputTexDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D =
			{
				.MostDetailedMip = 0,
				.MipLevels = 1
			}
		};

		if (FAILED(mDevice->CreateShaderResourceView(mOutTex.Get(), &outputSrvDesc, mOutSrv.ReleaseAndGetAddressOf()))) {
			throw std::runtime_error{ "Failed to recreate Render Target output SRV." };
		}

		D3D11_TEXTURE2D_DESC const dsTexDesc{
			.Width = mWidth,
			.Height = mHeight,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_D24_UNORM_S8_UINT,
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_DEPTH_STENCIL,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};

		if (FAILED(mDevice->CreateTexture2D(&dsTexDesc, nullptr, mDepthTex.ReleaseAndGetAddressOf()))) {
			throw std::runtime_error{ "Failed to recreate Render Target depth-stencil texture." };
		}

		D3D11_DEPTH_STENCIL_VIEW_DESC const dsDsvDesc{
			.Format = dsTexDesc.Format,
			.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D,
			.Flags = 0,
			.Texture2D = { .MipSlice = 0 }
		};

		if (FAILED(mDevice->CreateDepthStencilView(mDepthTex.Get(), &dsDsvDesc,mDsv.ReleaseAndGetAddressOf()))) {
			throw std::runtime_error{ "Failed to recreate Render Target DSV." };
		}
	}

public:
	RenderTarget(ComPtr<ID3D11Device> device, UINT const width, UINT const height) :
		mDevice{ std::move(device) },
		mWidth{ width },
		mHeight{ height } {
		Recreate();
	}


	auto Resize(UINT const width, UINT const height) {
		mWidth = width;
		mHeight = height;
		Recreate();
	}


	[[nodiscard]] auto GetHdrRtv() const noexcept -> ID3D11RenderTargetView* {
		return mHdrRtv.Get();
	}


	[[nodiscard]] auto GetOutRtv() const noexcept -> ID3D11RenderTargetView* {
		return mOutRtv.Get();
	}


	[[nodiscard]] auto GetHdrSrv() const noexcept -> ID3D11ShaderResourceView* {
		return mHdrSrv.Get();
	}


	[[nodiscard]] auto GetOutSrv() const noexcept -> ID3D11ShaderResourceView* {
		return mOutSrv.Get();
	}


	[[nodiscard]] auto GetDsv() const noexcept -> ID3D11DepthStencilView* {
		return mDsv.Get();
	}


	[[nodiscard]] auto GetWidth() const noexcept -> UINT {
		return mWidth;
	}


	[[nodiscard]] auto GetHeight() const noexcept -> UINT {
		return mHeight;
	}
};


class SwapChain {
	DXGI_FORMAT static constexpr FORMAT{ DXGI_FORMAT_R8G8B8A8_UNORM };

	ComPtr<ID3D11Device> mDevice;
	ComPtr<IDXGISwapChain1> mSwapChain;
	ComPtr<ID3D11RenderTargetView> mRtv;

	UINT mSwapChainFlags{ 0 };
	UINT mPresentFlags{ 0 };


	auto CreateRtv() -> void {
		ComPtr<ID3D11Texture2D> backBuf;
		if (FAILED(mSwapChain->GetBuffer(0, IID_PPV_ARGS(backBuf.GetAddressOf())))) {
			throw std::runtime_error{ "Failed to get swap chain backbuffer." };
		}

		D3D11_RENDER_TARGET_VIEW_DESC constexpr rtvDesc{
			.Format = FORMAT,
			.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		if (FAILED(mDevice->CreateRenderTargetView(backBuf.Get(), &rtvDesc, mRtv.ReleaseAndGetAddressOf()))) {
			throw std::runtime_error{ "Failed to create swap chain RTV." };
		}
	}

public:
	SwapChain(ComPtr<ID3D11Device> device, IDXGIFactory2* const factory) :
		mDevice{ std::move(device) } {
		if (ComPtr<IDXGIFactory5> factory5; SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(factory5.GetAddressOf())))) {
			if (BOOL allowTearing{ FALSE }; SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof allowTearing)) && allowTearing) {
				mSwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
				mPresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
			}
		}

		DXGI_SWAP_CHAIN_DESC1 const desc{
			.Width = 0,
			.Height = 0,
			.Format = FORMAT,
			.Stereo = FALSE,
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
			.BufferCount = 2,
			.Scaling = DXGI_SCALING_NONE,
			.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
			.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
			.Flags = mSwapChainFlags
		};

		if (FAILED(factory->CreateSwapChainForHwnd(mDevice.Get(), gWindow.GetHandle(), &desc, nullptr, nullptr, mSwapChain.GetAddressOf()))) {
			throw std::runtime_error{ "Failed to create swap chain." };
		}

		CreateRtv();
	}


	auto Present(UINT const syncInterval) const -> void {
		if (FAILED(mSwapChain->Present(syncInterval, mPresentFlags))) {
			throw std::runtime_error{ "Failed to present swap chain." };
		}
	}


	auto Resize(UINT const width, UINT const height) -> void {
		if (width == 0 || height == 0) {
			return;
		}

		mRtv.Reset();

		if (FAILED(mSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, mSwapChainFlags))) {
			throw std::runtime_error{ "Failed to resize swap chain buffers." };
		}

		CreateRtv();
	}


	[[nodiscard]] auto GetRtv() const noexcept -> ID3D11RenderTargetView* {
		return mRtv.Get();
	}
};


template<typename T>
class StructuredBuffer {
	static_assert(sizeof(T) % 16 == 0, "StructuredBuffer contained type must have a size divisible by 16.");

	ComPtr<ID3D11Device> mDevice;
	ComPtr<ID3D11DeviceContext> mContext;
	ComPtr<ID3D11Buffer> mBuffer;
	ComPtr<ID3D11ShaderResourceView> mSrv;
	T* mMappedPtr{ nullptr };
	int mCapacity{ 1 };
	int mSize{ 0 };


	auto RecreateBuffer() -> void {
		D3D11_BUFFER_DESC const bufDesc{
			.ByteWidth = mCapacity * sizeof(T),
			.Usage = D3D11_USAGE_DYNAMIC,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE,
			.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
			.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
			.StructureByteStride = sizeof(T)
		};

		if (FAILED(mDevice->CreateBuffer(&bufDesc, nullptr, mBuffer.ReleaseAndGetAddressOf()))) {
			throw std::runtime_error{ "Failed to recreate StructuredBuffer buffer." };
		}
	}


	auto RecreateSrv() -> void {
		D3D11_SHADER_RESOURCE_VIEW_DESC const srvDesc{
			.Format = DXGI_FORMAT_UNKNOWN,
			.ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
			.Buffer = { .FirstElement = 0, .NumElements = static_cast<UINT>(mSize) }
		};

		if (FAILED(mDevice->CreateShaderResourceView(mBuffer.Get(), &srvDesc, mSrv.ReleaseAndGetAddressOf()))) {
			throw std::runtime_error{ "Failed to recreate StructuredBuffer SRV." };
		}
	}

public:
	StructuredBuffer(ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> context) :
		mDevice{ std::move(device) },
		mContext{ std::move(context) } {
		RecreateBuffer();
	}


	[[nodiscard]] auto GetSrv() const noexcept -> ID3D11ShaderResourceView* {
		return mSrv.Get();
	}


	auto Resize(int const newSize) -> void {
		auto newCapacity = mCapacity;

		while (newCapacity < newSize) {
			newCapacity *= 2;
		}

		if (newCapacity != mCapacity) {
			mCapacity = newCapacity;
			mSize = newSize;
			Unmap();
			RecreateBuffer();
			RecreateSrv();
		}
		else if (mSize != newSize) {
			mSize = newSize;
			RecreateSrv();
		}
	}


	[[nodiscard]] auto Map() -> std::span<T> {
		if (mMappedPtr) {
			return { mMappedPtr, static_cast<std::size_t>(mSize) };
		}

		D3D11_MAPPED_SUBRESOURCE mappedSubresource;
		if (FAILED(mContext->Map(mBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubresource))) {
			throw std::runtime_error{ "Failed to map Structured Buffer." };
		}

		mMappedPtr = static_cast<T*>(mappedSubresource.pData);
		return { static_cast<T*>(mMappedPtr), static_cast<std::size_t>(mSize) };
	}


	auto Unmap() -> void {
		if (mMappedPtr) {
			mContext->Unmap(mBuffer.Get(), 0);
			mMappedPtr = nullptr;
		}
	}
};


struct Resources {
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;

	ComPtr<ID3D11ShaderResourceView> gizmoColorSbSrv;
	ComPtr<ID3D11ShaderResourceView> lineGizmoVertexSbSrv;

	ComPtr<ID3D11PixelShader> meshPbrPS;
	ComPtr<ID3D11PixelShader> toneMapGammaPS;
	ComPtr<ID3D11PixelShader> skyboxPS;
	ComPtr<ID3D11PixelShader> gizmoPS;

	ComPtr<ID3D11VertexShader> meshVS;
	ComPtr<ID3D11VertexShader> skyboxVS;
	ComPtr<ID3D11VertexShader> shadowVS;
	ComPtr<ID3D11VertexShader> screenVS;
	ComPtr<ID3D11VertexShader> lineGizmoVS;

	ComPtr<ID3D11Buffer> perFrameCB;
	ComPtr<ID3D11Buffer> perCamCB;
	ComPtr<ID3D11Buffer> perModelCB;
	ComPtr<ID3D11Buffer> toneMapGammaCB;
	ComPtr<ID3D11Buffer> skyboxCB;
	ComPtr<ID3D11Buffer> shadowCB;
	ComPtr<ID3D11Buffer> gizmoColorSB;
	ComPtr<ID3D11Buffer> lineGizmoVertexSB;

	ComPtr<ID3D11InputLayout> meshIL;
	ComPtr<ID3D11InputLayout> skyboxIL;

	ComPtr<ID3D11SamplerState> ssCmpPcf;
	ComPtr<ID3D11SamplerState> ssCmpPoint;
	ComPtr<ID3D11SamplerState> ssAf16;
	ComPtr<ID3D11SamplerState> ssAf8;
	ComPtr<ID3D11SamplerState> ssAf4;
	ComPtr<ID3D11SamplerState> ssTri;
	ComPtr<ID3D11SamplerState> ssBi;
	ComPtr<ID3D11SamplerState> ssPoint;

	ComPtr<ID3D11RasterizerState> skyboxPassRS;
	ComPtr<ID3D11RasterizerState> shadowPassRS;

	ComPtr<ID3D11DepthStencilState> shadowDSS;
	ComPtr<ID3D11DepthStencilState> skyboxPassDSS;

	std::unique_ptr<Material> defaultMaterial;
	std::unique_ptr<Mesh> cubeMesh;
	std::unique_ptr<Mesh> planeMesh;

	std::array<std::unique_ptr<ShadowAtlas>, 2> shadowAtlases;

	std::unique_ptr<RenderTarget> gameViewRenderTarget;
	std::unique_ptr<RenderTarget> sceneViewRenderTarget;
	std::unique_ptr<SwapChain> swapChain;

	std::unique_ptr<StructuredBuffer<ShaderLight>> lightBuffer;
};


std::vector const QUAD_POSITIONS{
	Vector3{ -1, 1, 0 }, Vector3{ -1, -1, 0 }, Vector3{ 1, -1, 0 }, Vector3{ 1, 1, 0 }
};


std::vector const QUAD_NORMALS{
	Vector3::Backward(), Vector3::Backward(), Vector3::Backward(), Vector3::Backward()
};


std::vector const QUAD_UVS{
	Vector2{ 0, 0 }, Vector2{ 0, 1 }, Vector2{ 1, 1 }, Vector2{ 1, 0 }
};


std::vector<unsigned> const QUAD_INDICES{
	2, 1, 0, 0, 3, 2
};


std::vector const CUBE_POSITIONS{
	Vector3{ 0.5f, 0.5f, 0.5f },
	Vector3{ 0.5f, 0.5f, 0.5f },
	Vector3{ 0.5f, 0.5f, 0.5f },

	Vector3{ -0.5f, 0.5f, 0.5f },
	Vector3{ -0.5f, 0.5f, 0.5f },
	Vector3{ -0.5f, 0.5f, 0.5f },

	Vector3{ -0.5f, 0.5f, -0.5f },
	Vector3{ -0.5f, 0.5f, -0.5f },
	Vector3{ -0.5f, 0.5f, -0.5f },

	Vector3{ 0.5f, 0.5f, -0.5f },
	Vector3{ 0.5f, 0.5f, -0.5f },
	Vector3{ 0.5f, 0.5f, -0.5f },

	Vector3{ 0.5f, -0.5f, 0.5f },
	Vector3{ 0.5f, -0.5f, 0.5f },
	Vector3{ 0.5f, -0.5f, 0.5f },

	Vector3{ -0.5f, -0.5f, 0.5f },
	Vector3{ -0.5f, -0.5f, 0.5f },
	Vector3{ -0.5f, -0.5f, 0.5f },

	Vector3{ -0.5f, -0.5f, -0.5f },
	Vector3{ -0.5f, -0.5f, -0.5f },
	Vector3{ -0.5f, -0.5f, -0.5f },

	Vector3{ 0.5f, -0.5f, -0.5f },
	Vector3{ 0.5f, -0.5f, -0.5f },
	Vector3{ 0.5f, -0.5f, -0.5f },
};


std::vector const CUBE_NORMALS{
	Vector3{ 1.0f, 0.0f, 0.0f },
	Vector3{ 0.0f, 1.0f, 0.0f },
	Vector3{ 0.0f, 0.0f, 1.0f },

	Vector3{ -1.0f, 0.0f, 0.0f },
	Vector3{ 0.0f, 1.0f, 0.0f },
	Vector3{ 0.0f, 0.0f, 1.0f },

	Vector3{ -1.0f, 0.0f, 0.0f },
	Vector3{ 0.0f, 1.0f, 0.0f },
	Vector3{ 0.0f, 0.0f, -1.0f },

	Vector3{ 1.0f, 0.0f, 0.0f },
	Vector3{ 0.0f, 1.0f, 0.0f },
	Vector3{ 0.0f, 0.0f, -1.0f },

	Vector3{ 1.0f, 0.0f, 0.0f },
	Vector3{ 0.0f, -1.0f, 0.0f },
	Vector3{ 0.0f, 0.0f, 1.0f },

	Vector3{ -1.0f, 0.0f, 0.0f },
	Vector3{ 0.0f, -1.0f, 0.0f },
	Vector3{ 0.0f, 0.0f, 1.0f },

	Vector3{ -1.0f, 0.0f, 0.0f },
	Vector3{ 0.0f, -1.0f, 0.0f },
	Vector3{ 0.0f, 0.0f, -1.0f },

	Vector3{ 1.0f, 0.0f, 0.0f },
	Vector3{ 0.0f, -1.0f, 0.0f },
	Vector3{ 0.0f, 0.0f, -1.0f },
};


std::vector const CUBE_UVS{
	Vector2{ 1, 0 },
	Vector2{ 1, 0 },
	Vector2{ 0, 0 },

	Vector2{ 0, 0 },
	Vector2{ 0, 0 },
	Vector2{ 1, 0 },

	Vector2{ 1, 0 },
	Vector2{ 0, 1 },
	Vector2{ 0, 0 },

	Vector2{ 0, 0 },
	Vector2{ 1, 1 },
	Vector2{ 1, 0 },

	Vector2{ 1, 1 },
	Vector2{ 1, 1 },
	Vector2{ 0, 1 },

	Vector2{ 0, 1 },
	Vector2{ 0, 1 },
	Vector2{ 1, 1 },

	Vector2{ 1, 1 },
	Vector2{ 0, 0 },
	Vector2{ 0, 1 },

	Vector2{ 0, 1 },
	Vector2{ 1, 0 },
	Vector2{ 1, 1 }
};


std::vector<UINT> const CUBE_INDICES{
	// Top face
	7, 4, 1,
	1, 10, 7,
	// Bottom face
	16, 19, 22,
	22, 13, 16,
	// Front face
	23, 20, 8,
	8, 11, 23,
	// Back face
	17, 14, 2,
	2, 5, 17,
	// Right face
	21, 9, 0,
	0, 12, 21,
	// Left face
	15, 3, 6,
	6, 18, 15
};

constexpr auto DIR_SHADOW_ATLAS_IDX{ 0 };
constexpr auto PUNC_SHADOW_ATLAS_IDX{ 1 };

Resources* gResources{ nullptr };
u32 gSyncInterval{ 0 };
std::vector<StaticMeshComponent const*> gStaticMeshComponents;
f32 gInvGamma{ 1.f / 2.2f };
std::vector<SkyboxComponent const*> gSkyboxes;
std::vector<LightComponent const*> gLights;
std::vector<Camera const*> gGameRenderCameras;
std::vector<ShaderLineGizmoVertexData> gLineGizmoVertexData;
std::vector<Vector4> gGizmoColors;
int gGizmoColorBufferSize{ 1 };
int gLineGizmoVertexBufferSize{ 1 };


auto CreateDeviceAndContext() -> void {
	UINT creationFlags{ 0 };
	D3D_FEATURE_LEVEL constexpr requestedFeatureLevels[]{ D3D_FEATURE_LEVEL_11_0 };

#ifndef NDEBUG
	creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creationFlags, requestedFeatureLevels, 1, D3D11_SDK_VERSION, gResources->device.GetAddressOf(), nullptr, gResources->context.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create D3D device." };
	}
}


auto SetDebugBreaks() -> void {
	ComPtr<ID3D11Debug> d3dDebug;
	if (FAILED(gResources->device.As(&d3dDebug))) {
		throw std::runtime_error{ "Failed to get ID3D11Debug interface." };
	}

	ComPtr<ID3D11InfoQueue> d3dInfoQueue;
	if (FAILED(d3dDebug.As<ID3D11InfoQueue>(&d3dInfoQueue))) {
		throw std::runtime_error{ "Failed to get ID3D11InfoQueue interface." };
	}

	d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
	d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
}


auto CreateInputLayouts() -> void {
	D3D11_INPUT_ELEMENT_DESC constexpr meshInputDesc[]{
		{
			.SemanticName = "POSITION",
			.SemanticIndex = 0,
			.Format = DXGI_FORMAT_R32G32B32_FLOAT,
			.InputSlot = 0,
			.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT,
			.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA,
			.InstanceDataStepRate = 0
		},
		{
			.SemanticName = "NORMAL",
			.SemanticIndex = 0,
			.Format = DXGI_FORMAT_R32G32B32_FLOAT,
			.InputSlot = 1,
			.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT,
			.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA,
			.InstanceDataStepRate = 0
		},
		{
			.SemanticName = "TEXCOORD",
			.SemanticIndex = 0,
			.Format = DXGI_FORMAT_R32G32_FLOAT,
			.InputSlot = 2,
			.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT,
			.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA,
			.InstanceDataStepRate = 0
		}
	};

	if (FAILED(gResources->device->CreateInputLayout(meshInputDesc, ARRAYSIZE(meshInputDesc), gMeshVSBin, ARRAYSIZE(gMeshVSBin), gResources->meshIL.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create mesh input layout." };
	}

	D3D11_INPUT_ELEMENT_DESC constexpr skyboxInputDesc
	{
		.SemanticName = "POSITION",
		.SemanticIndex = 0,
		.Format = DXGI_FORMAT_R32G32B32_FLOAT,
		.InputSlot = 0,
		.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT,
		.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA,
		.InstanceDataStepRate = 0
	};

	if (FAILED(gResources->device->CreateInputLayout(&skyboxInputDesc, 1, gSkyboxVSBin, ARRAYSIZE(gSkyboxVSBin), gResources->skyboxIL.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create skybox pass input layout." };
	}
}


auto CreateShaders() -> void {
	if (FAILED(gResources->device->CreateVertexShader(gMeshVSBin, ARRAYSIZE(gMeshVSBin), nullptr, gResources->meshVS.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create mesh vertex shader." };
	}

	if (FAILED(gResources->device->CreatePixelShader(gMeshPbrPSBin, ARRAYSIZE(gMeshPbrPSBin), nullptr, gResources->meshPbrPS.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create mesh pbr pixel shader." };
	}

	if (FAILED(gResources->device->CreatePixelShader(gToneMapGammaPSBin, ARRAYSIZE(gToneMapGammaPSBin), nullptr, gResources->toneMapGammaPS.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create textured tonemap-gamma pixel shader." };
	}

	if (FAILED(gResources->device->CreateVertexShader(gSkyboxVSBin, ARRAYSIZE(gSkyboxVSBin), nullptr, gResources->skyboxVS.ReleaseAndGetAddressOf()))) {
		throw std::runtime_error{ "Failed to create skybox vertex shader." };
	}

	if (FAILED(gResources->device->CreatePixelShader(gSkyboxPSBin, ARRAYSIZE(gSkyboxPSBin), nullptr, gResources->skyboxPS.ReleaseAndGetAddressOf()))) {
		throw std::runtime_error{ "Failed to create skybox pixel shader." };
	}

	if (FAILED(gResources->device->CreateVertexShader(gShadowVSBin, ARRAYSIZE(gShadowVSBin), nullptr, gResources->shadowVS.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create shadow vertex shader." };
	}

	if (FAILED(gResources->device->CreateVertexShader(gScreenVSBin, ARRAYSIZE(gScreenVSBin), nullptr, gResources->screenVS.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create screen vertex shader." };
	}

	if (FAILED(gResources->device->CreateVertexShader(gLineGizmoVSBin, ARRAYSIZE(gLineGizmoVSBin), nullptr, gResources->lineGizmoVS.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create line gizmo vertex shader." };
	}

	if (FAILED(gResources->device->CreatePixelShader(gGizmoPSBin, ARRAYSIZE(gGizmoPSBin), nullptr, gResources->gizmoPS.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create gizmo pixel shader." };
	}
}


auto CreateConstantBuffers() -> void {
	D3D11_BUFFER_DESC constexpr perFrameCbDesc{
		.ByteWidth = clamp_cast<UINT>(RoundToNextMultiple(sizeof(PerFrameCB), 16)),
		.Usage = D3D11_USAGE_DYNAMIC,
		.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
		.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
		.MiscFlags = 0,
		.StructureByteStride = 0
	};

	if (FAILED(gResources->device->CreateBuffer(&perFrameCbDesc, nullptr, gResources->perFrameCB.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create per frame CB." };
	}

	D3D11_BUFFER_DESC constexpr perCamCbDesc{
		.ByteWidth = clamp_cast<UINT>(RoundToNextMultiple(sizeof(PerCameraCB), 16)),
		.Usage = D3D11_USAGE_DYNAMIC,
		.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
		.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
		.MiscFlags = 0,
		.StructureByteStride = 0
	};

	if (FAILED(gResources->device->CreateBuffer(&perCamCbDesc, nullptr, gResources->perCamCB.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create camera constant buffer." };
	}

	D3D11_BUFFER_DESC constexpr perModelCbDesc{
		.ByteWidth = clamp_cast<UINT>(RoundToNextMultiple(sizeof(PerModelCB), 16)),
		.Usage = D3D11_USAGE_DYNAMIC,
		.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
		.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
		.MiscFlags = 0,
		.StructureByteStride = 0
	};

	if (FAILED(gResources->device->CreateBuffer(&perModelCbDesc, nullptr, gResources->perModelCB.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create model constant buffer." };
	}

	D3D11_BUFFER_DESC constexpr toneMapGammaCBDesc{
		.ByteWidth = clamp_cast<UINT>(RoundToNextMultiple(sizeof(ToneMapGammaCB), 16)),
		.Usage = D3D11_USAGE_DYNAMIC,
		.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
		.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
		.MiscFlags = 0,
		.StructureByteStride = 0
	};

	if (FAILED(gResources->device->CreateBuffer(&toneMapGammaCBDesc, nullptr, gResources->toneMapGammaCB.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create tonemap-gamma constant buffer." };
	}

	D3D11_BUFFER_DESC constexpr skyboxCBDesc{
		.ByteWidth = clamp_cast<UINT>(RoundToNextMultiple(sizeof(SkyboxCB), 16)),
		.Usage = D3D11_USAGE_DYNAMIC,
		.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
		.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
		.MiscFlags = 0,
		.StructureByteStride = 0
	};

	if (FAILED(gResources->device->CreateBuffer(&skyboxCBDesc, nullptr, gResources->skyboxCB.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create skybox pass constant buffer." };
	}

	D3D11_BUFFER_DESC constexpr shadowCBDesc{
		.ByteWidth = clamp_cast<UINT>(RoundToNextMultiple(sizeof(ShadowCB), 16)),
		.Usage = D3D11_USAGE_DYNAMIC,
		.BindFlags = D3D11_BIND_CONSTANT_BUFFER,
		.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
		.MiscFlags = 0,
		.StructureByteStride = 0
	};

	if (FAILED(gResources->device->CreateBuffer(&shadowCBDesc, nullptr, gResources->shadowCB.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create shadow constant buffer." };
	}
}


auto CreateRasterizerStates() -> void {
	D3D11_RASTERIZER_DESC constexpr skyboxPassRasterizerDesc{
		.FillMode = D3D11_FILL_SOLID,
		.CullMode = D3D11_CULL_NONE,
		.FrontCounterClockwise = FALSE,
		.DepthBias = 0,
		.DepthBiasClamp = 0.0f,
		.SlopeScaledDepthBias = 0.0f,
		.DepthClipEnable = TRUE,
		.ScissorEnable = FALSE,
		.MultisampleEnable = FALSE,
		.AntialiasedLineEnable = FALSE
	};

	if (FAILED(gResources->device->CreateRasterizerState(&skyboxPassRasterizerDesc, gResources->skyboxPassRS.ReleaseAndGetAddressOf()))) {
		throw std::runtime_error{ "Failed to create skybox pass rasterizer state." };
	}

	D3D11_RASTERIZER_DESC constexpr shadowPassRasterizerDesc{
		.FillMode = D3D11_FILL_SOLID,
		.CullMode = D3D11_CULL_FRONT,
		.FrontCounterClockwise = FALSE,
		.DepthBias = 0,
		.DepthBiasClamp = 0,
		.SlopeScaledDepthBias = 0,
		.DepthClipEnable = TRUE,
		.ScissorEnable = FALSE,
		.MultisampleEnable = FALSE,
		.AntialiasedLineEnable = FALSE
	};

	if (FAILED(gResources->device->CreateRasterizerState(&shadowPassRasterizerDesc, gResources->shadowPassRS.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create shadow pass rasterizer state." };
	}
}


auto CreateDepthStencilStates() -> void {
	D3D11_DEPTH_STENCIL_DESC constexpr skyboxPassDepthStencilDesc{
		.DepthEnable = TRUE,
		.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO,
		.DepthFunc = D3D11_COMPARISON_LESS_EQUAL,
		.StencilEnable = FALSE,
		.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK,
		.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK,
		.FrontFace = {
			.StencilFailOp = D3D11_STENCIL_OP_KEEP,
			.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP,
			.StencilPassOp = D3D11_STENCIL_OP_KEEP,
			.StencilFunc = D3D11_COMPARISON_ALWAYS
		},
		.BackFace = {
			.StencilFailOp = D3D11_STENCIL_OP_KEEP,
			.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP,
			.StencilPassOp = D3D11_STENCIL_OP_KEEP,
			.StencilFunc = D3D11_COMPARISON_ALWAYS
		}
	};

	if (FAILED(gResources->device->CreateDepthStencilState(&skyboxPassDepthStencilDesc, gResources->skyboxPassDSS.ReleaseAndGetAddressOf()))) {
		throw std::runtime_error{ "Failed to create skybox pass depth-stencil state." };
	}

	D3D11_DEPTH_STENCIL_DESC constexpr shadowPassDesc{
		.DepthEnable = TRUE,
		.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL,
		.DepthFunc = D3D11_COMPARISON_GREATER,
		.StencilEnable = FALSE,
		.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK,
		.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK,
		.FrontFace = {
			.StencilFailOp = D3D11_STENCIL_OP_KEEP,
			.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP,
			.StencilPassOp = D3D11_STENCIL_OP_KEEP,
			.StencilFunc = D3D11_COMPARISON_ALWAYS
		},
		.BackFace = {
			.StencilFailOp = D3D11_STENCIL_OP_KEEP,
			.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP,
			.StencilPassOp = D3D11_STENCIL_OP_KEEP,
			.StencilFunc = D3D11_COMPARISON_ALWAYS
		}
	};

	if (FAILED(gResources->device->CreateDepthStencilState(&shadowPassDesc, gResources->shadowDSS.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create shadow pass depth-stencil state." };
	}
}


auto CreateShadowAtlases() -> void {
	gResources->shadowAtlases[PUNC_SHADOW_ATLAS_IDX] = std::make_unique<PunctualShadowAtlas>(gResources->device.Get(), 4096);
	gResources->shadowAtlases[DIR_SHADOW_ATLAS_IDX] = std::make_unique<DirectionalShadowAtlas>(gResources->device.Get(), 4096);
}


auto CreateSamplerStates() -> void {
	D3D11_SAMPLER_DESC constexpr cmpPcf{
		.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
		.AddressU = D3D11_TEXTURE_ADDRESS_BORDER,
		.AddressV = D3D11_TEXTURE_ADDRESS_BORDER,
		.AddressW = D3D11_TEXTURE_ADDRESS_BORDER,
		.MipLODBias = 0,
		.MaxAnisotropy = 1,
		.ComparisonFunc = D3D11_COMPARISON_GREATER_EQUAL,
		.BorderColor = { 0, 0, 0, 0 },
		.MinLOD = 0,
		.MaxLOD = 0
	};

	if (FAILED(gResources->device->CreateSamplerState(&cmpPcf, gResources->ssCmpPcf.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create PCF comparison sampler state." };
	}

	D3D11_SAMPLER_DESC constexpr cmpPointDesc{
		.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT,
		.AddressU = D3D11_TEXTURE_ADDRESS_BORDER,
		.AddressV = D3D11_TEXTURE_ADDRESS_BORDER,
		.AddressW = D3D11_TEXTURE_ADDRESS_BORDER,
		.MipLODBias = 0,
		.MaxAnisotropy = 1,
		.ComparisonFunc = D3D11_COMPARISON_GREATER_EQUAL,
		.BorderColor = { 0, 0, 0, 0 },
		.MinLOD = 0,
		.MaxLOD = 0
	};

	if (FAILED(gResources->device->CreateSamplerState(&cmpPointDesc, gResources->ssCmpPoint.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create point-filter comparison sampler state." };
	}

	D3D11_SAMPLER_DESC constexpr af16Desc{
		.Filter = D3D11_FILTER_ANISOTROPIC,
		.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
		.MipLODBias = 0,
		.MaxAnisotropy = 16,
		.ComparisonFunc = D3D11_COMPARISON_NEVER,
		.BorderColor = { 1.0f, 1.0f, 1.0f, 1.0f },
		.MinLOD = -FLT_MAX,
		.MaxLOD = FLT_MAX
	};

	if (FAILED(gResources->device->CreateSamplerState(&af16Desc, gResources->ssAf16.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create AF16 sampler state." };
	}

	D3D11_SAMPLER_DESC constexpr af8Desc{
		.Filter = D3D11_FILTER_ANISOTROPIC,
		.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
		.MipLODBias = 0,
		.MaxAnisotropy = 8,
		.ComparisonFunc = D3D11_COMPARISON_NEVER,
		.BorderColor = { 1.0f, 1.0f, 1.0f, 1.0f },
		.MinLOD = -FLT_MAX,
		.MaxLOD = FLT_MAX
	};

	if (FAILED(gResources->device->CreateSamplerState(&af8Desc, gResources->ssAf8.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create AF8 sampler state." };
	}

	D3D11_SAMPLER_DESC constexpr af4Desc{
		.Filter = D3D11_FILTER_ANISOTROPIC,
		.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
		.MipLODBias = 0,
		.MaxAnisotropy = 4,
		.ComparisonFunc = D3D11_COMPARISON_NEVER,
		.BorderColor = { 1.0f, 1.0f, 1.0f, 1.0f },
		.MinLOD = -FLT_MAX,
		.MaxLOD = FLT_MAX
	};

	if (FAILED(gResources->device->CreateSamplerState(&af4Desc, gResources->ssAf4.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create AF4 sampler state." };
	}

	D3D11_SAMPLER_DESC constexpr trilinearDesc{
		.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
		.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
		.MipLODBias = 0,
		.MaxAnisotropy = 1,
		.ComparisonFunc = D3D11_COMPARISON_NEVER,
		.BorderColor = { 1.0f, 1.0f, 1.0f, 1.0f },
		.MinLOD = -FLT_MAX,
		.MaxLOD = FLT_MAX
	};

	if (FAILED(gResources->device->CreateSamplerState(&trilinearDesc, gResources->ssTri.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create trilinear sampler state." };
	}

	D3D11_SAMPLER_DESC constexpr bilinearDesc{
		.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
		.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
		.MipLODBias = 0,
		.MaxAnisotropy = 1,
		.ComparisonFunc = D3D11_COMPARISON_NEVER,
		.BorderColor = { 1.0f, 1.0f, 1.0f, 1.0f },
		.MinLOD = -FLT_MAX,
		.MaxLOD = FLT_MAX
	};

	if (FAILED(gResources->device->CreateSamplerState(&bilinearDesc, gResources->ssBi.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create bilinear sampler state." };
	}

	D3D11_SAMPLER_DESC constexpr pointDesc{
		.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
		.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
		.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
		.MipLODBias = 0,
		.MaxAnisotropy = 1,
		.ComparisonFunc = D3D11_COMPARISON_NEVER,
		.BorderColor = { 1.0f, 1.0f, 1.0f, 1.0f },
		.MinLOD = -FLT_MAX,
		.MaxLOD = FLT_MAX
	};

	if (FAILED(gResources->device->CreateSamplerState(&pointDesc, gResources->ssPoint.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to create point-filter sampler state." };
	}
}


auto CreateDefaultAssets() -> void {
	gResources->defaultMaterial = std::make_unique<Material>();
	gResources->defaultMaterial->SetName("Default Material");

	gResources->cubeMesh = std::make_unique<Mesh>();
	gResources->cubeMesh->SetGuid(Guid{ 0, 0 });
	gResources->cubeMesh->SetName("Cube");
	gResources->cubeMesh->SetPositions(CUBE_POSITIONS);
	gResources->cubeMesh->SetNormals(CUBE_NORMALS);
	gResources->cubeMesh->SetUVs(CUBE_UVS);
	gResources->cubeMesh->SetIndices(CUBE_INDICES);
	gResources->cubeMesh->SetSubMeshes(std::vector{ Mesh::SubMeshData{ 0, 0, static_cast<int>(CUBE_INDICES.size()) } });
	gResources->cubeMesh->ValidateAndUpdate();

	gResources->planeMesh = std::make_unique<Mesh>();
	gResources->planeMesh->SetGuid(Guid{ 0, 1 });
	gResources->planeMesh->SetName("Plane");
	gResources->planeMesh->SetPositions(QUAD_POSITIONS);
	gResources->planeMesh->SetNormals(QUAD_NORMALS);
	gResources->planeMesh->SetUVs(QUAD_UVS);
	gResources->planeMesh->SetIndices(QUAD_INDICES);
	gResources->planeMesh->SetSubMeshes(std::vector{ Mesh::SubMeshData{ 0, 0, static_cast<int>(QUAD_INDICES.size()) } });
	gResources->planeMesh->ValidateAndUpdate();
}


auto RecreateGizmoColorBuffer() -> void {
	D3D11_BUFFER_DESC const bufDesc{
		.ByteWidth = static_cast<UINT>(gGizmoColorBufferSize * sizeof(decltype(gGizmoColors)::value_type)),
		.Usage = D3D11_USAGE_DYNAMIC,
		.BindFlags = D3D11_BIND_SHADER_RESOURCE,
		.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
		.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
		.StructureByteStride = sizeof(decltype(gGizmoColors)::value_type)
	};

	if (FAILED(gResources->device->CreateBuffer(&bufDesc, nullptr, gResources->gizmoColorSB.ReleaseAndGetAddressOf()))) {
		throw std::runtime_error{ "Failed to create gizmo color structured buffer." };
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC const srvDesc{
		.Format = DXGI_FORMAT_UNKNOWN,
		.ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
		.Buffer = {
			.FirstElement = 0,
			.NumElements = static_cast<UINT>(gGizmoColorBufferSize)
		}
	};

	if (FAILED(gResources->device->CreateShaderResourceView(gResources->gizmoColorSB.Get(), &srvDesc, gResources->gizmoColorSbSrv.ReleaseAndGetAddressOf()))) {
		throw std::runtime_error{ "Failed to create gizmo color SB SRV." };
	}
}


auto RecreateLineGizmoVertexBuffer() -> void {
	D3D11_BUFFER_DESC const bufDesc{
		.ByteWidth = static_cast<UINT>(gLineGizmoVertexBufferSize * sizeof(decltype(gLineGizmoVertexData)::value_type)),
		.Usage = D3D11_USAGE_DYNAMIC,
		.BindFlags = D3D11_BIND_SHADER_RESOURCE,
		.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
		.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
		.StructureByteStride = sizeof(decltype(gLineGizmoVertexData)::value_type)
	};

	if (FAILED(gResources->device->CreateBuffer(&bufDesc, nullptr, gResources->lineGizmoVertexSB.ReleaseAndGetAddressOf()))) {
		throw std::runtime_error{ "Failed to create line gizmo vertex structured buffer." };
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC const srvDesc{
		.Format = DXGI_FORMAT_UNKNOWN,
		.ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
		.Buffer = {
			.FirstElement = 0,
			.NumElements = static_cast<UINT>(gLineGizmoVertexBufferSize)
		}
	};

	if (FAILED(gResources->device->CreateShaderResourceView(gResources->lineGizmoVertexSB.Get(), &srvDesc, gResources->lineGizmoVertexSbSrv.ReleaseAndGetAddressOf()))) {
		throw std::runtime_error{ "Failed to create line gizmo vertex SB SRV." };
	}
}


auto CreateStructuredBuffers() -> void {
	RecreateGizmoColorBuffer();
	RecreateLineGizmoVertexBuffer();
}


auto DrawMeshes(std::span<int const> const meshComponentIndices, bool const useMaterials) noexcept -> void {
	gResources->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	gResources->context->IASetInputLayout(gResources->meshIL.Get());

	gResources->context->VSSetConstantBuffers(CB_SLOT_PER_MODEL, 1, gResources->perModelCB.GetAddressOf());
	gResources->context->PSSetConstantBuffers(CB_SLOT_PER_MODEL, 1, gResources->perModelCB.GetAddressOf());

	for (auto const meshComponentIdx : meshComponentIndices) {
		auto const meshComponent{ gStaticMeshComponents[meshComponentIdx] };
		auto const& mesh{ meshComponent->GetMesh() };

		ID3D11Buffer* vertexBuffers[]{ mesh.GetPositionBuffer().Get(), mesh.GetNormalBuffer().Get(), mesh.GetUVBuffer().Get() };
		UINT constexpr strides[]{ sizeof(Vector3), sizeof(Vector3), sizeof(Vector2) };
		UINT constexpr offsets[]{ 0, 0, 0 };
		gResources->context->IASetVertexBuffers(0, 3, vertexBuffers, strides, offsets);
		gResources->context->IASetIndexBuffer(mesh.GetIndexBuffer().Get(), DXGI_FORMAT_R32_UINT, 0);

		D3D11_MAPPED_SUBRESOURCE mappedPerModelCBuf;
		gResources->context->Map(gResources->perModelCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedPerModelCBuf);
		auto& [modelMatData, normalMatData]{ *static_cast<PerModelCB*>(mappedPerModelCBuf.pData) };
		modelMatData = meshComponent->GetEntity()->GetTransform().GetModelMatrix();
		normalMatData = Matrix4{ meshComponent->GetEntity()->GetTransform().GetNormalMatrix() };
		gResources->context->Unmap(gResources->perModelCB.Get(), 0);

		auto const subMeshes{ mesh.GetSubMeshes() };
		auto const& materials{ meshComponent->GetMaterials() };

		for (int i = 0; i < static_cast<int>(subMeshes.size()); i++) {
			auto const& [baseVertex, firstIndex, indexCount]{ subMeshes[i] };

			if (useMaterials) {
				auto const& mtl{ static_cast<int>(materials.size()) > i ? *materials[i] : *gResources->defaultMaterial };

				auto const mtlBuffer{ mtl.GetBuffer() };
				gResources->context->VSSetConstantBuffers(CB_SLOT_PER_MATERIAL, 1, &mtlBuffer);
				gResources->context->PSSetConstantBuffers(CB_SLOT_PER_MATERIAL, 1, &mtlBuffer);

				auto const albedoSrv{ mtl.GetAlbedoMap() ? mtl.GetAlbedoMap()->GetSrv() : nullptr };
				gResources->context->PSSetShaderResources(RES_SLOT_ALBEDO_MAP, 1, &albedoSrv);

				auto const metallicSrv{ mtl.GetMetallicMap() ? mtl.GetMetallicMap()->GetSrv() : nullptr };
				gResources->context->PSSetShaderResources(RES_SLOT_METALLIC_MAP, 1, &metallicSrv);

				auto const roughnessSrv{ mtl.GetRoughnessMap() ? mtl.GetRoughnessMap()->GetSrv() : nullptr };
				gResources->context->PSSetShaderResources(RES_SLOT_ROUGHNESS_MAP, 1, &roughnessSrv);

				auto const aoSrv{ mtl.GetAoMap() ? mtl.GetAoMap()->GetSrv() : nullptr };
				gResources->context->PSSetShaderResources(RES_SLOT_AO_MAP, 1, &aoSrv);
			}

			gResources->context->DrawIndexed(indexCount, firstIndex, baseVertex);
		}
	}
}


auto DoToneMapGammaCorrectionStep(ID3D11ShaderResourceView* const src, ID3D11RenderTargetView* const dst) noexcept -> void {
	// Back up old views to restore later.

	ComPtr<ID3D11RenderTargetView> rtvBackup;
	ComPtr<ID3D11DepthStencilView> dsvBackup;
	ComPtr<ID3D11ShaderResourceView> srvBackup;
	gResources->context->OMGetRenderTargets(1, rtvBackup.GetAddressOf(), dsvBackup.GetAddressOf());
	gResources->context->PSGetShaderResources(0, 1, srvBackup.GetAddressOf());

	// Do the step

	D3D11_MAPPED_SUBRESOURCE mappedToneMapGammaCB;
	gResources->context->Map(gResources->toneMapGammaCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedToneMapGammaCB);
	auto const toneMapGammaCBData{ static_cast<ToneMapGammaCB*>(mappedToneMapGammaCB.pData) };
	toneMapGammaCBData->invGamma = gInvGamma;
	gResources->context->Unmap(gResources->toneMapGammaCB.Get(), 0);

	gResources->context->VSSetShader(gResources->screenVS.Get(), nullptr, 0);
	gResources->context->PSSetShader(gResources->toneMapGammaPS.Get(), nullptr, 0);

	gResources->context->OMSetRenderTargets(1, &dst, nullptr);

	gResources->context->PSSetConstantBuffers(CB_SLOT_TONE_MAP_GAMMA, 1, gResources->toneMapGammaCB.GetAddressOf());
	gResources->context->PSSetShaderResources(RES_SLOT_TONE_MAP_SRC, 1, &src);

	gResources->context->IASetInputLayout(nullptr);
	gResources->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	gResources->context->Draw(6, 0);

	// Restore old view bindings to that we don't leave any input/output conflicts behind.

	gResources->context->PSSetShaderResources(RES_SLOT_TONE_MAP_SRC, 1, srvBackup.GetAddressOf());
	gResources->context->OMSetRenderTargets(1, rtvBackup.GetAddressOf(), dsvBackup.Get());
}


auto DrawSkybox(Matrix4 const& camViewMtx, Matrix4 const& camProjMtx) noexcept -> void {
	if (gSkyboxes.empty()) {
		return;
	}

	D3D11_MAPPED_SUBRESOURCE mappedSkyboxCB;
	gResources->context->Map(gResources->skyboxCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSkyboxCB);
	auto const skyboxCBData{ static_cast<SkyboxCB*>(mappedSkyboxCB.pData) };
	skyboxCBData->skyboxViewProjMtx = Matrix4{ Matrix3{ camViewMtx } } * camProjMtx;
	gResources->context->Unmap(gResources->skyboxCB.Get(), 0);

	ID3D11Buffer* const vertexBuffer{ gResources->cubeMesh->GetPositionBuffer().Get() };
	UINT constexpr stride{ sizeof(Vector3) };
	UINT constexpr offset{ 0 };
	gResources->context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
	gResources->context->IASetIndexBuffer(gResources->cubeMesh->GetIndexBuffer().Get(), DXGI_FORMAT_R32_UINT, 0);
	gResources->context->IASetInputLayout(gResources->skyboxIL.Get());
	gResources->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	gResources->context->VSSetShader(gResources->skyboxVS.Get(), nullptr, 0);
	gResources->context->PSSetShader(gResources->skyboxPS.Get(), nullptr, 0);

	auto const cubemapSrv{ gSkyboxes[0]->GetCubemap()->GetSrv() };
	gResources->context->PSSetShaderResources(RES_SLOT_SKYBOX_CUBEMAP, 1, &cubemapSrv);

	auto const cb{ gResources->skyboxCB.Get() };
	gResources->context->VSSetConstantBuffers(CB_SLOT_SKYBOX_PASS, 1, &cb);

	gResources->context->OMSetDepthStencilState(gResources->skyboxPassDSS.Get(), 0);
	gResources->context->RSSetState(gResources->skyboxPassRS.Get());

	gResources->context->DrawIndexed(clamp_cast<UINT>(CUBE_INDICES.size()), 0, 0);

	// Restore state
	gResources->context->OMSetDepthStencilState(nullptr, 0);
	gResources->context->RSSetState(nullptr);
}


auto DrawShadowMaps(ShadowAtlas const& atlas) -> void {
	gResources->context->OMSetRenderTargets(0, nullptr, atlas.GetDsv());
	gResources->context->ClearDepthStencilView(atlas.GetDsv(), D3D11_CLEAR_DEPTH, 0, 0);
	gResources->context->VSSetShader(gResources->shadowVS.Get(), nullptr, 0);
	gResources->context->PSSetShader(nullptr, nullptr, 0);
	gResources->context->VSSetConstantBuffers(CB_SLOT_SHADOW_PASS, 1, gResources->shadowCB.GetAddressOf());
	gResources->context->OMSetDepthStencilState(gResources->shadowDSS.Get(), 0);
	gResources->context->RSSetState(gResources->shadowPassRS.Get());

	auto const cellSizeNorm{ atlas.GetNormalizedElementSize() };

	for (auto i = 0; i < atlas.GetElementCount(); i++) {
		auto const& cell{ atlas.GetCell(i) };
		auto const cellOffsetNorm{ atlas.GetNormalizedElementOffset(i) };
		auto const subcellSize{ cellSizeNorm * cell.GetNormalizedElementSize() * static_cast<float>(atlas.GetSize()) };

		for (auto j = 0; j < cell.GetElementCount(); j++) {
			if (auto const& subcell{ cell.GetSubcell(j) }) {
				auto const subcellOffset{ (cellOffsetNorm + cell.GetNormalizedElementOffset(j) * cellSizeNorm) * static_cast<float>(atlas.GetSize()) };

				D3D11_VIEWPORT const viewport{
					.TopLeftX = subcellOffset[0],
					.TopLeftY = subcellOffset[1],
					.Width = subcellSize,
					.Height = subcellSize,
					.MinDepth = 0,
					.MaxDepth = 1
				};

				gResources->context->RSSetViewports(1, &viewport);

				D3D11_MAPPED_SUBRESOURCE mapped;
				gResources->context->Map(gResources->shadowCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
				auto const shadowCbData{ static_cast<ShadowCB*>(mapped.pData) };
				shadowCbData->shadowViewProjMtx = subcell->shadowViewProjMtx;
				gResources->context->Unmap(gResources->shadowCB.Get(), 0);

				Frustum const shadowFrustumWS{ subcell->shadowViewProjMtx };

				Visibility static perLightVisibility;

				CullStaticMeshComponents(shadowFrustumWS, perLightVisibility);
				DrawMeshes(perLightVisibility.staticMeshIndices, false);
			}
		}
	}
}


auto DrawGizmos() -> void {
	auto gizmoColorBufferSize{ gGizmoColorBufferSize };

	while (gizmoColorBufferSize < static_cast<int>(gGizmoColors.size())) {
		gizmoColorBufferSize *= 2;
	}

	if (gizmoColorBufferSize != gGizmoColorBufferSize) {
		gGizmoColorBufferSize = gizmoColorBufferSize;
		RecreateGizmoColorBuffer();
	}

	if (!gGizmoColors.empty()) {
		D3D11_MAPPED_SUBRESOURCE mappedGizmoColorSb;
		gResources->context->Map(gResources->gizmoColorSB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedGizmoColorSb);
		std::memcpy(mappedGizmoColorSb.pData, gGizmoColors.data(), gGizmoColors.size() * sizeof(decltype(gGizmoColors)::value_type));
		gResources->context->Unmap(gResources->gizmoColorSB.Get(), 0);
	}

	auto lineGizmoVertexBufferSize{ gLineGizmoVertexBufferSize };

	while (lineGizmoVertexBufferSize < static_cast<int>(gLineGizmoVertexData.size())) {
		lineGizmoVertexBufferSize *= 2;
	}

	if (lineGizmoVertexBufferSize != gLineGizmoVertexBufferSize) {
		gLineGizmoVertexBufferSize = lineGizmoVertexBufferSize;
		RecreateLineGizmoVertexBuffer();
	}

	if (!gLineGizmoVertexData.empty()) {
		D3D11_MAPPED_SUBRESOURCE mappedLineGizmoVertexSb;
		gResources->context->Map(gResources->lineGizmoVertexSB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedLineGizmoVertexSb);
		std::memcpy(mappedLineGizmoVertexSb.pData, gLineGizmoVertexData.data(), gLineGizmoVertexData.size() * sizeof(decltype(gLineGizmoVertexData)::value_type));
		gResources->context->Unmap(gResources->lineGizmoVertexSB.Get(), 0);
	}

	gResources->context->PSSetShader(gResources->gizmoPS.Get(), nullptr, 0);
	gResources->context->PSSetShaderResources(RES_SLOT_GIZMO_COLOR, 1, gResources->gizmoColorSbSrv.GetAddressOf());

	if (!gLineGizmoVertexData.empty()) {
		gResources->context->VSSetShader(gResources->lineGizmoVS.Get(), nullptr, 0);
		gResources->context->VSSetShaderResources(RES_SLOT_LINE_GIZMO_VERTEX, 1, gResources->lineGizmoVertexSbSrv.GetAddressOf());
		gResources->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
		gResources->context->DrawInstanced(2, static_cast<UINT>(gLineGizmoVertexData.size()), 0, 0);
	}
}


auto ClearGizmoDrawQueue() noexcept {
	gGizmoColors.clear();
	gLineGizmoVertexData.clear();
}


auto DrawFullWithCameras(std::span<Camera const* const> const cameras, RenderTarget const& rt) -> void {
	FLOAT constexpr clearColor[]{ 0, 0, 0, 1 };
	gResources->context->ClearRenderTargetView(rt.GetHdrRtv(), clearColor);
	gResources->context->ClearDepthStencilView(rt.GetDsv(), D3D11_CLEAR_DEPTH, 1, 0);

	auto const aspectRatio{ static_cast<float>(rt.GetWidth()) / static_cast<float>(rt.GetHeight()) };

	D3D11_VIEWPORT const viewport{
		.TopLeftX = 0,
		.TopLeftY = 0,
		.Width = static_cast<FLOAT>(rt.GetWidth()),
		.Height = static_cast<FLOAT>(rt.GetHeight()),
		.MinDepth = 0,
		.MaxDepth = 1
	};

	gResources->context->PSSetSamplers(SAMPLER_SLOT_CMP_PCF, 1, gResources->ssCmpPcf.GetAddressOf());
	gResources->context->PSSetSamplers(SAMPLER_SLOT_CMP_POINT, 1, gResources->ssCmpPoint.GetAddressOf());
	gResources->context->PSSetSamplers(SAMPLER_SLOT_AF16, 1, gResources->ssAf16.GetAddressOf());
	gResources->context->PSSetSamplers(SAMPLER_SLOT_AF8, 1, gResources->ssAf8.GetAddressOf());
	gResources->context->PSSetSamplers(SAMPLER_SLOT_AF4, 1, gResources->ssAf4.GetAddressOf());
	gResources->context->PSSetSamplers(SAMPLER_SLOT_TRI, 1, gResources->ssTri.GetAddressOf());
	gResources->context->PSSetSamplers(SAMPLER_SLOT_BI, 1, gResources->ssBi.GetAddressOf());
	gResources->context->PSSetSamplers(SAMPLER_SLOT_POINT, 1, gResources->ssPoint.GetAddressOf());

	D3D11_MAPPED_SUBRESOURCE mappedPerFrameCB;
	if (FAILED(gResources->context->Map(gResources->perFrameCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedPerFrameCB))) {
		throw std::runtime_error{ "Failed to map per frame CB." };
	}

	auto const perFrameCbData{ static_cast<PerFrameCB*>(mappedPerFrameCB.pData) };
	perFrameCbData->gPerFrameConstants.shadowCascadeCount = gCascadeCount;
	perFrameCbData->gPerFrameConstants.visualizeShadowCascades = gVisualizeShadowCascades;
	perFrameCbData->gPerFrameConstants.shadowFilteringMode = static_cast<int>(gShadowFilteringMode);

	gResources->context->Unmap(gResources->perFrameCB.Get(), 0);

	gResources->context->VSSetConstantBuffers(CB_SLOT_PER_FRAME, 1, gResources->perFrameCB.GetAddressOf());
	gResources->context->PSSetConstantBuffers(CB_SLOT_PER_FRAME, 1, gResources->perFrameCB.GetAddressOf());

	for (auto const cam : cameras) {
		auto const camPos{ cam->GetPosition() };
		auto const camViewMtx{ cam->CalculateViewMatrix() };
		auto const camProjMtx{ cam->CalculateProjectionMatrix(aspectRatio) };
		auto const camViewProjMtx{ camViewMtx * camProjMtx };
		Frustum const camFrustWS{ camViewProjMtx };

		Visibility static visibility;
		CullLights(camFrustWS, visibility);

		auto const shadowCascadeBoundaries{ CalculateCameraShadowCascadeBoundaries(*cam) };

		for (auto const& shadowAtlas : gResources->shadowAtlases) {
			shadowAtlas->Update(gLights, visibility, *cam, camViewProjMtx, shadowCascadeBoundaries, aspectRatio);
		}

		ID3D11ShaderResourceView* const nullSrv{ nullptr };
		gResources->context->PSSetShaderResources(RES_SLOT_PUNCTUAL_SHADOW_ATLAS, 1, &nullSrv);
		gResources->context->PSSetShaderResources(RES_SLOT_DIR_SHADOW_ATLAS, 1, &nullSrv);
		gResources->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		for (auto const& shadowAtlas : gResources->shadowAtlases) {
			DrawShadowMaps(*shadowAtlas);
		}

		CullStaticMeshComponents(camFrustWS, visibility);

		gResources->context->VSSetShader(gResources->meshVS.Get(), nullptr, 0);

		D3D11_MAPPED_SUBRESOURCE mappedPerCamCB;
		if (FAILED(gResources->context->Map(gResources->perCamCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedPerCamCB))) {
			throw std::runtime_error{ "Failed to map per camera CB." };
		}

		auto const perCamCbData{ static_cast<PerCameraCB*>(mappedPerCamCB.pData) };
		perCamCbData->gPerCamConstants.viewProjMtx = camViewProjMtx;
		perCamCbData->gPerCamConstants.camPos = camPos;

		for (int i = 0; i < MAX_CASCADE_COUNT; i++) {
			perCamCbData->gPerCamConstants.shadowCascadeSplitDistances[i] = shadowCascadeBoundaries[i].farClip;
		}

		gResources->context->Unmap(gResources->perCamCB.Get(), 0);

		auto const lightCount{ std::ssize(visibility.lightIndices) };
		gResources->lightBuffer->Resize(static_cast<int>(lightCount));
		auto const lightBufferData{ gResources->lightBuffer->Map() };

		for (int i = 0; i < lightCount; i++) {
			lightBufferData[i].color = gLights[visibility.lightIndices[i]]->GetColor();
			lightBufferData[i].intensity = gLights[visibility.lightIndices[i]]->GetIntensity();
			lightBufferData[i].type = static_cast<int>(gLights[visibility.lightIndices[i]]->GetType());
			lightBufferData[i].direction = gLights[visibility.lightIndices[i]]->GetDirection();
			lightBufferData[i].isCastingShadow = FALSE;
			lightBufferData[i].range = gLights[visibility.lightIndices[i]]->GetRange();
			lightBufferData[i].halfInnerAngleCos = std::cos(ToRadians(gLights[visibility.lightIndices[i]]->GetInnerAngle() / 2.0f));
			lightBufferData[i].halfOuterAngleCos = std::cos(ToRadians(gLights[visibility.lightIndices[i]]->GetOuterAngle() / 2.0f));
			lightBufferData[i].position = gLights[visibility.lightIndices[i]]->GetEntity()->GetTransform().GetWorldPosition();
			lightBufferData[i].depthBias = gLights[visibility.lightIndices[i]]->GetShadowDepthBias();
			lightBufferData[i].normalBias = gLights[visibility.lightIndices[i]]->GetShadowNormalBias();

			for (auto& sample : lightBufferData[i].sampleShadowMap) {
				sample = FALSE;
			}
		}

		for (auto const& shadowAtlas : gResources->shadowAtlases) {
			shadowAtlas->SetLookUpInfo(lightBufferData);
		}

		gResources->lightBuffer->Unmap();

		gResources->context->VSSetShader(gResources->meshVS.Get(), nullptr, 0);
		gResources->context->VSSetConstantBuffers(CB_SLOT_PER_CAM, 1, gResources->perCamCB.GetAddressOf());

		gResources->context->OMSetRenderTargets(1, std::array{ rt.GetHdrRtv() }.data(), rt.GetDsv());
		gResources->context->OMSetDepthStencilState(nullptr, 0);

		gResources->context->PSSetShader(gResources->meshPbrPS.Get(), nullptr, 0);
		gResources->context->PSSetConstantBuffers(CB_SLOT_PER_CAM, 1, gResources->perCamCB.GetAddressOf());
		gResources->context->PSSetShaderResources(RES_SLOT_LIGHTS, 1, std::array{ gResources->lightBuffer->GetSrv() }.data());
		gResources->context->PSSetShaderResources(RES_SLOT_PUNCTUAL_SHADOW_ATLAS, 1, std::array{ gResources->shadowAtlases[PUNC_SHADOW_ATLAS_IDX]->GetSrv() }.data());
		gResources->context->PSSetShaderResources(RES_SLOT_DIR_SHADOW_ATLAS, 1, std::array{ gResources->shadowAtlases[DIR_SHADOW_ATLAS_IDX]->GetSrv() }.data());

		gResources->context->RSSetViewports(1, &viewport);
		gResources->context->RSSetState(nullptr);

		DrawMeshes(visibility.staticMeshIndices, true);
		DrawGizmos();
		DrawSkybox(camViewMtx, camProjMtx);
	}

	gResources->context->RSSetViewports(1, &viewport);
	DoToneMapGammaCorrectionStep(rt.GetHdrSrv(), rt.GetOutRtv());

	ClearGizmoDrawQueue();
}
}


auto Camera::GetNearClipPlane() const noexcept -> float {
	return mNear;
}


auto Camera::SetNearClipPlane(float const nearClipPlane) noexcept -> void {
	if (GetType() == Type::Perspective) {
		mNear = std::max(nearClipPlane, MINIMUM_PERSPECTIVE_NEAR_CLIP_PLANE);
		SetFarClipPlane(GetFarClipPlane());
	}
	else {
		mNear = nearClipPlane;
	}
}


auto Camera::GetFarClipPlane() const noexcept -> float {
	return mFar;
}


auto Camera::SetFarClipPlane(float const farClipPlane) noexcept -> void {
	if (GetType() == Type::Perspective) {
		mFar = std::max(farClipPlane, mNear + MINIMUM_PERSPECTIVE_FAR_CLIP_PLANE_OFFSET);
	}
	else {
		mFar = farClipPlane;
	}
}


auto Camera::GetType() const noexcept -> Type {
	return mType;
}


auto Camera::SetType(Type const type) noexcept -> void {
	mType = type;

	if (type == Type::Perspective) {
		SetNearClipPlane(GetNearClipPlane());
	}
}


auto Camera::GetHorizontalPerspectiveFov() const -> float {
	return mPerspFovHorizDeg;
}


auto Camera::SetHorizontalPerspectiveFov(float degrees) -> void {
	degrees = std::max(degrees, MINIMUM_PERSPECTIVE_HORIZONTAL_FOV);
	mPerspFovHorizDeg = degrees;
}


auto Camera::GetHorizontalOrthographicSize() const -> float {
	return mOrthoSizeHoriz;
}


auto Camera::SetHorizontalOrthographicSize(float size) -> void {
	size = std::max(size, MINIMUM_ORTHOGRAPHIC_HORIZONTAL_SIZE);
	mOrthoSizeHoriz = size;
}


auto Camera::CalculateViewMatrix() const noexcept -> Matrix4 {
	return Matrix4::LookToLH(GetPosition(), GetForwardAxis(), Vector3::Up());
}


auto Camera::CalculateProjectionMatrix(float const aspectRatio) const noexcept -> Matrix4 {
	switch (GetType()) {
	case Type::Perspective:
		return Matrix4::PerspectiveAsymZLH(ToRadians(Camera::HorizontalPerspectiveFovToVertical(GetHorizontalPerspectiveFov(), aspectRatio)), aspectRatio, GetNearClipPlane(), GetFarClipPlane());

	case Type::Orthographic:
		return Matrix4::OrthographicAsymZLH(GetHorizontalOrthographicSize(), GetHorizontalOrthographicSize() / aspectRatio, GetNearClipPlane(), GetFarClipPlane());
	}

	return Matrix4{};
}


auto Camera::HorizontalPerspectiveFovToVertical(float const fovDegrees, float const aspectRatio) noexcept -> float {
	return ToDegrees(2.0f * std::atan(std::tan(ToRadians(fovDegrees) / 2.0f) / aspectRatio));
}


auto Camera::VerticalPerspectiveFovToHorizontal(float const fovDegrees, float const aspectRatio) noexcept -> float {
	return ToDegrees(2.0f * std::atan(std::tan(ToRadians(fovDegrees) / 2.0f) * aspectRatio));
}


auto StartUp() -> void {
	gResources = new Resources{};

	CreateDeviceAndContext();

#ifndef NDEBUG
	SetDebugBreaks();
#endif

	ComPtr<IDXGIDevice> dxgiDevice;
	if (FAILED(gResources->device.As(&dxgiDevice))) {
		throw std::runtime_error{ "Failed to query IDXGIDevice interface." };
	}

	ComPtr<IDXGIAdapter> dxgiAdapter;
	if (FAILED(dxgiDevice->GetAdapter(dxgiAdapter.GetAddressOf()))) {
		throw std::runtime_error{ "Failed to get IDXGIAdapter." };
	}

	ComPtr<IDXGIFactory2> dxgiFactory2;
	if (FAILED(dxgiAdapter->GetParent(IID_PPV_ARGS(dxgiFactory2.GetAddressOf())))) {
		throw std::runtime_error{ "Failed to query IDXGIFactory2 interface." };
	}

	gResources->gameViewRenderTarget = std::make_unique<RenderTarget>(gResources->device, gWindow.GetCurrentClientAreaSize().width, gWindow.GetCurrentClientAreaSize().height);
	gResources->sceneViewRenderTarget = std::make_unique<RenderTarget>(gResources->device, gWindow.GetCurrentClientAreaSize().width, gWindow.GetCurrentClientAreaSize().height);

	gResources->swapChain = std::make_unique<SwapChain>(gResources->device, dxgiFactory2.Get());

	gResources->lightBuffer = std::make_unique<StructuredBuffer<ShaderLight>>(gResources->device, gResources->context);

	CreateInputLayouts();
	CreateShaders();
	CreateConstantBuffers();
	CreateRasterizerStates();
	CreateDepthStencilStates();
	CreateShadowAtlases();
	CreateSamplerStates();
	CreateDefaultAssets();
	CreateStructuredBuffers();

	gWindow.OnWindowSize.add_handler([](Extent2D<u32> const size) {
		gResources->swapChain->Resize(size.width, size.height);
	});

	dxgiFactory2->MakeWindowAssociation(gWindow.GetHandle(), DXGI_MWA_NO_WINDOW_CHANGES);
}


auto ShutDown() noexcept -> void {
	delete gResources;
	gResources = nullptr;
}


auto DrawGame() -> void {
	DrawFullWithCameras(gGameRenderCameras, *gResources->gameViewRenderTarget);
}


auto DrawSceneView(Camera const& cam) -> void {
	auto const camPtr{ &cam };
	DrawFullWithCameras(std::span{ &camPtr, 1 }, *gResources->sceneViewRenderTarget);
}


auto GetGameResolution() noexcept -> Extent2D<u32> {
	return { gResources->gameViewRenderTarget->GetWidth(), gResources->gameViewRenderTarget->GetHeight() };
}


auto SetGameResolution(Extent2D<u32> const resolution) noexcept -> void {
	gResources->gameViewRenderTarget->Resize(resolution.width, resolution.height);
}


auto GetSceneResolution() noexcept -> Extent2D<u32> {
	return { gResources->sceneViewRenderTarget->GetWidth(), gResources->sceneViewRenderTarget->GetHeight() };
}


auto SetSceneResolution(Extent2D<u32> const resolution) noexcept -> void {
	gResources->sceneViewRenderTarget->Resize(resolution.width, resolution.height);
}


auto GetGameFrame() noexcept -> ID3D11ShaderResourceView* {
	return gResources->gameViewRenderTarget->GetOutSrv();
}


auto GetSceneFrame() noexcept -> ID3D11ShaderResourceView* {
	return gResources->sceneViewRenderTarget->GetOutSrv();
}


auto GetGameAspectRatio() noexcept -> f32 {
	auto const& rt{ *gResources->gameViewRenderTarget };
	return static_cast<float>(rt.GetWidth()) / static_cast<float>(rt.GetHeight());
}


auto GetSceneAspectRatio() noexcept -> f32 {
	auto const& rt{ *gResources->sceneViewRenderTarget };
	return static_cast<float>(rt.GetWidth()) / static_cast<float>(rt.GetHeight());
}


auto BindAndClearSwapChain() noexcept -> void {
	FLOAT constexpr clearColor[]{ 0, 0, 0, 1 };
	auto const rtv{ gResources->swapChain->GetRtv() };
	gResources->context->ClearRenderTargetView(rtv, clearColor);
	gResources->context->OMSetRenderTargets(1, &rtv, nullptr);
}


auto Present() noexcept -> void {
	gResources->swapChain->Present(gSyncInterval);
}


auto GetSyncInterval() noexcept -> u32 {
	return gSyncInterval;
}


auto SetSyncInterval(u32 const interval) noexcept -> void {
	gSyncInterval = interval;
}


auto RegisterStaticMesh(StaticMeshComponent const* const staticMesh) -> void {
	gStaticMeshComponents.emplace_back(staticMesh);
}


auto UnregisterStaticMesh(StaticMeshComponent const* const staticMesh) -> void {
	std::erase(gStaticMeshComponents, staticMesh);
}


auto GetDevice() noexcept -> ID3D11Device* {
	return gResources->device.Get();
}


auto GetImmediateContext() noexcept -> ID3D11DeviceContext* {
	return gResources->context.Get();
}


auto RegisterLight(LightComponent const* light) -> void {
	gLights.emplace_back(light);
}


auto UnregisterLight(LightComponent const* light) -> void {
	std::erase(gLights, light);
}


auto GetDefaultMaterial() noexcept -> Material* {
	return gResources->defaultMaterial.get();
}


auto GetCubeMesh() noexcept -> Mesh* {
	return gResources->cubeMesh.get();
}


auto GetPlaneMesh() noexcept -> Mesh* {
	return gResources->planeMesh.get();
}


auto GetGamma() noexcept -> f32 {
	return 1.f / gInvGamma;
}


auto SetGamma(f32 const gamma) noexcept -> void {
	gInvGamma = 1.f / gamma;
}


auto RegisterSkybox(SkyboxComponent const* const skybox) -> void {
	gSkyboxes.emplace_back(skybox);
}


auto UnregisterSkybox(SkyboxComponent const* const skybox) -> void {
	std::erase(gSkyboxes, skybox);
}


auto RegisterGameCamera(Camera const& cam) -> void {
	gGameRenderCameras.emplace_back(&cam);
}


auto UnregisterGameCamera(Camera const& cam) -> void {
	std::erase(gGameRenderCameras, &cam);
}


auto CullLights(Frustum const& frustumWS, Visibility& visibility) -> void {
	visibility.lightIndices.clear();

	for (int lightIdx = 0; lightIdx < static_cast<int>(gLights.size()); lightIdx++) {
		switch (auto const light{ gLights[lightIdx] }; light->GetType()) {
		case LightComponent::Type::Directional: {
			visibility.lightIndices.emplace_back(lightIdx);
			break;
		}

		case LightComponent::Type::Spot: {
			auto const lightVerticesWS{
				[light] {
					auto vertices{ CalculateSpotLightLocalVertices(*light) };

					for (auto const modelMtxNoScale{ CalculateModelMatrixNoScale(light->GetEntity()->GetTransform()) };
					     auto& vertex : vertices) {
						vertex = Vector3{ Vector4{ vertex, 1 } * modelMtxNoScale };
					}

					return vertices;
				}()
			};

			if (frustumWS.Intersects(AABB::FromVertices(lightVerticesWS))) {
				visibility.lightIndices.emplace_back(lightIdx);
			}

			break;
		}

		case LightComponent::Type::Point: {
			BoundingSphere const boundsWS{
				.center = Vector3{ light->GetEntity()->GetTransform().GetWorldPosition() },
				.radius = light->GetRange()
			};

			if (frustumWS.Intersects(boundsWS)) {
				visibility.lightIndices.emplace_back(lightIdx);
			}
			break;
		}
		}
	}
}


auto CullStaticMeshComponents(Frustum const& frustumWS, Visibility& visibility) -> void {
	visibility.staticMeshIndices.clear();

	for (int i = 0; i < static_cast<int>(gStaticMeshComponents.size()); i++) {
		if (frustumWS.Intersects(gStaticMeshComponents[i]->CalculateBounds())) {
			visibility.staticMeshIndices.emplace_back(i);
		}
	}
}


auto DrawLineAtNextRender(Vector3 const& from, Vector3 const& to, Color const& color) -> void {
	gGizmoColors.emplace_back(color);
	gLineGizmoVertexData.emplace_back(from, static_cast<std::uint32_t>(gGizmoColors.size() - 1), to, 0.0f);
}


auto GetShadowCascadeCount() noexcept -> int {
	return gCascadeCount;
}


auto SetShadowCascadeCount(int const cascadeCount) noexcept -> void {
	gCascadeCount = std::clamp(cascadeCount, 1, MAX_CASCADE_COUNT);
	int const splitCount{ gCascadeCount - 1 };

	for (int i = 1; i < splitCount; i++) {
		gCascadeSplits[i] = std::max(gCascadeSplits[i - 1], gCascadeSplits[i]);
	}
}


auto GetMaxShadowCascadeCount() noexcept -> int {
	return MAX_CASCADE_COUNT;
}


auto GetNormalizedShadowCascadeSplits() noexcept -> std::span<float const> {
	return { std::begin(gCascadeSplits), static_cast<std::size_t>(gCascadeCount - 1) };
}


auto SetNormalizedShadowCascadeSplit(int const idx, float const split) noexcept -> void {
	auto const splitCount{ gCascadeCount - 1 };

	if (idx < 0 || idx >= splitCount) {
		return;
	}

	float const clampMin{ idx == 0 ? 0.0f : gCascadeSplits[idx - 1] };
	float const clampMax{ idx == splitCount - 1 ? 1.0f : gCascadeSplits[idx + 1] };

	gCascadeSplits[idx] = std::clamp(split, clampMin, clampMax);
}


auto GetShadowDistance() noexcept -> float {
	return gShadowDistance;
}


auto SetShadowDistance(float const shadowDistance) noexcept -> void {
	gShadowDistance = std::max(shadowDistance, 0.0f);
}


auto IsVisualizingShadowCascades() noexcept -> bool {
	return gVisualizeShadowCascades;
}


auto VisualizeShadowCascades(bool const visualize) noexcept -> void {
	gVisualizeShadowCascades = visualize;
}


auto IsUsingStableShadowCascadeProjection() noexcept -> bool {
	return gUseStableShadowCascadeProjection;
}


auto UseStableShadowCascadeProjection(bool const useStableProj) noexcept -> void {
	gUseStableShadowCascadeProjection = useStableProj;
}


auto GetShadowFilteringMode() noexcept -> ShadowFilteringMode {
	return gShadowFilteringMode;
}


auto SetShadowFilteringMode(ShadowFilteringMode const filteringMode) noexcept -> void {
	gShadowFilteringMode = filteringMode;
}
}
