#include "GUI.hpp"

#include <memory>


namespace sorcery {
auto SetImGuiContext(ImGuiContext& ctx) -> void {
  ImGui::SetCurrentContext(std::addressof(ctx));
}


auto detail::ObjectPickerBase::GetNextInstanceId() noexcept -> int {
  return sNextInstanceId++;
}


namespace detail {
int ObjectPickerBase::sNextInstanceId{0};
}
}
