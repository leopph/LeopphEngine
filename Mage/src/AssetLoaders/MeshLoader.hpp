#pragma once

#include "AssetLoader.hpp"


namespace sorcery::mage {
class MeshLoader : public AssetLoader {
  class Impl;
  Impl* mImpl;

public:
  MeshLoader();
  ~MeshLoader() override;

  [[nodiscard]] auto GetSupportedExtensions() const -> std::span<std::string const> override;
  [[nodiscard]] auto Load(std::filesystem::path const& src, std::filesystem::path const& cache) -> std::unique_ptr<Resource> override;
  [[nodiscard]] auto GetPrecedence() const noexcept -> int override;
};
}
