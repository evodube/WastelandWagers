#pragma once

namespace fr::Audio
{
	enum class Bank
	{
		kChip,    // a chip put down or picked up
		kRattle,  // the ball hitting a diamond or a fret hard
		kSettle,  // the ball rocking into place in its pocket
		kTick,    // the ball's small knocks: bouncing about the rotor and the pockets
		kRoll,    // the ball running round the track
		kSpin,    // the rotor turned by hand
		kWin,     // (unused since 0.2.3: a win is the caps' own clink)
		kCard,    // a card landing on the felt
		kSlide,   // cards or chips slid across the felt
		kShuffle, // the deck shuffled between hands
		kTurn,    // your turn at a card table (a chime)
		kSlotPull,  // a slot machine's lever (the Vault-Tec Workshop machine's own sounds)
		kSlotReel,  // ...its reels running
		kSlotStop,  // ...a reel stopping
		kSlotWin,   // ...a win
		kCount
	};

	// Loads Data\Sound\WastelandWagers\*.wav (<bank>_<n>.wav, mono 16-bit 44.1 kHz) and starts XAudio2. Safe to call more
	// than once.
	void Init();

	// how far a table's sounds carry (game units, about 70 a metre): the poker table's five metres, roulette's ten
	inline constexpr float kNear = 350.0f;
	inline constexpr float kFar = 700.0f;

	// Plays one of the bank's sounds at a_pos in the world, quieter with distance from the camera (silent past a_range:
	// about five metres unless said). a_gain 0..1.
	// a_seconds > 0 plays only that much of it, fading out at the end.
	void Play(Bank a_bank, const RE::NiPoint3& a_pos, float a_gain, float a_seconds = 0.0f, float a_range = kNear);

	// As Play, with the loudness following a_keys over time: (seconds from the start, gain 0..1) pairs, in order,
	// joined by straight lines; the sound stops at the last one.
	void PlayShaped(Bank a_bank, const RE::NiPoint3& a_pos, float a_gain, const std::vector<std::pair<float, float>>& a_keys,
		float a_range = kNear);

	// At the listener: no position.
	void PlayUI(Bank a_bank, float a_gain);

	// Which way the listener faces (radians, the game's yaw). Set each frame at the table.
	void SetListenerYaw(float a_yaw, bool a_valid);
}
