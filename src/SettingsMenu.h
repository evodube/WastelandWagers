#pragma once

namespace fr::SettingsMenu
{
	// Adds the fo4roulette pages to F4SE Menu Framework's menu (if it's installed). Safe to call more than once.
	void Install();

	// A key's name for showing on screen ("F7", "Numpad 7").
	std::string KeyName(int a_vk);
}
