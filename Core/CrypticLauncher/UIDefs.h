#pragma once
typedef enum PatchButtonState
{
	CL_BUTTONSTATE_DISABLED, // Maps to do_set_button_state('disabled')
	CL_BUTTONSTATE_PATCH, // 'patch'
	CL_BUTTONSTATE_PLAY, // 'play'
	CL_BUTTONSTATE_CANCEL, // 'cancel'
}
PatchButtonState;

// Enum values for window types.
enum 
{
	CL_WINDOW_MAIN,
	CL_WINDOW_OPTIONS,
	CL_WINDOW_AUTOPATCH,
	CL_WINDOW_XFERS,
};


