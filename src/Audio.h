#pragma once

namespace fr::Audio
{
	enum class Bank
	{
		kChip,    // a chip put down or picked up
		kRattle,  // the ball hitting a diamond or a fret
		kSettle,  // the ball dropping into its pocket
		kRoll,    // the ball running round the track
		kSpin,    // the rotor turned by hand
		kWin,     // a winning bet paid (HUD sound)
		kCount
	};

	// Loads Data\Sound\fo4roulette\*.wav (<bank>_<n>.wav, mono 16-bit 44.1 kHz) and starts XAudio2. Safe to call more
	// than once.
	void Init();

	// Plays one of the bank's sounds at a_pos in the world, quieter with distance from the camera. a_gain 0..1.
	// a_seconds > 0 plays only that much of it, fading out at the end.
	void Play(Bank a_bank, const RE::NiPoint3& a_pos, float a_gain, float a_seconds = 0.0f);

	// At the listener: no position.
	void PlayUI(Bank a_bank, float a_gain);

	// Which way the listener faces (radians, the game's yaw). Set each frame at the table.
	void SetListenerYaw(float a_yaw, bool a_valid);
}
