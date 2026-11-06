#pragma once

namespace fr::CameraHook
{
	// Hooks FreeCameraState::Update and ::GetTranslation (see CameraHook.cpp). Safe to call more than once.
	void Install();
}
