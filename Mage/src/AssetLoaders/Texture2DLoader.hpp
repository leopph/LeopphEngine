#pragma once

#include "AssetLoader.hpp"


namespace sorcery::mage {
class Texture2DLoader : public AssetLoader {
public:
  [[nodiscard]] auto GetSupportedExtensions() const -> std::span<std::string const> override;
  [[nodiscard]] auto Load(std::filesystem::path const& src, [[maybe_unused]] std::filesystem::path const& cache) -> std::unique_ptr<Resource> override;
  [[nodiscard]] auto GetPrecedence() const noexcept -> int override;
};
}
