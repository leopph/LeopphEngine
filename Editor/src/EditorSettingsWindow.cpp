#include "EditorSettingsWindow.hpp"

#include "Timing.hpp"

#include <imgui.h>

#include <limits>


namespace leopph::editor {
std::string_view const EDITOR_SETTINGS_WINDOW_TITLE{ "Editor Settings" };


auto DrawEditorSettingsWindow(bool& isOpen) -> void {
	ImGui::SetNextWindowSizeConstraints(ImVec2{ 200, 200 }, ImVec2{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max() });
	if (std::string const static windowName{ std::string{ EDITOR_SETTINGS_WINDOW_TITLE } + "##Window" }; !ImGui::Begin(windowName.data(), &isOpen)) {
		ImGui::End();
		return;
	}

	ImGui::Text("%s", "Frame Rate Limit");
	ImGui::SameLine();

	bool isFrameRateLimited{ timing::GetTargetFrameRate() != -1 };
	if (ImGui::Checkbox("##FrameRateLimitCheckbox", &isFrameRateLimited)) {
		timing::SetTargetFrameRate(isFrameRateLimited ? DEFAULT_TARGET_FRAME_RATE : -1);
	}

	if (isFrameRateLimited) {
		ImGui::Text("%s", "Target Frame Rate");
		ImGui::SameLine();

		int targetFrameRate{ timing::GetTargetFrameRate() };
		if (ImGui::DragInt("##TargetFrameRateWidget", &targetFrameRate, 1, 30, std::numeric_limits<int>::max(), "%d", ImGuiSliderFlags_AlwaysClamp)) {
			timing::SetTargetFrameRate(targetFrameRate);
		}
	}

	ImGui::End();
}
}
