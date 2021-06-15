#pragma once

#include <string>
#include "../input/cursorstate.h"

namespace leopph::impl
{
	class Window
	{
	public:
		static Window& Get(unsigned width = 1280u, unsigned height = 720u,
			const std::string& title = "Window", bool fullscreen = false);
		static void Destroy();

		virtual unsigned Width() const;
		virtual void Width(unsigned newWidth);

		virtual unsigned Height() const;
		virtual void Height(unsigned newHeight);

		float AspectRatio() const;

		bool Fullscreen() const;

		virtual void PollEvents() = 0;
		virtual void SwapBuffers() = 0;
		virtual bool ShouldClose() = 0;
		virtual void Clear() = 0;

		virtual CursorState CursorMode() const = 0;
		virtual void CursorMode(CursorState newState) = 0;

	protected:
		Window(unsigned width, unsigned height,
			const std::string& title, bool fullscreen);
		virtual ~Window() = default;
		
	private:
		virtual void InitKeys() = 0;
		
		static Window* s_Instance;

		unsigned m_Width;
		unsigned m_Height;
		std::string m_Title;
		bool m_Fullscreen; 
	};
}