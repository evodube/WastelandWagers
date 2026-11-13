// The HUD, drawn through F4SE Menu Framework when it's installed, in Playable Pool's green terminal style: your caps
// and chip, the dealer and the players, the bet under the cursor, the last numbers, what's been happening, and the
// big banners (no more bets, the result). Text uses the two pre-rendered fonts in Data\F4SE\Plugins\fo4roulette\hud
// (HudFont.h); the panels are drawn shapes. Laid out on a 1280x720 design screen, scaled to the real one.

#include "Hud.h"

#include "F4SEMenuFramework.h"
#include "Game.h"
#include "HudFont.h"
#include "Settings.h"
#include "SettingsMenu.h"
#include "rl/Wheel.h"

namespace fr::Hud
{
	namespace
	{
		using namespace ImGuiMCP;
		namespace DL = ImGuiMCP::ImDrawListManager;

		constexpr ImU32 Rgba(int r, int g, int b, float a = 1.0f)
		{
			return static_cast<ImU32>(r) | (static_cast<ImU32>(g) << 8) | (static_cast<ImU32>(b) << 16) |
			       (static_cast<ImU32>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f) << 24);
		}
		ImU32 Fade(ImU32 a_col, float a_alpha)
		{
			const float a = static_cast<float>(a_col >> 24) / 255.0f * std::clamp(a_alpha, 0.0f, 1.0f);
			return (a_col & 0x00FFFFFFu) | (static_cast<ImU32>(a * 255.0f + 0.5f) << 24);
		}

		constexpr ImU32 kBright = Rgba(166, 255, 166);
		constexpr ImU32 kText = Rgba(134, 255, 134);
		constexpr ImU32 kDim = Rgba(95, 174, 102);
		constexpr ImU32 kOff = Rgba(79, 143, 86);
		constexpr ImU32 kGlow = Rgba(80, 255, 110, 0.55f);
		constexpr ImU32 kRedText = Rgba(255, 122, 110);
		constexpr ImU32 kRedGlow = Rgba(255, 60, 40, 0.6f);
		constexpr ImU32 kPanel = Rgba(6, 20, 9, 0.78f);
		constexpr ImU32 kEdge = Rgba(80, 200, 100, 0.55f);
		constexpr ImU32 kRedChip = Rgba(178, 22, 30);
		constexpr ImU32 kBlackChip = Rgba(28, 26, 26);
		constexpr ImU32 kGreenChip = Rgba(18, 112, 40);

		// ------------------------------------------------------------ fonts

		struct Tex
		{
			ImTextureID id{ nullptr };
			bool        tried{ false };
		};
		std::unordered_map<std::string, Tex> g_textures;
		bool                                 g_artMissing{ false };

		ImTextureID Texture(const char* a_file)
		{
			auto& t = g_textures[a_file];
			if (!t.tried) {
				t.tried = true;
				for (const char* dir : { "Data\\F4SE\\Plugins\\fo4roulette\\hud\\", "F4SE\\Plugins\\fo4roulette\\hud\\" }) {
					t.id = F4SEMenuFramework::LoadTexture(std::string(dir) + a_file);
					if (t.id) {
						break;
					}
				}
				if (!t.id) {
					g_artMissing = true;
					log::warn("HUD: could not load {}; the plain HUD is used", a_file);
				}
			}
			return t.id;
		}

		struct View
		{
			ImDrawList* dl;
			ImVec2      origin;
			float       s;
			float       w, h;

			ImVec2 P(float a_x, float a_y) const { return { origin.x + a_x * s, origin.y + a_y * s }; }
		};

		const HudFont::Glyph* FindGlyph(const HudFont::Font& a_f, std::uint32_t a_code)
		{
			for (int i = 0; i < a_f.count; ++i) {
				if (a_f.glyphs[i].code == a_code) {
					return &a_f.glyphs[i];
				}
			}
			return a_code != '?' ? FindGlyph(a_f, '?') : nullptr;
		}

		std::vector<std::uint32_t> Decode(std::string_view a_text)
		{
			std::vector<std::uint32_t> out;
			for (std::size_t i = 0; i < a_text.size();) {
				const auto    c = static_cast<unsigned char>(a_text[i]);
				std::uint32_t cp = c;
				int           extra = 0;
				if (c >= 0xF0) {
					cp = c & 0x07;
					extra = 3;
				} else if (c >= 0xE0) {
					cp = c & 0x0F;
					extra = 2;
				} else if (c >= 0xC0) {
					cp = c & 0x1F;
					extra = 1;
				}
				++i;
				for (int k = 0; k < extra && i < a_text.size(); ++k, ++i) {
					cp = (cp << 6) | (static_cast<unsigned char>(a_text[i]) & 0x3F);
				}
				out.push_back(cp);
			}
			return out;
		}

		float TextWidth(const HudFont::Font& a_f, std::string_view a_text, float a_size, float a_spacing)
		{
			const float k = a_size / a_f.size;
			float       w = 0.0f;
			const auto  codes = Decode(a_text);
			for (std::size_t i = 0; i < codes.size(); ++i) {
				if (const auto* g = FindGlyph(a_f, codes[i])) {
					w += g->advance * k + (i + 1 < codes.size() ? a_spacing : 0.0f);
				}
			}
			return w;
		}

		enum class Align
		{
			kLeft,
			kCentre,
			kRight
		};

		void Print(const View& a_v, const HudFont::Font& a_f, std::string_view a_text, float a_x, float a_y, float a_size, ImU32 a_col,
			ImU32 a_glow = 0, float a_spacing = 0.0f, Align a_align = Align::kLeft)
		{
			auto sharp = Texture(a_f.file);
			auto glow = (a_glow >> 24) ? Texture(a_f.glowFile) : nullptr;
			if (!sharp) {
				return;
			}
			const float k = a_size / a_f.size;
			const float width = TextWidth(a_f, a_text, a_size, a_spacing);
			float       x = a_align == Align::kLeft ? a_x : a_align == Align::kCentre ? a_x - width * 0.5f : a_x - width;
			for (const auto code : Decode(a_text)) {
				const auto* g = FindGlyph(a_f, code);
				if (!g) {
					continue;
				}
				if (code != ' ') {
					const ImVec2 p0 = a_v.P(x + g->xoff * k, a_y + g->yoff * k);
					const ImVec2 p1 = a_v.P(x + (g->xoff + g->w) * k, a_y + (g->yoff + g->h) * k);
					const ImVec2 uv0{ g->x / a_f.atlasW, g->y / a_f.atlasH };
					const ImVec2 uv1{ (g->x + g->w) / a_f.atlasW, (g->y + g->h) / a_f.atlasH };
					if (glow) {
						DL::AddImage(a_v.dl, glow, p0, p1, uv0, uv1, a_glow);
					}
					DL::AddImage(a_v.dl, sharp, p0, p1, uv0, uv1, a_col);
				}
				x += g->advance * k + a_spacing;
			}
		}

		// text centred on (a_cx, a_cy) by its ink, not its advance and line height: digits sit dead centre in a disc
		void PrintInkCentred(const View& a_v, const HudFont::Font& a_f, std::string_view a_text, float a_cx, float a_cy, float a_size, ImU32 a_col)
		{
			const float k = a_size / a_f.size;
			const auto* space = FindGlyph(a_f, ' ');
			const float pad = space ? -space->xoff : 0.0f;  // the atlas's padding round each glyph (for the glow)
			float       pen = 0.0f, l = 1.0e9f, r = -1.0e9f, t = 1.0e9f, b = -1.0e9f;
			for (const auto code : Decode(a_text)) {
				const auto* g = FindGlyph(a_f, code);
				if (!g) {
					continue;
				}
				if (code != ' ') {
					l = std::min(l, pen + (g->xoff + pad) * k);
					r = std::max(r, pen + (g->xoff + g->w - pad) * k);
					t = std::min(t, (g->yoff + pad) * k);
					b = std::max(b, (g->yoff + g->h - pad) * k);
				}
				pen += g->advance * k;
			}
			if (r < l) {
				return;
			}
			Print(a_v, a_f, a_text, a_cx - (l + r) * 0.5f, a_cy - (t + b) * 0.5f, a_size, a_col);
		}

		const HudFont::Font& Title() { return HudFont::k_title; }
		const HudFont::Font& Mono() { return HudFont::k_mono; }

		// ------------------------------------------------------------ shapes

		void Chamfer(const View& a_v, float a_x, float a_y, float a_w, float a_h, float a_c, ImU32 a_fill, ImU32 a_edge)
		{
			const std::array<ImVec2, 8> pts{ a_v.P(a_x + a_c, a_y), a_v.P(a_x + a_w - a_c, a_y), a_v.P(a_x + a_w, a_y + a_c),
				a_v.P(a_x + a_w, a_y + a_h - a_c), a_v.P(a_x + a_w - a_c, a_y + a_h), a_v.P(a_x + a_c, a_y + a_h), a_v.P(a_x, a_y + a_h - a_c),
				a_v.P(a_x, a_y + a_c) };
			DL::AddConvexPolyFilled(a_v.dl, pts.data(), static_cast<int>(pts.size()), a_fill);
			if (a_edge >> 24) {
				DL::AddPolyline(a_v.dl, pts.data(), static_cast<int>(pts.size()), a_edge, ImDrawFlags_Closed, std::max(1.0f, 1.2f * a_v.s));
			}
		}

		ImU32 NumberColour(int a_n)
		{
			switch (rl::ColourOf(a_n)) {
			case rl::Colour::kRed: return kRedChip;
			case rl::Colour::kBlack: return kBlackChip;
			default: return kGreenChip;
			}
		}

		// a number in its colour's disc
		void NumberDisc(const View& a_v, int a_n, float a_cx, float a_cy, float a_r, float a_alpha = 1.0f)
		{
			DL::AddCircleFilled(a_v.dl, a_v.P(a_cx, a_cy), a_r * a_v.s, Fade(NumberColour(a_n), a_alpha), 32);
			DL::AddCircle(a_v.dl, a_v.P(a_cx, a_cy), a_r * a_v.s, Fade(Rgba(230, 220, 190), 0.8f * a_alpha), 32, std::max(1.0f, 1.2f * a_v.s));
			PrintInkCentred(a_v, Title(), rl::NumberName(a_n), a_cx, a_cy, a_r * 1.15f, Fade(Rgba(245, 240, 225), a_alpha));
		}

		void Chip(const View& a_v, int a_index, float a_cx, float a_cy, float a_r, bool a_on)
		{
			static constexpr std::array<ImU32, 4> fill{ Rgba(226, 222, 210), Rgba(172, 28, 34), Rgba(28, 120, 52), Rgba(24, 24, 26) };
			static constexpr std::array<const char*, 4> label{ "1", "5", "25", "100" };
			const float a = a_on ? 1.0f : 0.45f;
			DL::AddCircleFilled(a_v.dl, a_v.P(a_cx, a_cy), a_r * a_v.s, Fade(fill[a_index], a), 32);
			DL::AddCircle(a_v.dl, a_v.P(a_cx, a_cy), a_r * 0.78f * a_v.s, Fade(Rgba(236, 232, 222), 0.8f * a), 32, std::max(1.0f, 1.5f * a_v.s));
			Print(a_v, Mono(), label[a_index], a_cx, a_cy - a_r * 0.55f, a_r * 0.95f, Fade(a_index == 0 ? Rgba(20, 20, 20) : Rgba(240, 236, 226), a), 0,
				0.0f, Align::kCentre);
			if (a_on) {
				DL::AddCircle(a_v.dl, a_v.P(a_cx, a_cy), (a_r + 4.0f) * a_v.s, kBright, 32, std::max(1.5f, 2.0f * a_v.s));
			}
		}

		std::string Commas(int a_n)
		{
			std::string s = std::to_string(std::abs(a_n));
			for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) {
				s.insert(static_cast<std::size_t>(i), ",");
			}
			return a_n < 0 ? "-" + s : s;
		}

		// ------------------------------------------------------------ the pieces

		void Status(const View& a_v, const Game::HudState& a_st)
		{
			const float x = 24.0f, y = 24.0f, w = 300.0f;
			const float h = 176.0f + 20.0f * static_cast<float>(a_st.players.size());
			Chamfer(a_v, x, y, w, h, 12.0f, kPanel, kEdge);
			Print(a_v, Mono(), a_st.american ? "ROULETTE · AMERICAN" : "ROULETTE · EUROPEAN", x + 18, y + 12, 13, kDim, 0, 2.0f);
			Print(a_v, Title(), Commas(a_st.caps), x + 18, y + 30, 34, kBright, kGlow, 2.0f);
			Print(a_v, Mono(), "CAPS", x + 22 + TextWidth(Title(), Commas(a_st.caps), 34, 2.0f), y + 48, 13, kDim, 0, 2.0f);
			Print(a_v, Mono(), a_st.onTable > 0 ? std::format("{} ON THE TABLE", Commas(a_st.onTable)) : std::string("NO BETS DOWN"), x + 18, y + 76, 13,
				kText, 0, 1.0f);
			for (int i = 0; i < 4; ++i) {
				Chip(a_v, i, x + 40 + i * 62.0f, y + 120, 20, i == a_st.chip);
			}
			float row = y + 152;
			const std::string when = a_st.spinning ? std::string("NO MORE BETS")
			                         : a_st.spinIn < 0.0f ? std::string("PLACE A BET · THE WHEEL SPINS AFTER")
			                                              : std::format("WHEEL SPINS IN {}", static_cast<int>(std::ceil(a_st.spinIn)));
			Print(a_v, Mono(), when, x + 18, row, 12, a_st.spinIn >= 0.0f && a_st.spinIn < 5.0f ? kBright : kDim, 0, 1.0f);
			for (const auto& p : a_st.players) {
				row += 20.0f;
				Print(a_v, Mono(), p, x + 18, row, 12, kOff, 0, 1.0f);
			}
		}

		void History(const View& a_v, const Game::HudState& a_st)
		{
			if (a_st.history.empty()) {
				return;
			}
			// the last five, newest first, all the same size, the row centred in the panel
			constexpr std::size_t kShown = 5;
			const std::size_t     n = std::min(a_st.history.size(), kShown);
			const float           r = 17.0f, gap = 42.0f;
			const float           row = 2.0f * r + gap * static_cast<float>(kShown - 1);
			const float           w = std::max(row, TextWidth(Mono(), "LAST NUMBERS", 13, 2.0f)) + 36.0f;
			const float           x = a_v.w - 24.0f - w, y = 24.0f;
			Chamfer(a_v, x, y, w, 80.0f, 10.0f, kPanel, kEdge);
			Print(a_v, Mono(), "LAST NUMBERS", x + w * 0.5f, y + 10, 13, kDim, 0, 2.0f, Align::kCentre);
			const float x0 = x + (w - row) * 0.5f + r;
			for (std::size_t i = 0; i < n; ++i) {
				NumberDisc(a_v, a_st.history[i], x0 + gap * static_cast<float>(i), y + 50, r, i == 0 ? 1.0f : 0.85f);
			}
		}

		// what's been happening, in a panel at the bottom left
		void Log(const View& a_v, const Game::HudState& a_st, float a_bottom)
		{
			if (a_st.log.empty()) {
				return;
			}
			constexpr float kLine = 22.0f, kSize = 15.0f;
			float w = 0.0f;
			for (const auto& line : a_st.log) {
				w = std::max(w, TextWidth(Mono(), "> " + line.text, kSize, 1.0f));
			}
			w = std::min(w + 36.0f, a_v.w * 0.42f);
			const float h = kLine * static_cast<float>(a_st.log.size()) + 20.0f;
			const float x = 24.0f, y0 = a_bottom - h;
			Chamfer(a_v, x, y0, w, h, 10.0f, kPanel, kEdge);
			float y = y0 + 10.0f;
			for (const auto& line : a_st.log) {
				const ImU32 col = line.tone == 1 ? kBright : line.tone == 2 ? kRedText : kText;
				Print(a_v, Mono(), "> " + line.text, x + 16, y, kSize, col, 0, 1.0f);
				y += kLine;
			}
		}

		void Hover(const View& a_v, const Game::HudState& a_st)
		{
			if (a_st.hover.empty() || a_st.phase != 0) {
				return;
			}
			std::string tail = std::format("PAYS {} TO 1", a_st.hoverPays);
			if (a_st.hoverMine > 0) {
				tail += std::format(" · YOURS {}", a_st.hoverMine);
			}
			if (a_st.hoverAll > a_st.hoverMine) {
				tail += std::format(" · ALL {}", a_st.hoverAll);
			}
			const float cx = a_v.w * 0.5f, y = a_v.h - 150.0f;  // (above the buttons)
			const float w = std::max(TextWidth(Title(), a_st.hover, 24, 2.0f), TextWidth(Mono(), tail, 13, 1.5f)) + 60.0f;
			Chamfer(a_v, cx - w * 0.5f, y, w, 62.0f, 10.0f, kPanel, kEdge);
			Print(a_v, Title(), a_st.hover, cx, y + 6, 24, kBright, kGlow, 2.0f, Align::kCentre);
			Print(a_v, Mono(), tail, cx, y + 38, 13, kDim, 0, 1.5f, Align::kCentre);
		}

		// your bets, top middle: what each is on and how much (won in green, lost in red once the ball's in)
		void MyBets(const View& a_v, const Game::HudState& a_st)
		{
			if (a_st.myBets.empty()) {
				return;
			}
			constexpr int   kCols = 2, kRows = 3;
			constexpr float kRow = 17.0f, colW = 190.0f, pad = 14.0f;
			const int       n = static_cast<int>(a_st.myBets.size());
			const int       cols = n > kRows ? kCols : 1;
			const int       shown = std::min(n, kCols * kRows);
			const int       rows = std::min(n, kRows);
			const float     w = colW * static_cast<float>(cols) + pad * 2.0f;
			const float     h = 30.0f + kRow * static_cast<float>(rows) + 8.0f;
			const float     x = a_v.w * 0.5f - w * 0.5f, y = 24.0f;
			Chamfer(a_v, x, y, w, h, 10.0f, kPanel, kEdge);
			int total = 0;
			for (const auto& b : a_st.myBets) {
				total += b.amount;
			}
			Print(a_v, Mono(), std::format("YOUR BETS · {}", Commas(total)), x + w * 0.5f, y + 8.0f, 12, kDim, 0, 2.0f, Align::kCentre);
			for (int i = 0; i < shown; ++i) {
				const auto& b = a_st.myBets[i];
				const bool  more = i == shown - 1 && n > shown;
				const float cx = x + pad + colW * static_cast<float>(i / kRows), cy = y + 28.0f + kRow * static_cast<float>(i % kRows);
				if (more) {
					Print(a_v, Mono(), std::format("+{} MORE", n - shown + 1), cx, cy, 12, kDim, 0, 1.0f);
					continue;
				}
				const ImU32 col = b.won > 0 ? kBright : b.won == 0 ? kRedText : kText;
				const float size = std::min(12.0f, 12.0f * (colW - 70.0f) / std::max(1.0f, TextWidth(Mono(), b.name, 12, 1.0f)));
				Print(a_v, Mono(), b.name, cx, cy, size, col, 0, 1.0f);
				const std::string amt = b.won > 0 ? std::format("+{}", Commas(b.won)) : Commas(b.amount);
				Print(a_v, Mono(), amt, cx + colW - 14.0f, cy, 12, col, 0, 1.0f, Align::kRight);
			}
		}

		// the key hints, in a strip along the bottom
		void Keys(const View& a_v, const Game::HudState& a_st)
		{
			constexpr float kSize = 13.0f;
			const float     w = TextWidth(Mono(), a_st.keys, kSize, 1.0f) + 40.0f;
			const float     y = a_v.h - 46.0f;
			Chamfer(a_v, a_v.w * 0.5f - w * 0.5f, y, w, 30.0f, 8.0f, kPanel, kEdge);
			Print(a_v, Mono(), a_st.keys, a_v.w * 0.5f, y + 7.0f, kSize, kText, 0, 1.0f, Align::kCentre);
		}

		void Banner(const View& a_v, const Game::HudState& a_st)
		{
			if (a_st.banner == 0) {
				return;
			}
			float alpha = 1.0f;
			if (a_st.banner == 1) {
				// no more bets: up for a moment, then out of the way of the wheel
				constexpr float kHold = 2.0f, kFade = 0.6f;
				alpha = a_st.bannerAge < kHold ? 1.0f : 1.0f - (a_st.bannerAge - kHold) / kFade;
				if (alpha <= 0.0f) {
					return;
				}
				const float cx = a_v.w * 0.5f, y = 120.0f;
				Chamfer(a_v, cx - 220, y, 440, 92, 14, Fade(kPanel, alpha), Fade(kEdge, alpha));
				Print(a_v, Title(), a_st.bannerTitle, cx, y + 10, 40, Fade(kBright, alpha), Fade(kGlow, alpha), 4.0f, Align::kCentre);
				Print(a_v, Mono(), a_st.bannerSub, cx, y + 62, 13, Fade(kDim, alpha), 0, 2.0f, Align::kCentre);
				return;
			}
			alpha = std::min(1.0f, a_st.bannerAge * 4.0f);
			const bool  won = a_st.banner == 2;
			const bool  lost = a_st.banner == 3;
			const float cx = a_v.w * 0.5f, y = 110.0f;
			Chamfer(a_v, cx - 240, y, 480, 168, 16, Fade(kPanel, alpha), Fade(lost ? Rgba(255, 110, 90, 0.6f) : kEdge, alpha));
			if (a_st.bannerNumber >= 0) {
				NumberDisc(a_v, a_st.bannerNumber, cx, y + 56, 36, alpha);
			}
			Print(a_v, Title(), a_st.bannerTitle, cx, y + 98, 30, Fade(lost ? kRedText : kBright, alpha), Fade(lost ? kRedGlow : kGlow, alpha), 3.0f,
				Align::kCentre);
			if (!a_st.bannerSub.empty()) {
				Print(a_v, Mono(), a_st.bannerSub, cx, y + 138, 15, Fade(won ? kText : lost ? kRedText : kDim, alpha), 0, 2.0f, Align::kCentre);
			}
		}

		// ------------------------------------------------------------ poker

		// the poker HUD's pieces, sized first so they can be laid out without overlapping (and the whole HUD shrunk when
		// the screen, or the HUD scale, leaves too little room)
		constexpr float kPkMargin = 20.0f;
		constexpr float kPkRow = 46.0f;
		constexpr float kPkBottom = 98.0f;  // the button rows' room along the bottom (the quick sizes over the bar)
		constexpr float kPkSideW = 170.0f;  // the column of buttons on the right
		constexpr float kPkSideH = 7 * 34.0f;

		float PokerStatusW(const Game::HudState& a_st)
		{
			float w = 320.0f;
			w = std::max(w, TextWidth(Mono(), a_st.pkInfo, 12, 1.0f) + 36.0f);
			w = std::max(w, TextWidth(Mono(), a_st.pkTitle.empty() ? std::string("POKER · NO LIMIT HOLD'EM") : a_st.pkTitle, 13, 2.0f) + 36.0f);
			return w;
		}
		float PokerStatusH(const Game::HudState& a_st) { return 100.0f + kPkRow * static_cast<float>(a_st.pkRows.size()); }
		float PokerBoardW() { return 130.0f; }
		float PokerBoardH() { return 58.0f; }
		float PokerHoleW() { return 300.0f; }
		float PokerHoleH() { return 40.0f; }
		float PokerLogH(const Game::HudState& a_st) { return a_st.log.empty() ? 0.0f : 22.0f * static_cast<float>(a_st.log.size()) + 20.0f; }

		void PokerStatus(const View& a_v, const Game::HudState& a_st)
		{
			const float x = kPkMargin, y = kPkMargin, w = PokerStatusW(a_st);
			Chamfer(a_v, x, y, w, PokerStatusH(a_st), 12.0f, kPanel, kEdge);
			Print(a_v, Mono(), a_st.pkTitle.empty() ? std::string("POKER · NO LIMIT HOLD'EM") : a_st.pkTitle, x + 18, y + 12, 13, kDim, 0, 2.0f);
			Print(a_v, Title(), Commas(a_st.caps), x + 18, y + 28, 30, kBright, kGlow, 2.0f);
			Print(a_v, Mono(), "CAPS", x + 22 + TextWidth(Title(), Commas(a_st.caps), 30, 2.0f), y + 44, 12, kDim, 0, 2.0f);
			Print(a_v, Mono(), a_st.pkInfo, x + 18, y + 72, 12, kText, 0, 1.0f);
			// the players: a disc with their initial (a ring round it running down while they think), their name, what they
			// did as a tag, their caps and their bet
			const float stackX = x + w - 16.0f;
			float       row = y + 100;
			for (const auto& r : a_st.pkRows) {
				const float a = r.folded ? 0.45f : 1.0f;
				DL::AddLine(a_v.dl, a_v.P(x + 14, row - 4), a_v.P(x + w - 14, row - 4), Rgba(80, 200, 100, 0.15f), std::max(1.0f, a_v.s));
				if (r.toAct) {
					Chamfer(a_v, x + 8, row - 2, w - 16, kPkRow - 6, 6.0f, Rgba(40, 160, 70, 0.16f), Rgba(120, 255, 140, 0.45f));
				}
				const ImU32 col = Fade(r.you ? kBright : kText, a);
				// the disc
				const float cx = x + 30, cy = row + 19, rad = 14.0f;
				DL::AddCircleFilled(a_v.dl, a_v.P(cx, cy), rad * a_v.s, Fade(r.you ? Rgba(30, 90, 40) : Rgba(14, 44, 20), a), 32);
				DL::AddCircle(a_v.dl, a_v.P(cx, cy), rad * a_v.s, Fade(kEdge, a), 32, std::max(1.0f, 1.2f * a_v.s));
				const std::string initial = r.name.empty() ? std::string("?") : r.name.substr(0, 1);
				PrintInkCentred(a_v, Title(), initial, cx, cy, 15, col);
				if (r.think >= 0.0f || r.toAct) {
					// a ring: the time left to think, or (your turn) a full one
					const float  f = r.think >= 0.0f ? r.think : 1.0f;
					const int    n = 40;
					std::array<ImVec2, 41> pts{};
					int          k = 0;
					for (int i = 0; i <= n; ++i) {
						const float ang = -1.5708f + 6.2832f * f * static_cast<float>(i) / n;
						pts[k++] = a_v.P(cx + std::cos(ang) * (rad + 4.0f), cy + std::sin(ang) * (rad + 4.0f));
					}
					DL::AddPolyline(a_v.dl, pts.data(), k, kBright, 0, std::max(1.5f, 2.5f * a_v.s));
				}
				if (r.bigBlind) {
					DL::AddCircleFilled(a_v.dl, a_v.P(cx + 11, cy + 11), 7.0f * a_v.s, Rgba(236, 232, 222), 20);
					PrintInkCentred(a_v, Mono(), "B", cx + 11, cy + 11, 9, Rgba(20, 20, 20));
				}
				// the name, and what they did as a tag under it
				Print(a_v, Mono(), r.name, x + 56, row + 4, 14, col, 0, 1.0f);
				if (!r.status.empty()) {
					const bool  allIn = r.status.starts_with("ALL IN");
					const bool  thinking = r.status == "THINKING";
					const ImU32 tagFill = allIn ? Rgba(120, 40, 20, 0.85f) : thinking ? Rgba(30, 110, 50, 0.85f) : Rgba(14, 44, 20, 0.85f);
					const ImU32 tagText = allIn ? Rgba(255, 190, 150) : thinking ? kBright : kDim;
					const float tw = TextWidth(Mono(), r.status, 10, 1.0f) + 12.0f;
					DL::AddRectFilled(a_v.dl, a_v.P(x + 56, row + 22), a_v.P(x + 56 + tw, row + 36), Fade(tagFill, a), 4.0f * a_v.s, 0);
					Print(a_v, Mono(), r.status, x + 62, row + 24, 10, Fade(tagText, a), 0, 1.0f);
				}
				// the caps, and the bet in front of them (a little chip)
				Print(a_v, Mono(), Commas(r.stack), stackX, row + 4, 14, col, 0, 1.0f, Align::kRight);
				if (r.bet > 0) {
					const std::string b = Commas(r.bet);
					const float       bw = TextWidth(Mono(), b, 11, 1.0f);
					DL::AddCircleFilled(a_v.dl, a_v.P(stackX - bw - 10, row + 29), 5.0f * a_v.s, Fade(kRedChip, a), 16);
					DL::AddCircle(a_v.dl, a_v.P(stackX - bw - 10, row + 29), 3.2f * a_v.s, Fade(Rgba(236, 232, 222), a), 16, std::max(1.0f, a_v.s));
					Print(a_v, Mono(), b, stackX, row + 23, 11, Fade(kBright, a), 0, 1.0f, Align::kRight);
				}
				row += kPkRow;
			}
		}

		// the pot and what's going on, top right (the cards themselves are on the table)
		void PokerBoard(const View& a_v, const Game::HudState& a_st)
		{
			const float w = PokerBoardW(), x = a_v.w - kPkMargin - w, y = kPkMargin;
			Chamfer(a_v, x, y, w, PokerBoardH(), 12.0f, kPanel, kEdge);
			const float mid = x + w * 0.5f;
			// just the pot: the word, the number under it
			Print(a_v, Mono(), a_st.pkBoardLabel.empty() ? std::string("POT") : a_st.pkBoardLabel, mid, y + 8, 11, kDim, 0, 2.0f, Align::kCentre);
			Print(a_v, Title(), Commas(a_st.pkPot), mid, y + 23, 24, a_st.pkPot > 0 ? kBright : kDim, a_st.pkPot > 0 ? kGlow : 0, 1.5f, Align::kCentre);
		}

		// your hand's name and your turn, top middle
		// the dealer's panel's height (0: not shown): it's at the top, the turn panel under it
		float PokerDealerH(const Game::HudState& a_st)
		{
			if (!a_st.pkDealer || a_st.pkBoard.empty() || a_st.blackjack) {
				return 0.0f;
			}
			return 70.0f + 16.0f + (a_st.pkWinFive.empty() ? 0.0f : 22.0f);
		}

		void PokerHole(const View& a_v, const Game::HudState& a_st)
		{
			const std::string label = a_st.pkYourTurn ? (a_st.pkHand.empty() ? std::string("YOUR TURN") : "YOUR TURN · " + a_st.pkHand) : a_st.pkHand;
			if (label.empty()) {
				return;
			}
			const float w = PokerHoleW(), h = PokerHoleH();
			const float dh = PokerDealerH(a_st);
			const float x = a_v.w * 0.5f - w * 0.5f, y = kPkMargin + (dh > 0.0f ? dh + 8.0f : 0.0f);  // (under the dealer's cards)
			Chamfer(a_v, x, y, w, h, 10.0f, kPanel, a_st.pkYourTurn ? kBright : kEdge);
			const float size = std::min(14.0f, 14.0f * (w - 24.0f) / std::max(1.0f, TextWidth(Mono(), label, 14, 1.0f)));
			Print(a_v, Mono(), label, x + w * 0.5f, y + (h - size) * 0.5f - 2.0f, size, a_st.pkYourTurn ? kBright : kText, a_st.pkYourTurn ? kGlow : 0,
				1.0f, Align::kCentre);
		}

		// a card's suit, drawn (the HUD's fonts are plain ASCII): clubs, diamonds, hearts, spades, about 2r across
		void SuitShape(const View& a_v, int a_suit, float a_cx, float a_cy, float a_r, ImU32 a_col)
		{
			auto P = [&](float a_x, float a_y) { return a_v.P(a_cx + a_x * a_r, a_cy + a_y * a_r); };
			const float r = a_r * a_v.s;
			auto circle = [&](float a_x, float a_y, float a_rad) { DL::AddCircleFilled(a_v.dl, P(a_x, a_y), a_rad * r, a_col, 20); };
			auto stem = [&]() { DL::AddTriangleFilled(a_v.dl, P(0.0f, 0.1f), P(-0.45f, 1.05f), P(0.45f, 1.05f), a_col); };
			switch (a_suit) {
			case 0:  // clubs
				circle(0.0f, -0.48f, 0.42f);
				circle(-0.5f, 0.18f, 0.42f);
				circle(0.5f, 0.18f, 0.42f);
				circle(0.0f, 0.05f, 0.25f);
				stem();
				break;
			case 1:  // diamonds
				{
					const std::array<ImVec2, 4> pts{ P(0.0f, -1.05f), P(0.72f, 0.0f), P(0.0f, 1.05f), P(-0.72f, 0.0f) };
					DL::AddConvexPolyFilled(a_v.dl, pts.data(), 4, a_col);
				}
				break;
			case 2:  // hearts
				circle(-0.46f, -0.32f, 0.5f);
				circle(0.46f, -0.32f, 0.5f);
				DL::AddTriangleFilled(a_v.dl, P(-0.94f, -0.18f), P(0.94f, -0.18f), P(0.0f, 0.95f), a_col);
				break;
			default:  // spades
				circle(-0.44f, 0.22f, 0.44f);
				circle(0.44f, 0.22f, 0.44f);
				DL::AddTriangleFilled(a_v.dl, P(0.0f, -1.0f), P(-0.86f, 0.12f), P(0.86f, 0.12f), a_col);
				stem();
				break;
			}
		}

		// the table's own card faces (hud\cards.png: cards_d's atlas, 10 across and 6 down, the back in cell 52): not
		// having it isn't the HUD's art missing, the cards are drawn instead
		ImTextureID CardsTexture()
		{
			static ImTextureID id = nullptr;
			static bool        tried = false;
			if (!tried) {
				tried = true;
				for (const char* dir : { "Data\\F4SE\\Plugins\\fo4roulette\\hud\\", "F4SE\\Plugins\\fo4roulette\\hud\\" }) {
					if ((id = F4SEMenuFramework::LoadTexture(std::string(dir) + "cards.png"))) {
						break;
					}
				}
				if (!id) {
					log::warn("HUD: cards.png didn't load; the HUD's cards are drawn instead");
				}
			}
			return id;
		}

		// a card on the HUD (44 x 70: a card's 5:8 with a margin): the table's own face (its back with a_card 52); a_in false:
		// dimmed; a_mark: outlined
		void HudCard(const View& a_v, int a_card, float a_x, float a_y, bool a_in = true, bool a_mark = false)
		{
			constexpr float cw = 44.0f, ch = 70.0f;
			if (auto tex = CardsTexture()) {
				const float u = static_cast<float>(a_card % 10) / 10.0f, v = static_cast<float>(a_card / 10) / 6.0f;
				// (on a card of its own a little bigger: the faces in the atlas are cut close, their corners' numbers at the
				// very edge)
				constexpr float m = 3.0f;
				DL::AddRectFilled(a_v.dl, a_v.P(a_x, a_y), a_v.P(a_x + cw, a_y + ch), a_in ? Rgba(250, 250, 248) : Rgba(250, 250, 248, 0.4f),
					4.0f * a_v.s, 0);
				DL::AddImage(a_v.dl, tex, a_v.P(a_x + m, a_y + m), a_v.P(a_x + cw - m, a_y + ch - m), { u + 0.001f, v + 0.001f },
					{ u + 0.1f - 0.001f, v + 1.0f / 6.0f - 0.001f }, a_in ? Rgba(255, 255, 255) : Rgba(255, 255, 255, 0.4f));
				DL::AddRect(a_v.dl, a_v.P(a_x, a_y), a_v.P(a_x + cw, a_y + ch), a_in ? Rgba(0, 0, 0, 0.35f) : Rgba(0, 0, 0, 0.15f), 4.0f * a_v.s, 0,
					std::max(1.0f, a_v.s));
				if (a_mark) {
					DL::AddRect(a_v.dl, a_v.P(a_x - 1.5f, a_y - 1.5f), a_v.P(a_x + cw + 1.5f, a_y + ch + 1.5f), kBright, 6.0f * a_v.s, 0,
						std::max(1.0f, 2.0f * a_v.s));
				}
				return;
			}
			if (a_card >= 52) {
				DL::AddRectFilled(a_v.dl, a_v.P(a_x, a_y), a_v.P(a_x + cw, a_y + ch), Rgba(150, 30, 35), 5.0f * a_v.s, 0);
				return;
			}
			static constexpr std::array<const char*, 13> kRanks{ "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A" };
			const int   rank = a_card % 13, suit = a_card / 13;
			const bool  red = suit == 1 || suit == 2;
			const ImU32 ink = Fade(red ? Rgba(200, 30, 35) : Rgba(20, 20, 24), a_in ? 1.0f : 0.35f);
			DL::AddRectFilled(a_v.dl, a_v.P(a_x, a_y), a_v.P(a_x + cw, a_y + ch), Fade(Rgba(246, 244, 236), a_in ? 1.0f : 0.45f), 5.0f * a_v.s, 0);
			if (a_mark) {
				DL::AddRect(a_v.dl, a_v.P(a_x - 1.5f, a_y - 1.5f), a_v.P(a_x + cw + 1.5f, a_y + ch + 1.5f), kBright, 6.0f * a_v.s, 0,
					std::max(1.0f, 2.0f * a_v.s));
			}
			Print(a_v, Mono(), kRanks[rank], a_x + 5.0f, a_y + 4.0f, 16, ink, 0, 0.0f);
			SuitShape(a_v, suit, a_x + cw * 0.5f, a_y + ch * 0.6f, 11.0f, ink);
		}

		// your two cards, bottom middle above the buttons: face up, or face down till you SHOW MY CARDS (the setting)
		void PokerMine(const View& a_v, const Game::HudState& a_st)
		{
			if (a_st.pkHole[0] < 0) {
				return;
			}
			constexpr float cw = 44.0f, ch = 70.0f, gap = 7.0f, pad = 8.0f;
			const float     w = cw * 2.0f + gap + pad * 2.0f, h = ch + pad * 2.0f;
			const float     x = a_v.w * 0.5f - w * 0.5f, y = a_v.h - kPkBottom - h - 6.0f;
			Chamfer(a_v, x, y, w, h, 10.0f, kPanel, kEdge);
			for (int k = 0; k < 2; ++k) {
				HudCard(a_v, a_st.pkPeek ? a_st.pkHole[k] : 52, x + pad + static_cast<float>(k) * (cw + gap), y + pad);
			}
		}

		// the dealer's cards (the board), top middle: the SHOW / HIDE DEALER button
		void PokerDealer(const View& a_v, const Game::HudState& a_st)
		{
			if (!a_st.pkDealer || a_st.pkBoard.empty()) {
				return;
			}
			constexpr float cw = 44.0f, ch = 70.0f, gap = 7.0f, pad = 8.0f, split = 22.0f;
			// at the showdown: the winning five lifted and outlined (the rest dimmed), the winner's own two beside the board
			const bool  won = !a_st.pkWinFive.empty();
			const float boardW = cw * 5.0f + gap * 4.0f;
			const float w = boardW + pad * 2.0f + (won ? split + cw * 2.0f + gap : 0.0f), h = ch + pad * 2.0f + (won ? 22.0f : 0.0f);
			const float x = a_v.w * 0.5f - w * 0.5f, y = kPkMargin;  // (at the very top: the turn panel under it)
			Chamfer(a_v, x, y, w, h, 10.0f, kPanel, kEdge);
			auto card = [&](int a_card, float a_cx, float a_cy) {
				const bool in = !won || std::ranges::find(a_st.pkWinFive, a_card) != a_st.pkWinFive.end();
				HudCard(a_v, a_card, a_cx, a_cy - (won && in ? 4.0f : 0.0f), in, won && in);
			};
			for (int k = 0; k < 5; ++k) {
				const float cx = x + pad + static_cast<float>(k) * (cw + gap), cy = y + pad;
				if (k >= static_cast<int>(a_st.pkBoard.size())) {
					// (still to come: an empty place)
					DL::AddRect(a_v.dl, a_v.P(cx, cy), a_v.P(cx + cw, cy + ch), Rgba(80, 200, 100, 0.25f), 5.0f * a_v.s, 0, std::max(1.0f, a_v.s));
					continue;
				}
				card(a_st.pkBoard[k], cx, cy);
			}
			if (won) {
				const float hx = x + pad + boardW + split;
				DL::AddLine(a_v.dl, a_v.P(hx - split * 0.5f, y + pad), a_v.P(hx - split * 0.5f, y + pad + ch), kEdge, std::max(1.0f, a_v.s));
				for (int k = 0; k < 2; ++k) {
					if (a_st.pkWinHole[k] >= 0) {
						card(a_st.pkWinHole[k], hx + static_cast<float>(k) * (cw + gap), y + pad);
					}
				}
				Print(a_v, Mono(), a_st.pkWinWho + " · " + a_st.pkWinName, x + w * 0.5f, y + pad + ch + 6.0f, 12, kBright, 0, 1.0f, Align::kCentre);
			}
		}

		void PokerBanner(const View& a_v, const Game::HudState& a_st)
		{
			if (a_st.banner == 0) {
				return;
			}
			// green when you won, red when you lost, plain when it wasn't your hand
			const bool  lost = a_st.banner == 3;
			const bool  won = a_st.banner == 2;
			const float alpha = std::min(1.0f, a_st.bannerAge * 4.0f) * std::clamp((5.0f - a_st.bannerAge) / 0.6f, 0.0f, 1.0f);
			const float w = std::max({ 380.0f, TextWidth(Title(), a_st.bannerTitle, 28, 3.0f) + 60.0f, TextWidth(Mono(), a_st.bannerSub, 14, 2.0f) + 60.0f });
			const float cx = a_v.w * 0.5f, y = a_v.h * 0.42f - 46.0f;
			const ImU32 edge = lost ? Rgba(255, 110, 90, 0.7f) : won ? kBright : kEdge;
			Chamfer(a_v, cx - w * 0.5f, y, w, 92, 16, Fade(lost ? Rgba(30, 6, 6, 0.8f) : kPanel, alpha), Fade(edge, alpha));
			Print(a_v, Title(), a_st.bannerTitle, cx, y + 12, 28, Fade(lost ? kRedText : won ? kBright : kText, alpha),
				Fade(lost ? kRedGlow : won ? kGlow : 0, alpha), 3.0f, Align::kCentre);
			if (!a_st.bannerSub.empty()) {
				Print(a_v, Mono(), a_st.bannerSub, cx, y + 58, 14, Fade(lost ? kRedText : kText, alpha), 0, 2.0f, Align::kCentre);
			}
		}


		// the log, bottom left: as wide as the room left of your cards
		void PokerLog(const View& a_v, const Game::HudState& a_st)
		{
			if (a_st.log.empty()) {
				return;
			}
			constexpr float kLine = 22.0f;
			const float     maxW = a_v.w - kPkSideW - kPkMargin * 3.0f;
			float           w = 0.0f;
			for (const auto& line : a_st.log) {
				w = std::max(w, TextWidth(Mono(), "> " + line.text, 14, 1.0f));
			}
			const float size = w + 32.0f > maxW ? 14.0f * (maxW - 32.0f) / w : 14.0f;
			w = std::min(w + 32.0f, maxW);
			const float h = PokerLogH(a_st);
			const float x = kPkMargin, y0 = a_v.h - kPkBottom - h;
			Chamfer(a_v, x, y0, w, h, 10.0f, kPanel, kEdge);
			float y = y0 + 10.0f;
			int idx = 0;
			const int n = static_cast<int>(a_st.log.size());
			for (const auto& line : a_st.log) {
				const ImU32 col = line.tone == 1 ? kBright : line.tone == 2 ? kRedText : kText;
				const float age = static_cast<float>(n - 1 - idx++);  // (the older, the fainter)
				Print(a_v, Mono(), "> " + line.text, x + 14, y, size, Fade(col, std::max(0.35f, 1.0f - age * 0.16f)), 0, 1.0f);
				y += kLine;
			}
		}

		// a button: drawn, and noted (in screen pixels) for Game to click; true when the pointer's over it
		bool Button(const View& a_v, const Game::HudState& a_st, std::vector<Game::PkButton>& a_out, int a_id, std::string_view a_label,
			float a_x, float a_y, float a_w, float a_h, bool a_on, bool a_strong = false, float a_size = 14.0f, ImU32 a_accent = 0)
		{
			const ImVec2 p0 = a_v.P(a_x, a_y), p1 = a_v.P(a_x + a_w, a_y + a_h);
			const float  ox = a_v.origin.x, oy = a_v.origin.y;
			const bool   hover = a_on && a_st.pkPtrX >= p0.x - ox && a_st.pkPtrX <= p1.x - ox && a_st.pkPtrY >= p0.y - oy && a_st.pkPtrY <= p1.y - oy;
			ImU32 fill = !a_on ? Rgba(6, 20, 9, 0.5f) : hover ? Rgba(40, 140, 60, 0.85f) : a_strong ? Rgba(16, 70, 28, 0.85f) : kPanel;
			ImU32 edge = a_on ? (hover || a_strong ? kBright : kEdge) : Rgba(80, 200, 100, 0.2f);
			if (a_on && a_accent) {
				fill = hover ? Fade(a_accent, 0.95f) : Fade(a_accent, 0.6f);  // (fold red, all in amber)
				edge = Fade(a_accent | 0xFF000000u, hover ? 1.0f : 0.8f);
			}
			Chamfer(a_v, a_x, a_y, a_w, a_h, 6.0f, fill, edge);
			const float size = std::min(a_size, a_size * (a_w - 12.0f) / std::max(1.0f, TextWidth(Mono(), a_label, a_size, 1.0f)));
			Print(a_v, Mono(), a_label, a_x + a_w * 0.5f, a_y + (a_h - size) * 0.5f - 1.0f, size, !a_on ? kOff : hover ? Rgba(230, 255, 230) : kBright,
				0, 1.0f, Align::kCentre);
			a_out.push_back({ a_id, p0.x - ox, p0.y - oy, p1.x - ox, p1.y - oy, a_on });
			return hover;
		}

		void PokerButtons(const View& a_v, const Game::HudState& a_st, ImGuiViewport* a_viewport)
		{
			std::vector<Game::PkButton> out;
			const bool turn = a_st.pkYourTurn;
			// the actions, along the bottom: check / call, fold, the raise (a slider to set it, the button to make it), all in
			const std::string call = !turn ? std::string("CHECK / CALL") : a_st.pkToCall == 0 ? std::string("CHECK") : std::format("CALL {}", Commas(a_st.pkToCall));
			const std::string raise = turn ? std::format("{} {}", a_st.pkToCall == 0 ? "BET" : "RAISE TO", Commas(a_st.pkRaiseTo)) : std::string("RAISE");
			const std::string allIn = turn ? std::format("ALL IN {}", Commas(a_st.pkAllIn)) : std::string("ALL IN");
			constexpr float h = 40.0f, gap = 8.0f;
			const float wCall = 150.0f, wFold = 110.0f, wSlider = 200.0f, wRaise = 170.0f, wAll = 150.0f;
			const float w = wCall + wFold + wSlider + wRaise + wAll + gap * 4;
			const float k = std::min(1.0f, (a_v.w - kPkMargin * 2.0f) / w);
			float       x = a_v.w * 0.5f - w * k * 0.5f;
			const float y = a_v.h - 52.0f;
			auto next = [&](int a_id, const std::string& a_label, float a_w, bool a_strong = false, ImU32 a_accent = 0) {
				Button(a_v, a_st, out, a_id, a_label, x, y, a_w * k, h, turn, a_strong, 14.0f, a_accent);
				x += (a_w + gap) * k;
			};
			const bool pad = a_st.pkPad;  // (a controller: its button on each)
			auto       tag = [&](const char* a_b, const std::string& a_s) { return pad ? std::format("{} {}", a_b, a_s) : a_s; };
			if (turn) {
			next(1, tag("(A)", call), wCall, turn);
			next(0, tag("(B)", "FOLD"), wFold, false, Rgba(150, 30, 30));
			{
				// the slider: from the smallest raise to all in
				const bool  on = turn && a_st.pkMaxRaise > a_st.pkMinRaise;
				const float sw = wSlider * k;
				{
					// the quick sizes, over the slider and the raise button: the smallest raise, half the pot, two
					// thirds, the pot
					static constexpr std::array<const char*, 4> kSizes{ "MIN", "1/2 POT", "2/3 POT", "POT" };
					const float span = (wSlider + gap + wRaise) * k, qw = (span - gap * k * 3.0f) / 4.0f;
					for (int q = 0; q < 4; ++q) {
						Button(a_v, a_st, out, 30 + q, kSizes[q], x + static_cast<float>(q) * (qw + gap * k), y - 34.0f, qw, 26.0f, on, false, 11.0f);
					}
				}
				Chamfer(a_v, x, y, sw, h, 6.0f, on ? kPanel : Rgba(6, 20, 9, 0.5f), on ? kEdge : Rgba(80, 200, 100, 0.2f));
				const float l = x + 14.0f, r = x + sw - 14.0f, cy = y + h * 0.5f + 4.0f;
				const float f = on ? std::clamp(static_cast<float>(a_st.pkRaiseTo - a_st.pkMinRaise) / static_cast<float>(a_st.pkMaxRaise - a_st.pkMinRaise), 0.0f, 1.0f) : 0.0f;
				DL::AddLine(a_v.dl, a_v.P(l, cy), a_v.P(r, cy), on ? Rgba(80, 200, 100, 0.5f) : Rgba(80, 200, 100, 0.2f), std::max(1.0f, 3.0f * a_v.s));
				if (on) {
					DL::AddLine(a_v.dl, a_v.P(l, cy), a_v.P(l + (r - l) * f, cy), kBright, std::max(1.0f, 3.0f * a_v.s));
					DL::AddCircleFilled(a_v.dl, a_v.P(l + (r - l) * f, cy), 7.0f * a_v.s, kBright, 20);
					Print(a_v, Mono(), Commas(a_st.pkMinRaise), l, y + 3.0f, 10, kDim, 0, 1.0f);
					Print(a_v, Mono(), "MAX", r, y + 3.0f, 10, kDim, 0, 1.0f, Align::kRight);
				}
				// (the whole panel to grab, the knob at either end included; the track's ends noted too, so where the pointer
				// is along it is the fraction)
				const ImVec2 p0 = a_v.P(x, y), p1 = a_v.P(x + sw, y + h);
				const ImVec2 tl = a_v.P(l, y), tr = a_v.P(r, y);
				Game::PkButton sb{ 11, p0.x - a_v.origin.x, p0.y - a_v.origin.y, p1.x - a_v.origin.x, p1.y - a_v.origin.y, on };
				sb.t0 = tl.x - a_v.origin.x;
				sb.t1 = tr.x - a_v.origin.x;
				out.push_back(sb);
				x += (wSlider + gap) * k;
			}
			next(2, tag("(X)", raise), wRaise, turn);
			next(3, tag("(Y)", allIn), wAll, false, Rgba(170, 100, 20));
			} else if (a_st.pkCanPre) {
				// not your turn yet: an action picked ahead, done the moment it is (picked again: off)
				static constexpr std::array<const char*, 3> kPre{ "CHECK / FOLD", "CHECK", "CALL ANY" };
				const float pw = 170.0f;
				float       px = a_v.w * 0.5f - (pw * 3.0f + gap * 2.0f) * k * 0.5f;
				for (int q = 0; q < 3; ++q) {
					const bool picked = a_st.pkPre == q + 1;
					Button(a_v, a_st, out, 34 + q, std::format("{} {}", picked ? "[X]" : "[ ]", kPre[q]), px, y, pw * k, h, true, picked, 13.0f);
					px += (pw + gap) * k;
				}
			}
			// the table's buttons, bottom right: in the corner, or above the action buttons when they'd run into them
			const int   nSide = a_st.pkKit ? 7 : 6;
			const float sx = a_v.w - kPkMargin - kPkSideW;
			const float barRight = a_v.w * 0.5f + w * k * 0.5f;
			const float sideFoot = barRight + gap < sx ? a_v.h - 12.0f : a_v.h - kPkBottom - 8.0f;
			float       sy = sideFoot - (34.0f * static_cast<float>(nSide) - 6.0f);
			auto side = [&](int a_id, const std::string& a_label, bool a_on) {
				Button(a_v, a_st, out, a_id, a_label, sx, sy, kPkSideW, 28.0f, a_on, false, 12.0f);
				sy += 34.0f;
			};
			side(10, tag("(RS)", a_st.pkPeek ? "HIDE MY CARDS" : "SHOW MY CARDS"), a_st.pkHole[0] >= 0 && a_st.pkCardsDown);  // (with the setting off they're always up)
			side(15, a_st.pkDealer ? "HIDE DEALER" : "SHOW DEALER", true);
			side(9, tag("(START)", a_st.pkTop ? "VIEW: MY SEAT" : "VIEW: FROM ABOVE"), true);
			side(12, a_st.pkZoom ? "ZOOM OUT" : "ZOOM IN", true);
			side(6, tag("(BACK)", "STEP AWAY"), true);
			side(8, tag("(LS)", "CASH OUT"), true);
			if (a_st.pkKit) {
				side(7, "PACK UP THE TABLE", a_st.pkInfo.find("BETWEEN HANDS") != std::string::npos);
			}
			Game::Get().SetPkButtons(std::move(out), a_viewport->Size.x, a_viewport->Size.y);
			// the pointer: an arrow
			if (a_st.pkPtrX >= 0.0f) {
				const ImVec2 p{ a_v.origin.x + a_st.pkPtrX, a_v.origin.y + a_st.pkPtrY };
				const float  s = std::max(0.6f, a_v.s);
				const ImVec2 a{ p.x, p.y }, b{ p.x, p.y + 20.0f * s }, c{ p.x + 13.0f * s, p.y + 14.0f * s };
				DL::AddTriangleFilled(a_v.dl, a, b, c, Rgba(230, 255, 230));
				DL::AddTriangle(a_v.dl, a, b, c, Rgba(10, 40, 15), std::max(1.0f, 1.5f * s));
			}
		}

		// roulette's buttons, as the poker table's: along the bottom the chips to bet with and what to do with them,
		// down the right the table's own. The pointer reaches them by pushing the cursor on down past the layout's near
		// edge (and back up off them onto the layout); each says its key too
		void RouletteButtons(const View& a_v, const Game::HudState& a_st, ImGuiViewport* a_viewport)
		{
			std::vector<Game::PkButton> out;
			const bool  betting = a_st.phase == 0;
			constexpr float h = 40.0f, gap = 8.0f;
			const float wChip = 70.0f, wSame = 150.0f, wClear = 130.0f;
			const float w = wChip * 4 + gap * 3 + 24.0f + wSame + wClear + gap;
			const float k = std::min(1.0f, (a_v.w - 48.0f) / w);
			float       x = a_v.w * 0.5f - w * k * 0.5f;
			const float y = a_v.h - 52.0f;
			static constexpr std::array<const char*, 4> values{ "1", "5", "25", "100" };
			for (int i = 0; i < 4; ++i) {
				Button(a_v, a_st, out, 20 + i, values[i], x, y, wChip * k, h, true, a_st.chip == i, 16.0f);
				if (a_st.chip == i) {
					// (the chip you're betting with: marked under it)
					DL::AddLine(a_v.dl, a_v.P(x + 10.0f, y + h + 4.0f), a_v.P(x + wChip * k - 10.0f, y + h + 4.0f), kBright, std::max(1.0f, 2.5f * a_v.s));
				}
				x += (wChip + gap) * k;
			}
			x += 24.0f * k;
			// (no SPIN: the dealer spins the wheel, a while after the round's first bet)
			Button(a_v, a_st, out, 25, std::format("SAME AGAIN ({})", a_st.rlKeys[1]), x, y, wSame * k, h, betting, false, 14.0f);
			x += (wSame + gap) * k;
			Button(a_v, a_st, out, 26, std::format("CLEAR ({})", a_st.rlKeys[2]), x, y, wClear * k, h, betting && a_st.onTable > 0, false, 14.0f,
				Rgba(150, 30, 30));
			// the table's, bottom right above the bar
			const int   n = a_st.kitTable ? 3 : 2;
			const float sx = a_v.w - kPkMargin - kPkSideW;
			float       sy = y - 14.0f - (34.0f * static_cast<float>(n) - 6.0f);
			auto side = [&](int a_id, const std::string& a_label, bool a_on) {
				Button(a_v, a_st, out, a_id, a_label, sx, sy, kPkSideW, 28.0f, a_on, false, 12.0f);
				sy += 34.0f;
			};
			side(27, std::format("{} ({})", a_st.wheelView ? "VIEW: THE LAYOUT" : "VIEW: THE WHEEL", a_st.rlKeys[3]), true);
			side(28, std::format("LEAVE THE TABLE ({})", a_st.rlKeys[4]), true);
			if (a_st.kitTable) {
				side(29, std::format("PACK UP ({})", a_st.rlKeys[5]), betting && a_st.onTable == 0);
			}
			Game::Get().SetPkButtons(std::move(out), a_viewport->Size.x, a_viewport->Size.y);
			if (a_st.pkPtrX >= 0.0f) {
				const ImVec2 p{ a_v.origin.x + a_st.pkPtrX, a_v.origin.y + a_st.pkPtrY };
				const float  s = std::max(0.6f, a_v.s);
				const ImVec2 a{ p.x, p.y }, b{ p.x, p.y + 20.0f * s }, c{ p.x + 13.0f * s, p.y + 14.0f * s };
				DL::AddTriangleFilled(a_v.dl, a, b, c, Rgba(230, 255, 230));
				DL::AddTriangle(a_v.dl, a, b, c, Rgba(10, 40, 15), std::max(1.0f, 1.5f * s));
			} else {
				// (how to get to them)
				Print(a_v, Mono(), "MOUSE DOWN PAST THE LAYOUT FOR THE BUTTONS · LMB BET · RMB TAKE BACK", a_v.w * 0.5f, y - 20.0f, 11, kDim, 0, 1.0f,
					Align::kCentre);
			}
		}

		void DrawPoker(View a_v, const Game::HudState& a_st, ImGuiViewport* a_viewport)
		{
			// room for it all: the status and the board side by side along the top, the log and your cards along the
			// bottom; too little, and everything's drawn smaller
			const float needW = std::max(PokerStatusW(a_st) + PokerHoleW() + PokerBoardW() + kPkMargin * 4.0f, 520.0f + kPkSideW);
			const float needH = kPkMargin + std::max({ PokerStatusH(a_st), PokerBoardH(), PokerHoleH() }) + 16.0f +
			                    std::max(PokerLogH(a_st), kPkSideH) + kPkBottom;
			const float f = std::min({ 1.0f, a_v.w / needW, a_v.h / needH });
			if (f < 1.0f) {
				a_v.s *= f;
				a_v.w /= f;
				a_v.h /= f;
			}
			// sliding in when you sit down: the left panels from the left, the right ones from the right
			auto slid = [&](float a_delay, float a_dir) {
				const float t = std::clamp((a_st.pkAge - a_delay) / 0.45f, 0.0f, 1.0f);
				const float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
				View v = a_v;
				v.origin.x += a_dir * (1.0f - e) * 420.0f * a_v.s;
				return v;
			};
			PokerStatus(slid(0.0f, -1.0f), a_st);
			PokerBoard(slid(0.08f, 1.0f), a_st);
			PokerLog(slid(0.16f, -1.0f), a_st);
			PokerHole(a_v, a_st);
			PokerDealer(a_v, a_st);
			PokerMine(a_v, a_st);
			PokerBanner(a_v, a_st);
			PokerButtons(a_v, a_st, a_viewport);
		}

		// ------------------------------------------------------------ blackjack (Blackjack.cpp): poker's panels, its own cards and buttons

		// the dealer's cards and what they come to, top middle: the SHOW / HIDE DEALER button
		void BjDealer(const View& a_v, const Game::HudState& a_st)
		{
			if (a_st.bjDealer.empty() || !a_st.bjShowDealer) {
				return;
			}
			constexpr float cw = 44.0f, ch = 70.0f, gap = 7.0f, pad = 8.0f;
			const float     n = static_cast<float>(std::max<std::size_t>(a_st.bjDealer.size(), 2));
			const float     w = std::max(cw * n + gap * (n - 1.0f) + pad * 2.0f, 200.0f), h = ch + pad * 2.0f + 22.0f;
			const float     x = a_v.w * 0.5f - w * 0.5f, y = kPkMargin;
			Chamfer(a_v, x, y, w, h, 10.0f, kPanel, kEdge);
			Print(a_v, Mono(), "DEALER · " + a_st.bjDealerValue, x + w * 0.5f, y + 6.0f, 12, kBright, 0, 1.5f, Align::kCentre);
			const float rowW = cw * static_cast<float>(a_st.bjDealer.size()) + gap * static_cast<float>(a_st.bjDealer.size() - 1);
			float       cx = x + (w - rowW) * 0.5f;
			for (int c : a_st.bjDealer) {
				HudCard(a_v, c, cx, y + 22.0f + pad * 0.5f);
				cx += cw + gap;
			}
		}

		// your hand (or two, split), bottom middle above the buttons: the cards, what they come to, the bet, the result
		void BjMine(const View& a_v, const Game::HudState& a_st)
		{
			if (a_st.bjHands.empty() || !a_st.bjShowHand) {
				return;
			}
			constexpr float cw = 44.0f, ch = 70.0f, gap = 5.0f, pad = 8.0f, between = 16.0f;
			float total = 0.0f;
			std::vector<float> widths;
			for (const auto& hd : a_st.bjHands) {
				const float n = static_cast<float>(std::max<std::size_t>(hd.cards.size(), 2));
				const float hw = std::max(cw * n + gap * (n - 1.0f), 130.0f);
				widths.push_back(hw);
				total += hw;
			}
			total += between * static_cast<float>(a_st.bjHands.size() - 1) + pad * 2.0f;
			const float h = ch + pad * 2.0f + 40.0f;
			const float x0 = a_v.w * 0.5f - total * 0.5f, y = a_v.h - kPkBottom - h - 6.0f;
			Chamfer(a_v, x0, y, total, h, 10.0f, kPanel, kEdge);
			float x = x0 + pad;
			for (std::size_t i = 0; i < a_st.bjHands.size(); ++i) {
				const auto& hd = a_st.bjHands[i];
				const float hw = widths[i];
				if (hd.active) {
					Chamfer(a_v, x - 4.0f, y + 4.0f, hw + 8.0f, h - 8.0f, 6.0f, Rgba(40, 160, 70, 0.16f), kBright);
				}
				const float rowW = cw * static_cast<float>(hd.cards.size()) + gap * static_cast<float>(hd.cards.size() > 0 ? hd.cards.size() - 1 : 0);
				float       cx = x + (hw - rowW) * 0.5f;
				for (int c : hd.cards) {
					HudCard(a_v, c, cx, y + pad);
					cx += cw + gap;
				}
				const std::string line = hd.result.empty() ? std::format("{} · BET {}", hd.value, Commas(hd.bet)) : std::format("{} · {}", hd.value, hd.result);
				const ImU32 col = hd.result.starts_with("WON") ? kBright : hd.result == "LOST" ? kRedText : kText;
				const float size = std::min(13.0f, 13.0f * hw / std::max(1.0f, TextWidth(Mono(), line, 13, 1.0f)));
				Print(a_v, Mono(), line, x + hw * 0.5f, y + pad + ch + 10.0f, size, col, 0, 1.0f, Align::kCentre);
				x += hw + between;
			}
		}

		void BlackjackButtons(const View& a_v, const Game::HudState& a_st, ImGuiViewport* a_viewport)
		{
			std::vector<Game::PkButton> out;
			constexpr float h = 40.0f, gap = 8.0f;
			const bool      pad = a_st.pkPad;
			auto            tag = [&](const char* a_b, const std::string& a_s) { return pad ? std::format("{} {}", a_b, a_s) : a_s; };
			const float     y = a_v.h - 52.0f;
			std::vector<std::tuple<int, std::string, float, bool, bool, ImU32>> row;  // id, label, width, on, strong, accent
			switch (a_st.bjPhase) {
			case 0:
				{
					static constexpr std::array<const char*, 4> kChips{ "+1", "+5", "+25", "+100" };
					for (int i = 0; i < 4; ++i) {
						row.emplace_back(50 + i, tag(std::array<const char*, 4>{ "(A)", "(B)", "(X)", "(Y)" }[i], kChips[i]), 80.0f, true, false, 0);
					}
					row.emplace_back(54, "TAKE BACK", 120.0f, a_st.bjBet > 0, false, Rgba(150, 30, 30));
					row.emplace_back(56, a_st.bjLastBet > 0 ? std::format("SAME BET {}", Commas(a_st.bjLastBet)) : std::string("SAME BET"), 150.0f,
						a_st.bjBet == 0 && a_st.bjLastBet > 0, false, 0);
					const std::string deal = a_st.bjBet > 0 ? (a_st.bjDealIn >= 0.0f ? std::format("DEAL · {}", static_cast<int>(std::ceil(a_st.bjDealIn))) : std::string("DEAL")) : std::string("PUT A BET DOWN");
					row.emplace_back(55, deal, 170.0f, a_st.bjBet > 0, true, 0);
					break;
				}
			case 3:
				row.emplace_back(64, tag("(A)", std::format("INSURANCE {}", Commas(a_st.bjInsurance))), 200.0f, a_st.bjInsurance > 0, true, 0);
				row.emplace_back(65, tag("(B)", "NO INSURANCE"), 180.0f, true, false, 0);
				break;
			default:
				{
					const bool turn = a_st.bjPhase == 2;
					row.emplace_back(60, tag("(A)", "HIT"), 120.0f, turn, turn, 0);
					row.emplace_back(61, tag("(B)", "STAND"), 120.0f, turn, false, 0);
					row.emplace_back(62, tag("(X)", "DOUBLE"), 130.0f, turn && a_st.bjCanDouble, false, Rgba(170, 100, 20));
					row.emplace_back(63, tag("(Y)", "SPLIT"), 120.0f, turn && a_st.bjCanSplit, false, 0);
					break;
				}
			}
			float w = 0.0f;
			for (const auto& r : row) {
				w += std::get<2>(r) + gap;
			}
			w -= gap;
			const float k = std::min(1.0f, (a_v.w - kPkMargin * 2.0f - kPkSideW) / w);
			float       x = a_v.w * 0.5f - w * k * 0.5f;
			for (const auto& [id, label, bw, on, strong, accent] : row) {
				Button(a_v, a_st, out, id, label, x, y, bw * k, h, on, strong, 14.0f, accent);
				x += (bw + gap) * k;
			}
			// the table's own, bottom right
			const int   nSide = a_st.pkKit ? 5 : 4;
			const float sx = a_v.w - kPkMargin - kPkSideW;
			float       sy = a_v.h - kPkBottom - 8.0f - (34.0f * static_cast<float>(nSide) - 6.0f);
			auto side = [&](int a_id, const std::string& a_label, bool a_on) {
				Button(a_v, a_st, out, a_id, a_label, sx, sy, kPkSideW, 28.0f, a_on, false, 12.0f);
				sy += 34.0f;
			};
			side(69, tag("(RS)", a_st.bjShowHand ? "HIDE MY HAND" : "SHOW MY HAND"), true);
			side(70, a_st.bjShowDealer ? "HIDE DEALER" : "SHOW DEALER", true);
			side(66, tag("(START)", a_st.pkTop ? "VIEW: MY PLACE" : "VIEW: FROM ABOVE"), true);
			side(67, tag("(BACK)", "LEAVE THE TABLE"), true);
			if (a_st.pkKit) {
				side(68, "PACK UP THE TABLE", a_st.bjPhase == 0);
			}
			Game::Get().SetPkButtons(std::move(out), a_viewport->Size.x, a_viewport->Size.y);
			if (a_st.pkPtrX >= 0.0f) {
				const ImVec2 p{ a_v.origin.x + a_st.pkPtrX, a_v.origin.y + a_st.pkPtrY };
				const float  s = std::max(0.6f, a_v.s);
				const ImVec2 a{ p.x, p.y }, b{ p.x, p.y + 20.0f * s }, c{ p.x + 13.0f * s, p.y + 14.0f * s };
				DL::AddTriangleFilled(a_v.dl, a, b, c, Rgba(230, 255, 230));
				DL::AddTriangle(a_v.dl, a, b, c, Rgba(10, 40, 15), std::max(1.0f, 1.5f * s));
			}
		}

		void DrawBlackjack(View a_v, const Game::HudState& a_st, ImGuiViewport* a_viewport)
		{
			const float needW = std::max(PokerStatusW(a_st) + 380.0f + PokerBoardW() + kPkMargin * 4.0f, 560.0f + kPkSideW);
			const float needH = kPkMargin + std::max(PokerStatusH(a_st), PokerHoleH() + 120.0f) + 16.0f + std::max(PokerLogH(a_st), 140.0f) + kPkBottom;
			const float f = std::min({ 1.0f, a_v.w / needW, a_v.h / needH });
			if (f < 1.0f) {
				a_v.s *= f;
				a_v.w /= f;
				a_v.h /= f;
			}
			auto slid = [&](float a_delay, float a_dir) {
				const float t = std::clamp((a_st.pkAge - a_delay) / 0.45f, 0.0f, 1.0f);
				const float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
				View v = a_v;
				v.origin.x += a_dir * (1.0f - e) * 420.0f * a_v.s;
				return v;
			};
			PokerStatus(slid(0.0f, -1.0f), a_st);
			PokerBoard(slid(0.08f, 1.0f), a_st);
			PokerLog(slid(0.16f, -1.0f), a_st);
			BjDealer(a_v, a_st);  // (no turn panel up top, as at poker: the buttons show when it's yours)
			BjMine(a_v, a_st);
			PokerBanner(a_v, a_st);
			BlackjackButtons(a_v, a_st, a_viewport);
		}

		// ------------------------------------------------------------ a slot machine

		void SlotPaytable(const View& a_v, const Game::HudState& a_st)
		{
			// what pays, right, under the jackpot
			static constexpr std::array<std::pair<const char*, const char*>, 10> kRows{ {
				{ "7  7  7", "JACKPOT" },
				{ "BAR BAR BAR", "50 x" },
				{ "BELL x3", "20 x" },
				{ "CHERRY x3", "15 x" },
				{ "PLUM x3", "12 x" },
				{ "ORANGE x3", "10 x" },
				{ "LEMON x3", "8 x" },
				{ "2 CHERRIES", "4 x" },
				{ "1 CHERRY", "1 x" },
				{ "", "" },
			} };
			const float w = 230.0f, h = 30.0f + 20.0f * 9 + 34.0f;
			const float x = a_v.w - kPkMargin - w, y = kPkMargin + PokerBoardH() + 10.0f;
			Chamfer(a_v, x, y, w, h, 10.0f, kPanel, kEdge);
			Print(a_v, Mono(), "PAYS (TIMES THE BET)", x + 14, y + 10, 11, kDim, 0, 2.0f);
			float row = y + 30.0f;
			for (int i = 0; i < 9; ++i) {
				const ImU32 col = i == 0 ? kBright : kText;
				Print(a_v, Mono(), kRows[i].first, x + 14, row, 12, col, 0, 1.0f);
				Print(a_v, Mono(), kRows[i].second, x + w - 14, row, 12, col, 0, 1.0f, Align::kRight);
				row += 20.0f;
			}
			Print(a_v, Mono(), std::format("THE JACKPOT AT {} A PULL;", a_st.slMaxBet), x + 14, row + 2, 10, kDim, 0, 1.0f);
			Print(a_v, Mono(), "LESS: 100 x THE BET", x + 14, row + 16, 10, kDim, 0, 1.0f);
		}

		void SlotBanner(const View& a_v, const Game::HudState& a_st)
		{
			// the pull's result, top middle (clear of the reels)
			if (a_st.banner == 0) {
				return;
			}
			const bool  won = a_st.banner == 2;
			const float alpha = std::min(1.0f, a_st.bannerAge * 4.0f) * std::clamp((6.0f - a_st.bannerAge) / 0.6f, 0.0f, 1.0f);
			if (alpha <= 0.0f) {
				return;
			}
			const float w = std::max({ 340.0f, TextWidth(Title(), a_st.bannerTitle, 26, 3.0f) + 60.0f, TextWidth(Mono(), a_st.bannerSub, 13, 2.0f) + 60.0f });
			const float cx = a_v.w * 0.5f, y = kPkMargin;
			Chamfer(a_v, cx - w * 0.5f, y, w, 80, 14, Fade(kPanel, alpha), Fade(won ? kBright : kEdge, alpha));
			Print(a_v, Title(), a_st.bannerTitle, cx, y + 10, 26, Fade(won ? kBright : kText, alpha), Fade(won ? kGlow : 0, alpha), 3.0f, Align::kCentre);
			if (!a_st.bannerSub.empty()) {
				Print(a_v, Mono(), a_st.bannerSub, cx, y + 52, 13, Fade(kText, alpha), 0, 2.0f, Align::kCentre);
			}
		}

		void SlotButtons(const View& a_v, const Game::HudState& a_st, ImGuiViewport* a_viewport)
		{
			std::vector<Game::PkButton> out;
			constexpr float h = 40.0f, gap = 8.0f;
			const bool      pad = a_st.pkPad;
			const float     y = a_v.h - 52.0f;
			static constexpr std::array<int, 4> kBets{ 1, 5, 10, 25 };
			std::vector<std::tuple<int, std::string, float, bool, bool>> row;  // id, label, width, on, strong
			for (int i = 0; i < 4; ++i) {
				const bool picked = a_st.slBet == kBets[i];
				row.emplace_back(90 + i, std::format("{}BET {}", pad ? "" : std::format("{} · ", i + 1), kBets[i]), 104.0f, !a_st.slSpinning || picked, picked);
			}
			row.emplace_back(94, a_st.slSpinning ? std::string("SPINNING...") : (pad ? std::format("(A) PULL · {}", a_st.slBet) : std::format("PULL · {}", a_st.slBet)), 190.0f,
				!a_st.slSpinning && a_st.caps >= a_st.slBet, true);
			float w = 0.0f;
			for (const auto& r : row) {
				w += std::get<2>(r) + gap;
			}
			w -= gap;
			const float k = std::min(1.0f, (a_v.w - kPkMargin * 2.0f - kPkSideW) / w);
			float       x = a_v.w * 0.5f - w * k * 0.5f;
			for (const auto& [id, label, bw, on, strong] : row) {
				Button(a_v, a_st, out, id, label, x, y, bw * k, h, on, strong, 14.0f);
				x += (bw + gap) * k;
			}
			const float sx = a_v.w - kPkMargin - kPkSideW;
			const float sy = a_v.h - kPkBottom - 8.0f - 28.0f;
			Button(a_v, a_st, out, 95, pad ? "(B) GET UP" : "GET UP", sx, sy, kPkSideW, 28.0f, true, false, 12.0f);
			Game::Get().SetPkButtons(std::move(out), a_viewport->Size.x, a_viewport->Size.y);
			if (a_st.pkPtrX >= 0.0f) {
				const ImVec2 p{ a_v.origin.x + a_st.pkPtrX, a_v.origin.y + a_st.pkPtrY };
				const float  s = std::max(0.6f, a_v.s);
				const ImVec2 a{ p.x, p.y }, b{ p.x, p.y + 20.0f * s }, c{ p.x + 13.0f * s, p.y + 14.0f * s };
				DL::AddTriangleFilled(a_v.dl, a, b, c, Rgba(230, 255, 230));
				DL::AddTriangle(a_v.dl, a, b, c, Rgba(10, 40, 15), std::max(1.0f, 1.5f * s));
			}
		}

		void DrawSlots(View a_v, const Game::HudState& a_st, ImGuiViewport* a_viewport)
		{
			const float needW = PokerStatusW(a_st) + 340.0f + 230.0f + kPkMargin * 4.0f;
			const float needH = kPkMargin + std::max(PokerStatusH(a_st), PokerBoardH() + 260.0f) + 16.0f + std::max(PokerLogH(a_st), 140.0f) + kPkBottom;
			const float f = std::min({ 1.0f, a_v.w / needW, a_v.h / needH });
			if (f < 1.0f) {
				a_v.s *= f;
				a_v.w /= f;
				a_v.h /= f;
			}
			auto slid = [&](float a_delay, float a_dir) {
				const float t = std::clamp((a_st.pkAge - a_delay) / 0.45f, 0.0f, 1.0f);
				const float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
				View v = a_v;
				v.origin.x += a_dir * (1.0f - e) * 420.0f * a_v.s;
				return v;
			};
			PokerStatus(slid(0.0f, -1.0f), a_st);
			PokerBoard(slid(0.08f, 1.0f), a_st);
			SlotPaytable(slid(0.12f, 1.0f), a_st);
			PokerLog(slid(0.16f, -1.0f), a_st);
			SlotBanner(a_v, a_st);
			SlotButtons(a_v, a_st, a_viewport);
		}

		// ------------------------------------------------------------ plain fallback (the art didn't load)

		constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
		                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

		void DrawPlain(const Game::HudState& a_st, ImGuiViewport* a_viewport)
		{
			SetNextWindowPos(ImVec2{ a_viewport->Pos.x + 24.0f, a_viewport->Pos.y + 24.0f }, ImGuiCond_Always, ImVec2{ 0.0f, 0.0f });
			SetNextWindowBgAlpha(0.6f);
			if (Begin("##fo4roulette", nullptr, kFlags)) {
				static constexpr std::array<int, 4> chips{ 1, 5, 25, 100 };
				if (a_st.poker) {
					Text("Caps %d  %s  pot %d", a_st.caps, a_st.pkInfo.c_str(), a_st.pkPot);
					std::string board;
					for (int c : a_st.pkBoard) {
						board += pk::CardName(static_cast<pk::Card>(c)) + " ";
					}
					if (a_st.pkHole[0] >= 0) {
						Text("You: %s %s   board: %s  %s", pk::CardName(static_cast<pk::Card>(a_st.pkHole[0])).c_str(),
							pk::CardName(static_cast<pk::Card>(a_st.pkHole[1])).c_str(), board.c_str(), a_st.pkHand.c_str());
					}
					for (const auto& r : a_st.pkRows) {
						Text("%s%s  %d  bet %d  %s", r.bigBlind ? "(B) " : "", r.name.c_str(), r.stack, r.bet, r.status.c_str());
					}
					TextUnformatted(a_st.keys.c_str());
				} else {
					Text("Caps %d  (on the table %d)  chip %d", a_st.caps, a_st.onTable, chips[a_st.chip]);
				}
				if (!a_st.hover.empty()) {
					Text("%s  pays %d to 1", a_st.hover.c_str(), a_st.hoverPays);
				}
				if (!a_st.bannerTitle.empty() && a_st.banner != 0) {
					Text("%s  %s", a_st.bannerTitle.c_str(), a_st.bannerSub.c_str());
				}
				for (const auto& l : a_st.log) {
					TextUnformatted(l.text.c_str());
				}
			}
			End();
		}

		void __stdcall Draw()
		{
			const auto st = Game::Get().Hud();
			auto*      viewport = GetMainViewport();
			if (!viewport || !(st.seated || st.nearby || st.placing)) {
				return;
			}
			Texture(Title().file);
			if (g_artMissing) {
				if (st.seated) {
					DrawPlain(st, viewport);
				}
				return;
			}
			View v;
			v.dl = GetBackgroundDrawList();
			if (!v.dl) {
				return;
			}
			v.s = std::max(0.1f, viewport->Size.y / 720.0f * Settings::Get().hudScale);
			v.origin = viewport->Pos;
			v.w = viewport->Size.x / v.s;
			v.h = viewport->Size.y / v.s;
			if (st.placing) {
				Keys(v, st);  // setting a kit table up: what the keys do
				return;
			}
			if (st.poker) {
				DrawPoker(v, st, viewport);
				return;
			}
			if (st.blackjack) {
				DrawBlackjack(v, st, viewport);
				return;
			}
			if (st.slots) {
				DrawSlots(v, st, viewport);
				return;
			}
			History(v, st);
			Banner(v, st);
			if (st.seated) {
				Status(v, st);
				MyBets(v, st);
				Hover(v, st);
				RouletteButtons(v, st, viewport);
				Log(v, st, v.h - 140.0f);
			} else {
				Log(v, st, v.h - 24.0f);
			}
		}

		F4SEMenuFramework::Model::HudElement* g_element{ nullptr };
	}

	bool Showing()
	{
		return g_element && !g_artMissing;
	}

	void Install()
	{
		if (g_element) {
			return;
		}
		if (!F4SEMenuFramework::IsInstalled()) {
			log::warn("F4SE Menu Framework not found: no HUD on screen (results come as notifications)");
			return;
		}
		g_element = F4SEMenuFramework::AddHudElement(Draw);
		log::info("HUD element registered");
		SettingsMenu::Install();
	}
}
