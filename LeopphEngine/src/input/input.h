#pragma once

#include <map>
#include <utility>

#include "../api/leopphapi.h"
#include "keycodes.h"
#include "keystate.h"
#include "cursorstate.h"


namespace leopph
{
	namespace impl
	{
		class InputHandler;
	}
	
	/*--------------------------------------------------------------------
	The Input class provides ways to gather information about user inputs.
	--------------------------------------------------------------------*/

	class Input
	{
	public:
		/* Internally used functions */
		// TODO these should not be accessible to clients
		LEOPPHAPI static void UpdateReleasedKeys();

		/* Returns true if the given key is being pressed down in the current frame.
		Returns true for held keys. */
		LEOPPHAPI static bool GetKey(KeyCode key);

		/* Returns true if the given key was pressed in the current frame. 
		Returns false for held keys. */
		LEOPPHAPI static bool GetKeyDown(KeyCode key);

		/* Returns true if the given key was released in this frame.
		Returns false for untouched keys. */
		LEOPPHAPI static bool GetKeyUp(KeyCode key);

		/* Returns an (x, y) pair of mouse coordinates */
		LEOPPHAPI static const std::pair<float, float>& GetMousePosition();

		/* Determines whether the Cursor is Shown, Hidden, or Disabled */
		LEOPPHAPI static CursorState CursorMode();
		LEOPPHAPI static void CursorMode(CursorState newState);

	private:
		static std::map<KeyCode, KeyState> s_KeyStates;
		static std::pair<float, float> s_MousePos;

		static void OnInputChange(KeyCode, KeyState);
		static void OnInputChange(double x, double y);

		friend class impl::InputHandler;
	};
}