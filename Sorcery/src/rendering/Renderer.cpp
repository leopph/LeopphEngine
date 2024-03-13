#include "Renderer.hpp"

#include "renderer_impl.hpp"

#include <cassert>


namespace sorcery {
Renderer gRenderer;


auto Renderer::GetProjectionMatrixForRendering(Matrix4 const& proj_mtx) noexcept -> Matrix4 {
  return Impl::GetProjectionMatrixForRendering(proj_mtx);
}


auto Renderer::StartUp() -> void {
  mImpl = new Impl{};
  mImpl->StartUp();
}


auto Renderer::ShutDown() -> void {
  mImpl->ShutDown();
  delete mImpl;
  mImpl = nullptr;
}


auto Renderer::Render() -> void {
  assert(mImpl);
  mImpl->Render();
}


auto Renderer::DrawLineAtNextRender(Vector3 const& from, Vector3 const& to, Color const& color) -> void {
  assert(mImpl);
  mImpl->DrawLineAtNextRender(from, to, color);
}


auto Renderer::DrawGizmos(RenderTarget const* const rt) -> void {
  assert(mImpl);
  mImpl->DrawGizmos(rt);
}


/*auto Renderer::ClearAndBindMainRt(ObserverPtr<ID3D11DeviceContext> const ctx) const noexcept -> void {
  assert(mImpl);
  mImpl->ClearAndBindMainRt(ctx);
}


auto Renderer::BlitMainRtToSwapChain(ObserverPtr<ID3D11DeviceContext> const ctx) const noexcept -> void {
  assert(mImpl);
  mImpl->BlitMainRtToSwapChain(ctx);
}*/


auto Renderer::Present() noexcept -> void {
  assert(mImpl);
  mImpl->Present();
}


auto Renderer::LoadReadonlyTexture(
  DirectX::ScratchImage const& img) -> graphics::SharedDeviceChildHandle<graphics::Texture> {
  assert(mImpl);
  return mImpl->LoadReadonlyTexture(img);
}


auto Renderer::UpdateBuffer(graphics::Buffer const& buf, std::span<std::uint8_t const> const data) -> bool {
  assert(mImpl);
  return mImpl->UpdateBuffer(buf, data);
}


auto Renderer::GetDevice() const noexcept -> graphics::GraphicsDevice* {
  assert(mImpl);
  return mImpl->GetDevice();
}


auto Renderer::GetTemporaryRenderTarget(RenderTarget::Desc const& desc) -> RenderTarget& {
  assert(mImpl);
  return mImpl->GetTemporaryRenderTarget(desc);
}


auto Renderer::GetDefaultMaterial() const noexcept -> ObserverPtr<Material> {
  assert(mImpl);
  return mImpl->GetDefaultMaterial();
}


auto Renderer::GetCubeMesh() const noexcept -> ObserverPtr<Mesh> {
  assert(mImpl);
  return mImpl->GetCubeMesh();
}


auto Renderer::GetPlaneMesh() const noexcept -> ObserverPtr<Mesh> {
  assert(mImpl);
  return mImpl->GetPlaneMesh();
}


auto Renderer::GetSphereMesh() const noexcept -> ObserverPtr<Mesh> {
  assert(mImpl);
  return mImpl->GetSphereMesh();
}


auto Renderer::GetSyncInterval() const noexcept -> int {
  assert(mImpl);
  return mImpl->GetSyncInterval();
}


auto Renderer::SetSyncInterval(int interval) noexcept -> void {
  assert(mImpl);
  mImpl->SetSyncInterval(interval);
}


auto Renderer::GetMultisamplingMode() const noexcept -> MultisamplingMode {
  assert(mImpl);
  return mImpl->GetMultisamplingMode();
}


auto Renderer::SetMultisamplingMode(MultisamplingMode const mode) noexcept -> void {
  assert(mImpl);
  mImpl->SetMultisamplingMode(mode);
}


auto Renderer::IsDepthNormalPrePassEnabled() const noexcept -> bool {
  return mImpl->IsDepthNormalPrePassEnabled();
}


auto Renderer::SetDepthNormalPrePassEnabled(bool const enabled) noexcept -> void {
  mImpl->SetDepthNormalPrePassEnabled(enabled);
}


auto Renderer::IsUsingPreciseColorFormat() const noexcept -> bool {
  assert(mImpl);
  return mImpl->IsUsingPreciseColorFormat();
}


auto Renderer::SetUsePreciseColorFormat(bool const value) noexcept -> void {
  assert(mImpl);
  mImpl->SetUsePreciseColorFormat(value);
}


auto Renderer::GetShadowDistance() const noexcept -> float {
  assert(mImpl);
  return mImpl->GetShadowDistance();
}


auto Renderer::SetShadowDistance(float const shadowDistance) noexcept -> void {
  assert(mImpl);
  mImpl->SetShadowDistance(shadowDistance);
}


auto Renderer::GetMaxShadowCascadeCount() noexcept -> int {
  return Impl::GetMaxShadowCascadeCount();
}


auto Renderer::GetShadowCascadeCount() const noexcept -> int {
  assert(mImpl);
  return mImpl->GetShadowCascadeCount();
}


auto Renderer::SetShadowCascadeCount(int const cascadeCount) noexcept -> void {
  assert(mImpl);
  mImpl->SetShadowCascadeCount(cascadeCount);
}


auto Renderer::GetNormalizedShadowCascadeSplits() const noexcept -> std::span<float const> {
  assert(mImpl);
  return mImpl->GetNormalizedShadowCascadeSplits();
}


auto Renderer::SetNormalizedShadowCascadeSplit(int const idx, float const split) noexcept -> void {
  assert(mImpl);
  mImpl->SetNormalizedShadowCascadeSplit(idx, split);
}


auto Renderer::IsVisualizingShadowCascades() const noexcept -> bool {
  assert(mImpl);
  return mImpl->IsVisualizingShadowCascades();
}


auto Renderer::VisualizeShadowCascades(bool visualize) noexcept -> void {
  assert(mImpl);
  mImpl->VisualizeShadowCascades(visualize);
}


auto Renderer::GetShadowFilteringMode() const noexcept -> ShadowFilteringMode {
  assert(mImpl);
  return mImpl->GetShadowFilteringMode();
}


auto Renderer::SetShadowFilteringMode(ShadowFilteringMode filteringMode) noexcept -> void {
  assert(mImpl);
  mImpl->SetShadowFilteringMode(filteringMode);
}


auto Renderer::IsSsaoEnabled() const noexcept -> bool {
  assert(mImpl);
  return mImpl->IsSsaoEnabled();
}


auto Renderer::SetSsaoEnabled(bool const enabled) noexcept -> void {
  assert(mImpl);
  mImpl->SetSsaoEnabled(enabled);
}


auto Renderer::GetSsaoParams() const noexcept -> SsaoParams const& {
  assert(mImpl);
  return mImpl->GetSsaoParams();
}


auto Renderer::SetSsaoParams(SsaoParams const& ssaoParams) noexcept -> void {
  assert(mImpl);
  mImpl->SetSsaoParams(ssaoParams);
}


auto Renderer::GetGamma() const noexcept -> float {
  assert(mImpl);
  return mImpl->GetGamma();
}


auto Renderer::SetGamma(float gamma) noexcept -> void {
  assert(mImpl);
  mImpl->SetGamma(gamma);
}


auto Renderer::Register(StaticMeshComponent const& staticMeshComponent) noexcept -> void {
  assert(mImpl);
  mImpl->Register(staticMeshComponent);
}


auto Renderer::Unregister(StaticMeshComponent const& staticMeshComponent) noexcept -> void {
  assert(mImpl);
  mImpl->Unregister(staticMeshComponent);
}


auto Renderer::Register(LightComponent const& lightComponent) noexcept -> void {
  assert(mImpl);
  mImpl->Register(lightComponent);
}


auto Renderer::Unregister(LightComponent const& lightComponent) noexcept -> void {
  assert(mImpl);
  mImpl->Unregister(lightComponent);
}


auto Renderer::Register(Camera const& cam) noexcept -> void {
  assert(mImpl);
  mImpl->Register(cam);
}


auto Renderer::Unregister(Camera const& cam) noexcept -> void {
  assert(mImpl);
  mImpl->Unregister(cam);
}
}
