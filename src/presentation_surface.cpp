/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "presentation_surface.h"

#include "presentation_geometry.h"
#include "snes_style.h"
#include "theme.h"
#include "theme_tone_mapper.h"

#include <QImageReader>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace
{

QRect coverCrop(const QSize& source, const QSize& target, const QPointF& focal_point)
{
	if (source.isEmpty() || target.isEmpty()) {
		return {};
	}
	const qreal scale = std::max(
		static_cast<qreal>(target.width()) / source.width(),
		static_cast<qreal>(target.height()) / source.height());
	const int width = std::max(1, qRound(target.width() / scale));
	const int height = std::max(1, qRound(target.height() / scale));
	const qreal focal_x = std::clamp(focal_point.x(), 0.0, 1.0);
	const qreal focal_y = std::clamp(focal_point.y(), 0.0, 1.0);
	const int x = qRound((source.width() - width) * focal_x);
	const int y = qRound((source.height() - height) * focal_y);
	return QRect(x, y, width, height).intersected(QRect(QPoint(), source));
}

std::uint32_t pixelHash(std::uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	return value ^ (value >> 16);
}

// The classic 4x4 ordered-dither matrix. Godrays and every other "soft"
// motion effect must dither between palette roles instead of blending.
constexpr int kBayer4x4[4][4] = {
	{ 0, 8, 2, 10 },
	{ 12, 4, 14, 6 },
	{ 3, 11, 1, 9 },
	{ 15, 7, 13, 5 }
};

// Integer back-and-forth sweep in [0, amplitude]; zero at phase zero so the
// authored tick-zero frame carries no sway offset.
int triangleWave(int phase, int amplitude)
{
	if (amplitude <= 0) {
		return 0;
	}
	const int period = amplitude * 2;
	const int wrapped = ((phase % period) + period) % period;
	return wrapped < amplitude ? wrapped : period - wrapped;
}

struct GodrayBeam
{
	int offset;    // horizontal anchor shift from the shared sun column
	int slope_num; // leftward lean per scanline as num/den
	int slope_den;
	int width;     // base half-width; beams widen as they descend
};

// Dithered light shafts: inside each beam, pixels matching a palette role
// are swapped for that role's authored lighter partner wherever the Bayer
// threshold passes. Pure palette cycling — no alpha, no new colors.
void applyGodrays(
	QImage& image,
	int sun_x,
	int sway,
	const GodrayBeam* beams,
	int beam_count,
	const QRgb* from_roles,
	const QRgb* to_roles,
	int role_count)
{
	const int width = image.width();
	const int height = image.height();
	for (int y = 0; y < height; ++y) {
		QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int beam = 0; beam < beam_count; ++beam) {
			const GodrayBeam& ray = beams[beam];
			const int center = sun_x + ray.offset + sway - (y * ray.slope_num) / ray.slope_den;
			const int half_width = ray.width + y / 16;
			const int first = std::max(0, center - half_width);
			const int last = std::min(width - 1, center + half_width);
			for (int x = first; x <= last; ++x) {
				const int level = (std::abs(x - center) * 2 <= half_width) ? 3 : 1;
				if (kBayer4x4[x & 3][y & 3] >= level) {
					continue;
				}
				for (int role = 0; role < role_count; ++role) {
					if (line[x] == from_roles[role]) {
						line[x] = to_roles[role];
						break;
					}
				}
			}
		}
	}
}

inline int bayer(int x, int y)
{
	return kBayer4x4[x & 3][y & 3];
}

// Replace every `from` pixel in the rectangle with `to` wherever the ordered
// Bayer threshold passes. The threshold callable returns 0..16 per pixel, so
// callers paint soft gradients, haze, and shaded volume without ever leaving
// the authored palette or introducing a blended colour.
template <typename Threshold>
void ditherReplace(QImage& image, int x0, int y0, int x1, int y1,
	QRgb from, QRgb to, Threshold threshold)
{
	x0 = std::max(0, x0);
	y0 = std::max(0, y0);
	x1 = std::min(image.width(), x1);
	y1 = std::min(image.height(), y1);
	for (int y = y0; y < y1; ++y) {
		QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = x0; x < x1; ++x) {
			if (line[x] == from && bayer(x, y) < threshold(x, y)) {
				line[x] = to;
			}
		}
	}
}

// Grow a stochastic one-pixel fringe of `target` into neighbouring `from`
// pixels so filled blossom puffs dissolve into the sky along a soft, feathered
// edge instead of a hard vector rim. Neighbours are sampled from a snapshot so
// the fringe never cascades and stays exactly one pixel deep.
void ditherFringe(QImage& image, int x0, int y0, int x1, int y1,
	QRgb from, QRgb target, int level)
{
	const QImage snapshot = image;
	x0 = std::max(1, x0);
	y0 = std::max(1, y0);
	x1 = std::min(image.width() - 1, x1);
	y1 = std::min(image.height() - 1, y1);
	for (int y = y0; y < y1; ++y) {
		QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		const QRgb* row = reinterpret_cast<const QRgb*>(snapshot.scanLine(y));
		const QRgb* above = reinterpret_cast<const QRgb*>(snapshot.scanLine(y - 1));
		const QRgb* below = reinterpret_cast<const QRgb*>(snapshot.scanLine(y + 1));
		for (int x = x0; x < x1; ++x) {
			if (row[x] != from || bayer(x, y) >= level) {
				continue;
			}
			if (row[x - 1] == target || row[x + 1] == target
					|| above[x] == target || below[x] == target) {
				line[x] = target;
			}
		}
	}
}

// A gently rolling ridge offset: two summed sine terms keep the silhouette
// organic while remaining a pure function of x, so hill crests never jitter
// between frames.
int ridgeOffset(int x, int width, double freq, double phase, double amplitude)
{
	const double t = static_cast<double>(x) / std::max(1, width);
	const double wave = std::sin((t * freq + phase) * 6.2831853)
		+ 0.45 * std::sin((t * freq * 2.7 + phase * 1.9) * 6.2831853);
	return static_cast<int>(std::lround(wave * amplitude * 0.5));
}

void drawGeneratedMeadow(QImage& image, SnesColor fallback, int tick)
{
	image.fill(SnesStyle::toQColor(fallback));
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, false);
	const QColor cloud = SnesStyle::toQColor(SnesColor::fromComponents(31, 30, 27));
	const QColor cloud_shadow = SnesStyle::toQColor(SnesColor::fromComponents(24, 27, 28));
	const QColor far_grass = SnesStyle::toQColor(SnesColor::fromComponents(21, 28, 19));
	const QColor grass = SnesStyle::toQColor(SnesColor::fromComponents(14, 24, 12));
	const QColor dark_grass = SnesStyle::toQColor(SnesColor::fromComponents(9, 19, 9));
	const QColor flower_a = SnesStyle::toQColor(SnesColor::fromComponents(30, 22, 18));
	const QColor flower_b = SnesStyle::toQColor(SnesColor::fromComponents(26, 21, 29));
	const QColor flower_c = SnesStyle::toQColor(SnesColor::fromComponents(30, 28, 15));

	const int width = image.width();
	const int height = image.height();
	const int horizon = (height * 5) / 8;
	painter.fillRect(0, horizon, width, height - horizon, far_grass);
	for (int x = 0; x < width; x += 17) {
		const int hill_height = 3 + ((x / 17) % 5);
		painter.fillRect(x, horizon - hill_height, 23, hill_height + 1, far_grass);
	}
	painter.fillRect(0, horizon + (height / 10), width, height, grass);
	painter.fillRect(0, height - std::max(2, height / 12), width, height, dark_grass);

	// Clouds drift leftward in whole pixels; each puff is drawn twice one
	// span apart so the band wraps without ever opening a gap.
	const int cloud_span = width + 43;
	const int cloud_shift = (tick / 3) % cloud_span;
	for (int x = 8; x < width; x += 43) {
		const int y = 8 + ((x / 43) % 3) * 13;
		for (const int sx : { x - cloud_shift, x - cloud_shift + cloud_span }) {
			painter.fillRect(sx + 2, y, 9, 3, cloud_shadow);
			painter.fillRect(sx, y - 2, 8, 4, cloud);
			painter.fillRect(sx + 5, y - 4, 6, 5, cloud);
			painter.fillRect(sx + 10, y - 1, 5, 3, cloud);
		}
	}

	for (int x = 5; x < width; x += 19) {
		const int y = horizon + 5 + ((x * 7) % std::max(6, height - horizon - 8));
		painter.fillRect(x, y, 1, 4, dark_grass);
		const QColor blossom = ((x / 19) % 3 == 0) ? flower_a : (((x / 19) % 3 == 1) ? flower_b : flower_c);
		painter.fillRect(x - 1, y - 1, 3, 2, blossom);
	}

	// Grass shimmer: sparse light cells cycle across the field so the blades
	// appear to catch sunlight; the hash keys on the shimmer cadence.
	for (int x = 0; x < width; x += 2) {
		const std::uint32_t hash = pixelHash(
			static_cast<std::uint32_t>(x) * 53u + static_cast<std::uint32_t>(tick / 3) * 191u + 7u);
		if (hash % 7u == 0u) {
			const int band = std::max(4, height - horizon - 6);
			const int y = horizon + 3 + static_cast<int>((hash >> 8) % static_cast<std::uint32_t>(band));
			painter.fillRect(x, y, 1, 2, far_grass);
		}
	}
	painter.end();

	// Godrays lean rightward from a sun beyond the upper-left corner.
	const GodrayBeam beams[] = {
		{ 0, -1, 3, 3 },
		{ width / 4, -1, 4, 2 },
		{ (width * 5) / 12, -2, 7, 2 }
	};
	const QRgb from_roles[] = { SnesStyle::toQColor(fallback).rgb(), grass.rgb() };
	const QRgb to_roles[] = { cloud.rgb(), far_grass.rgb() };
	applyGodrays(image, width / 6, triangleWave(tick / 8, 10), beams, 3, from_roles, to_roles, 2);
}

// One lobe of a blossom canopy. Offsets and radius are hundredths of the tree's
// scale unit, so a canopy keeps its shape at any size. dx is measured toward the
// document, letting a single description serve both framing trees by mirroring.
struct CanopyPuff
{
	int dx;
	int dy;
	int r;
};

void drawCherryBlossom(QImage& image, const ThemePalette& palette, int tick, QRect clearing)
{
	const QRgb sky = palette.background.rgb();
	const QRgb pink = palette.secondary.rgb();
	const QRgb field = palette.primary.rgb();
	const QRgb shade = palette.accent.rgb();
	const QRgb dark = palette.deep.rgb();
	const QRgb warm = palette.ui_text.rgb();

	image.fill(palette.background);
	const int width = image.width();
	const int height = image.height();

	// Without real document geometry (theme previews, thumbnails, palette
	// checks) fall back to a centred writing surface of typical proportions so
	// the framing trees still compose sensibly.
	if (clearing.isEmpty()) {
		const int cw = (width * 58) / 100;
		const int ch = (height * 90) / 100;
		clearing = QRect((width - cw) / 2, (height - ch) / 2, cw, ch);
	}
	const int doc_left = std::clamp(clearing.left(), 0, width);
	const int doc_right = std::clamp(clearing.left() + clearing.width(), 0, width);
	const int doc_top = std::clamp(clearing.top(), 0, height);
	const int margin_l = std::max(8, doc_left);
	const int margin_r = std::max(8, width - doc_right);
	const int sun_x = (doc_left + doc_right) / 2;

	// Depth bands of the zen lake garden. The sky meets a misty mountain range
	// at the horizon; a grassy bank rolls down to the water; the still lake
	// fills the foreground and reflects everything above it.
	const int horizon = (height * 46) / 100;
	const int waterline = (height * 68) / 100;
	const int lake_h = std::max(1, height - waterline);

	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setPen(Qt::NoPen);

	// Soft pastel clouds: each cloud is a cluster of rounded lobes so it reads
	// as one billowing form rather than a scattered dash. The band is drawn
	// twice one span apart so it wraps seamlessly as it drifts leftward.
	const int cloud_span = width + 96;
	const int cloud_shift = (tick / 4) % cloud_span;
	painter.setBrush(palette.secondary);
	for (int i = 0; i < 5; ++i) {
		const int anchor = 8 + (i * (width + 20)) / 5;
		const int cy = 6 + ((i * 43) % 22);
		const int cw = std::max(10, width / 9 + (i % 3) * (width / 26));
		const int ch = std::max(3, cw / 4);
		for (const int base : { anchor - cloud_shift, anchor - cloud_shift + cloud_span }) {
			painter.drawEllipse(base, cy, cw, ch);
			painter.drawEllipse(base + cw / 3, cy - ch, (cw * 3) / 5, ch + ch / 2);
			painter.drawEllipse(base + (cw * 3) / 5, cy - ch / 2, cw / 2, ch);
		}
	}

	// Far mountain range: tall, slow-rolling summed-sine peaks filled solid, to
	// be dissolved into mist afterward. Behind everything else.
	const double mtn_amp = std::max(4.0, height * 11.0 / 100);
	const int mtn_lift = (height * 5) / 100;
	for (int x = 0; x < width; ++x) {
		const int crest = horizon - mtn_lift + ridgeOffset(x, width, 0.5, 0.15, mtn_amp);
		painter.fillRect(x, crest, 1, horizon - crest, palette.primary);
	}

	// Grassy bank from the horizon down to the shore, then the lake surface as a
	// flat reflective sheet of the sky tone.
	painter.fillRect(0, horizon, width, waterline - horizon, palette.primary);
	painter.fillRect(0, waterline, width, height - waterline, palette.background);

	// Rolling near hills at the shore's far edge, in front of the mountains.
	const int hill_base = horizon - std::max(1, (height * 1) / 100);
	const double hill_amp = std::max(3.0, height * 5.0 / 100);
	for (int x = 0; x < width; ++x) {
		const int crest = hill_base + ridgeOffset(x, width, 1.1, 0.6, hill_amp);
		painter.fillRect(x, crest, 1, horizon - crest, palette.primary);
	}
	painter.end();

	// Mist: the meadow green above the horizon dissolves into the sky, densest
	// low and thinning with altitude, so the peaks fade to distant haze.
	const int mtn_span = std::max(6, mtn_lift + static_cast<int>(mtn_amp));
	ditherReplace(image, 0, 0, width, horizon, field, sky,
		[&](int, int y) {
			return std::clamp(((horizon - y) * 20) / mtn_span, 0, 15);
		});

	// A distant waterfall threads down the left mountainside when the margin has
	// room: a bright ribbon of falling water between wet-rock banks, its sparkle
	// scrolling downward, ending in a small foaming pool at the treeline.
	if (margin_l >= 40) {
		const int cliff_x = std::clamp(margin_l / 4, 3, width - 4);
		const int fall_top = horizon - std::max(8, (height * 17) / 100);
		for (int y = fall_top; y < horizon; ++y) {
			QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
			for (int dx = -2; dx <= 2; ++dx) {
				const int x = cliff_x + dx;
				if (x < 0 || x >= width) {
					continue;
				}
				if (std::abs(dx) == 2) {
					if (bayer(x, y) < 9) {
						line[x] = dark; // wet rock channel wall
					}
				} else {
					line[x] = sky; // falling water
				}
			}
			if (((y - tick) & 3) == 0 && cliff_x < width) {
				line[cliff_x] = warm; // sparkle sliding down the fall
			}
		}
		// Foaming pool where the water lands.
		for (int dy = 0; dy < std::max(2, height / 60); ++dy) {
			const int y = horizon + dy;
			if (y >= height) {
				break;
			}
			QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
			for (int dx = -4; dx <= 4; ++dx) {
				const int x = cliff_x + dx;
				if (x < 0 || x >= width) {
					continue;
				}
				const std::uint32_t h = pixelHash(static_cast<std::uint32_t>(x) * 5u
					+ static_cast<std::uint32_t>(y) * 3u + static_cast<std::uint32_t>(tick / 2) * 61u);
				if (static_cast<int>(h % 10u) < 5 - std::abs(dx)) {
					line[x] = (h & 8u) ? warm : sky;
				}
			}
		}
	}

	// Grade the grassy bank from bright meadow near the horizon down toward the
	// shade role at the water's edge, by ordered dither so there is no seam.
	const int grade_top = horizon + std::max(2, (height * 6) / 100);
	ditherReplace(image, 0, grade_top, width, waterline, field, shade,
		[&](int, int y) {
			const int span = std::max(1, waterline - grade_top);
			return std::clamp(((y - grade_top) * 15) / span, 0, 15);
		});

	// The lake surface: a bright, still sheet of reflected sky. Only thin ripple
	// lines of the meadow tone scroll slowly downward, so the water stays
	// luminous and clearly distinct from the darker green bank above it, letting
	// reflections and sun-glitter read against it.
	for (int y = waterline; y < height; ++y) {
		QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		const int depth = y - waterline;
		const int band = (y * 3 - tick / 2) & 15;
		int base_level = 1 + (depth * 3) / lake_h;
		if (band < 2) {
			base_level = 0; // bright reflection streak stays pure sky
		} else if (band < 5) {
			base_level += 2; // faint ripple trough reads a touch greener
		}
		for (int x = 0; x < width; ++x) {
			if (line[x] == sky && bayer(x, y) < base_level) {
				line[x] = field;
			}
		}
	}
	// A crisp shoreline: the water's edge darkens where it meets the bank.
	ditherReplace(image, 0, waterline, width, waterline + std::max(1, lake_h / 20), sky, shade,
		[&](int, int y) { return std::max(0, 9 - (y - waterline) * 3); });

	// A shimmering column of sun-glitter lies on the water beneath the sun, its
	// sparkle scrolling toward the viewer.
	const int glint_half = std::max(3, width / 14);
	for (int y = waterline; y < height; ++y) {
		QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		if (((y - tick / 3) & 3) != 0) {
			continue;
		}
		const int first = std::max(0, sun_x - glint_half);
		const int last = std::min(width - 1, sun_x + glint_half);
		for (int x = first; x <= last; ++x) {
			if (line[x] != sky && line[x] != field) {
				continue;
			}
			const int near_axis = glint_half - std::abs(x - sun_x);
			const std::uint32_t h = pixelHash(static_cast<std::uint32_t>(x) * 13u
				+ static_cast<std::uint32_t>(y) * 7u + static_cast<std::uint32_t>(tick / 2) * 101u);
			if (static_cast<int>(h % 20u) < (near_axis * 6) / glint_half) {
				line[x] = warm;
			}
		}
	}

	// Rocks: rounded stones with a lit cap and a broken reflection in the water.
	// A cluster settles at each shore beneath the trees, with a couple standing
	// in the shallows.
	auto drawRock = [&](int cx, int cy, int rw, int rh) {
		QPainter rock(&image);
		rock.setRenderHint(QPainter::Antialiasing, false);
		rock.setPen(Qt::NoPen);
		rock.setBrush(palette.deep);
		rock.drawEllipse(QPoint(cx, cy), rw, rh);
		rock.setBrush(palette.accent);
		rock.drawEllipse(QPoint(cx - rw / 4, cy - rh / 3), (rw * 3) / 5, (rh * 2) / 5);
		rock.end();
		// Warm sun-catch on the upper rim.
		ditherReplace(image, cx - rw, cy - rh, cx + rw, cy, shade, warm,
			[&](int x, int y) {
				const int lit = (x - cx) + (cy - y);
				return lit <= 0 ? 0 : std::min(2, lit / std::max(1, rw));
			});
		// Reflection: a short, broken deep smear fading below the rock.
		for (int y = cy + rh; y < std::min(height, cy + rh + rh); ++y) {
			if (((y + tick / 3) & 1) == 0) {
				continue;
			}
			QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
			const int level = std::max(0, 7 - (y - cy - rh) * 3);
			for (int x = std::max(0, cx - rw + 1); x <= std::min(width - 1, cx + rw - 1); ++x) {
				if ((line[x] == sky || line[x] == field) && bayer(x, y) < level) {
					line[x] = dark;
				}
			}
		}
	};
	drawRock((margin_l * 3) / 5, waterline + 1, std::max(3, margin_l / 5), std::max(2, margin_l / 9));
	drawRock(width - (margin_r * 3) / 5, waterline + 2, std::max(3, margin_r / 6), std::max(2, margin_r / 10));
	drawRock(sun_x - width / 7, waterline + lake_h / 6, std::max(2, width / 40), std::max(1, width / 70));
	drawRock(sun_x + width / 8, waterline + lake_h / 4, std::max(2, width / 44), std::max(1, width / 80));

	// A flowering tree enshrines each side of the writing surface: the trunk
	// rises like a pillar from the shore, the crown leans inward and arches over
	// the document's top corner, and the blossom is mirrored in the still water.
	// `facing` is +1 for the left tree and -1 for the right, mirroring one
	// description; the trunk is rooted at `base_y` on the bank.
	auto drawTree = [&](int trunk_base_x, int crown_x, int crown_y, int unit, int facing, int base_y) {
		const int inward = facing; // +x points toward the document for this tree

		QPainter tree(&image);
		tree.setRenderHint(QPainter::Antialiasing, false);
		tree.setPen(Qt::NoPen);

		// Trunk: a tapered pillar leaning gently inward toward the crown.
		const int trunk_half = std::max(2, (unit * 13) / 100);
		const int fork_y = crown_y + (unit * 55) / 100;
		QPolygon trunk;
		trunk << QPoint(trunk_base_x - trunk_half, base_y)
			<< QPoint(crown_x - trunk_half / 2, fork_y)
			<< QPoint(crown_x + trunk_half / 2, fork_y)
			<< QPoint(trunk_base_x + trunk_half + trunk_half / 3, base_y);
		tree.setBrush(palette.deep);
		tree.drawPolygon(trunk);

		// Boughs fork up and inward from the trunk into the canopy. dx is stored
		// toward the document and mirrored by `inward`.
		const QPoint fork(crown_x, fork_y);
		const int bough_w = std::max(1, (unit * 7) / 100);
		struct Bough { int dx; int dy; int taper; };
		const Bough boughs[] = {
			{ -40, -52, 3 }, { 14, -74, 2 }, { 52, -38, 3 }, { 66, 16, 2 }
		};
		for (const Bough& b : boughs) {
			const QPoint tip(crown_x + inward * (unit * b.dx) / 100, crown_y + (unit * b.dy) / 100);
			tree.setPen(QPen(palette.deep, bough_w * b.taper, Qt::SolidLine, Qt::RoundCap));
			tree.drawLine(fork, tip);
		}
		tree.setPen(Qt::NoPen);

		// Canopy: overlapping puffs form one rounded, softly drooping blossom
		// mass. Lower lobes cascade down the margin so the pillar of blossom
		// fills the tall side strip.
		const CanopyPuff puffs[] = {
			{ 0, 0, 64 }, { -46, 10, 46 }, { 50, 6, 48 },
			{ -22, -42, 40 }, { 30, -38, 42 }, { 58, 30, 40 },
			{ 12, 50, 44 }, { -52, 42, 34 }, { 26, -70, 30 }
		};
		int bx0 = width;
		int by0 = height;
		int bx1 = 0;
		int by1 = 0;
		tree.setBrush(palette.secondary);
		for (const CanopyPuff& puff : puffs) {
			const int px = crown_x + inward * (unit * puff.dx) / 100;
			const int py = crown_y + (unit * puff.dy) / 100;
			const int r = std::max(2, (unit * puff.r) / 100);
			tree.drawEllipse(QPoint(px, py), r, r);
			bx0 = std::min(bx0, px - r - 2);
			by0 = std::min(by0, py - r - 2);
			bx1 = std::max(bx1, px + r + 2);
			by1 = std::max(by1, py + r + 2);
		}
		tree.end();

		// Feather the silhouette into the sky as stippled blossom.
		ditherFringe(image, bx0, by0, bx1, by1, sky, pink, 9);

		// Volume: a warm sunlit rim gathers on the inward, sun-facing upper side;
		// a soft deep shadow settles under the outward, lower lobes. Both are
		// sparse ordered dither so the pink never flattens into a wash.
		const int falloff = std::max(1, (unit * 55) / 100);
		ditherReplace(image, bx0, by0, bx1, by1, pink, warm,
			[&](int x, int y) {
				const int lit = inward * (x - crown_x) + (crown_y - y);
				return lit <= 0 ? 0 : std::min(2, lit / falloff);
			});
		ditherReplace(image, bx0, by0, bx1, by1, pink, dark,
			[&](int x, int y) {
				const int low = -inward * (x - crown_x) + (y - crown_y);
				return low <= 0 ? 0 : std::min(2, low / falloff);
			});

		// A few blossoms slowly catch the light and twinkle warm on a lazy
		// cadence; seeding on the crown keeps the two trees out of lockstep.
		for (int i = 0; i < 14; ++i) {
			const std::uint32_t h = pixelHash(static_cast<std::uint32_t>(i) * 2654435761u
				+ static_cast<std::uint32_t>(crown_x) * 71u
				+ static_cast<std::uint32_t>(tick / 8) * 40503u);
			const int tx = bx0 + static_cast<int>(h % static_cast<std::uint32_t>(std::max(1, bx1 - bx0)));
			const int ty = by0 + static_cast<int>((h >> 12) % static_cast<std::uint32_t>(std::max(1, by1 - by0)));
			if (tx >= 0 && ty >= 0 && tx < width && ty < height) {
				QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(ty));
				if (line[tx] == pink) {
					line[tx] = warm;
				}
			}
		}

		// Reflection: the blossom mass shimmers back off the water directly below
		// the crown, broken by ripple lines so it never mirrors too crisply.
		const int refl_h = (unit * 60) / 100;
		ditherReplace(image, crown_x - unit, waterline, crown_x + unit,
			std::min(height, waterline + refl_h), sky, pink,
			[&](int x, int y) {
				const int dv = y - waterline;
				const int hx = std::abs(x - crown_x);
				int t = 6 - dv / std::max(1, (unit * 20) / 100) - hx / std::max(1, (unit * 16) / 100);
				if (((y + tick / 3) & 3) == 0) {
					t -= 3;
				}
				return std::max(0, t);
			});
		// A broken plum reflection of the trunk continues under the shore.
		for (int y = base_y; y < std::min(height, base_y + refl_h / 2); ++y) {
			if (((y + tick / 2) & 1) == 0) {
				continue;
			}
			QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
			const int rx = trunk_base_x + (crown_x - trunk_base_x) / 6;
			for (int x = std::max(0, rx - trunk_half / 2); x <= std::min(width - 1, rx + trunk_half / 2); ++x) {
				if (line[x] == sky || line[x] == field) {
					line[x] = dark;
				}
			}
		}
	};

	// Scale each tree to its margin, but a touch larger so the crown overflows
	// inward and arches over the document corner. Crowns sit high in the strip;
	// trunks are rooted on the bank at the water's edge.
	const int unit_l = std::max(12, (margin_l * 3) / 4);
	const int unit_r = std::max(12, (margin_r * 3) / 4);
	const int crown_y_l = std::max((unit_l * 65) / 100, doc_top + (height * 2) / 100);
	const int crown_y_r = std::max((unit_r * 65) / 100, doc_top + (height * 2) / 100);
	drawTree((margin_l * 2) / 5, margin_l / 2, crown_y_l, unit_l, +1, waterline + 1);
	drawTree(width - (margin_r * 2) / 5, width - margin_r / 2, crown_y_r, unit_r, -1, waterline + 1);

	// Reeds nod at the shoreline near each trunk, a small foreground detail that
	// grounds the trees on the bank and sways with the same breeze.
	QPainter reeds(&image);
	reeds.setRenderHint(QPainter::Antialiasing, false);
	const int sway = triangleWave(tick / 6, 2);
	for (const int shore_x : { (margin_l * 3) / 5, width - (margin_r * 3) / 5 }) {
		const std::uint32_t hash = pixelHash(static_cast<std::uint32_t>(shore_x) * 29u + 11u);
		const int blades = 3 + static_cast<int>(hash % 2u);
		for (int b = 0; b < blades; ++b) {
			const int bx = shore_x + (b - blades / 2) * 2;
			const int bh = std::max(3, (height * 4) / 100) + static_cast<int>((hash >> b) % 3u);
			reeds.setPen(QPen((b & 1) ? palette.accent : palette.deep, 1));
			reeds.drawLine(bx, waterline + 1, bx + sway + (b - blades / 2), waterline + 1 - bh);
		}
	}
	reeds.end();

	// Fallen leaves rest on the lake and wander in slow, looping currents; each
	// traces a lazy Lissajous drift that is a pure function of its seed and the
	// tick, with a faint sky-bright wake trailing behind.
	const int leaf_count = std::max(6, width / 42);
	for (int i = 0; i < leaf_count; ++i) {
		const std::uint32_t hash = pixelHash(static_cast<std::uint32_t>(i) * 2246822519u + 17u);
		const double phase = (hash & 4095u) / 4095.0 * 6.2831853;
		const int base_x = static_cast<int>(hash % std::max(1, width));
		const int base_y = waterline + 2 + static_cast<int>((hash >> 12) % static_cast<std::uint32_t>(std::max(1, lake_h - 4)));
		const double t = tick * 0.008;
		const int lx = std::clamp(base_x + static_cast<int>(std::lround(std::sin(t + phase) * (width * 0.02))), 1, width - 3);
		const int ly = std::clamp(base_y + static_cast<int>(std::lround(std::cos(t * 0.7 + phase) * (lake_h * 0.06))), waterline + 1, height - 2);
		const QRgb leaf = (i % 3 == 0) ? pink : ((i % 3 == 1) ? shade : warm);
		QRgb* row = reinterpret_cast<QRgb*>(image.scanLine(ly));
		QRgb* below = reinterpret_cast<QRgb*>(image.scanLine(ly + 1));
		row[lx] = leaf;
		row[lx + 1] = leaf;
		below[lx + (i & 1)] = leaf;
		// wake: a bright ripple just behind the drifting leaf
		if (lx - 1 >= 0 && (row[lx - 1] == field)) {
			row[lx - 1] = sky;
		}
	}

	// Detached petals drift down on the breeze before settling toward the water,
	// a pure function of seed and tick. A few catch the sun and flash warm.
	const int petal_count = std::max(16, width / 9);
	for (int i = 0; i < petal_count; ++i) {
		const std::uint32_t hash = pixelHash(static_cast<std::uint32_t>(i) + 0xc4e1u);
		const int seed_x = static_cast<int>(hash % std::max(1, width));
		const int seed_y = static_cast<int>(((hash >> 10) + seed_x / 3) % std::max(1, waterline));
		const int rate = 1 + static_cast<int>((hash >> 20) & 1u);
		const int drift = (tick * rate) / 2;
		const int fall = (tick * (1 + static_cast<int>((hash >> 22) & 1u))) / 3;
		const int flutter = triangleWave(tick / 3 + static_cast<int>(hash & 7u), 2) - 1;
		const int x = ((seed_x - drift + flutter) % width + width) % width;
		const int y = (seed_y + fall) % std::max(1, waterline);
		const QRgb tone = (i % 6 == 0) ? warm : pink;
		if (x + 1 < width && y + 1 < height) {
			QRgb* row = reinterpret_cast<QRgb*>(image.scanLine(y));
			QRgb* next = reinterpret_cast<QRgb*>(image.scanLine(y + 1));
			row[x] = tone;
			row[x + 1] = tone;
			next[x + (i & 1)] = tone;
		}
	}

	// Godrays fan outward from the sun above the page, swaying gently. Each role
	// swaps for its lighter partner under the Bayer mask: sky gains pink shafts,
	// dark grass lifts to lit green, and blossoms caught in a ray flash warm.
	const GodrayBeam beams[] = {
		{ width / 9, -1, 3, 3 },
		{ width / 4, -1, 5, 2 },
		{ -width / 9, 1, 3, 3 },
		{ -width / 4, 1, 5, 2 }
	};
	const QRgb from_roles[] = { sky, shade, pink };
	const QRgb to_roles[] = { pink, field, warm };
	applyGodrays(image, sun_x, triangleWave(tick / 8, 8), beams, 4, from_roles, to_roles, 3);
}

void compositeCover(QImage& target, const QImage& source, const QPointF& focal, Qt::TransformationMode transform)
{
	const QRect crop = coverCrop(source.size(), target.size(), focal);
	if (crop.isEmpty()) {
		return;
	}
	const QImage scaled = source.copy(crop).scaled(target.size(), Qt::IgnoreAspectRatio, transform);
	QPainter painter(&target);
	painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
	painter.drawImage(0, 0, scaled);
}

// Nearest-neighbor integer upscale of a virtual-resolution frame to the
// physical window, shared by the static and animated paths so the Retina
// pitch math can never diverge between them.
QImage upscaleVirtual(const QImage& virtual_wallpaper, const QSize& logical_size, int pitch, qreal device_pixel_ratio)
{
	const QSize physical_size(
		qRound(logical_size.width() * device_pixel_ratio),
		qRound(logical_size.height() * device_pixel_ratio));
	const QSize integer_scaled_size(
		qRound(virtual_wallpaper.width() * pitch * device_pixel_ratio),
		qRound(virtual_wallpaper.height() * pitch * device_pixel_ratio));
	const QImage scaled_wallpaper = virtual_wallpaper.scaled(
		integer_scaled_size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
	QImage image = scaled_wallpaper.copy(QRect(QPoint(), physical_size));
	image.setDevicePixelRatio(device_pixel_ratio);
	return image;
}

void drawShell(QPainter& painter, const QRect& surface, int unit, const Theme& theme, const QColor& paper)
{
	const QColor primary = theme.uiPrimaryColor();
	const QColor secondary = theme.uiSecondaryColor();
	const QColor accent = theme.uiAccentColor();
	painter.fillRect(surface, primary);
	if (theme.shellMode() == 1) {
		// Terminal: a recessed phosphor bezel with an intentionally narrow
		// active display. Every band is an authored palette role.
		painter.fillRect(surface.adjusted(2 * unit, 2 * unit, -2 * unit, -2 * unit), accent);
		painter.fillRect(surface.adjusted(3 * unit, 3 * unit, -3 * unit, -3 * unit), secondary);
		painter.fillRect(surface.adjusted(5 * unit, 5 * unit, -5 * unit, -5 * unit), accent);
		painter.fillRect(surface.adjusted(7 * unit, 7 * unit, -7 * unit, -7 * unit), paper);
	} else {
		// Meadow: broad stacked cartridge bands.
		painter.fillRect(surface.adjusted(2 * unit, 2 * unit, -2 * unit, -2 * unit), secondary);
		painter.fillRect(surface.adjusted(4 * unit, 4 * unit, -4 * unit, -4 * unit), accent);
		painter.fillRect(surface.adjusted(6 * unit, 6 * unit, -6 * unit, -6 * unit), paper);
	}
}

}

QImage PresentationSurface::preprocessWallpaper(
	const QImage& source,
	const QSize& target,
	WallpaperMode mode,
	const QPointF& focal_point,
	SnesColor fallback,
	SnesColor black_replacement,
	const ThemePalette* palette,
	int tick,
	const QRect& clearing)
{
	if (target.isEmpty()) {
		return {};
	}

	QImage result(target, QImage::Format_ARGB32_Premultiplied);
	if (mode == SolidPattern) {
		drawGeneratedMeadow(result, fallback, tick);
	} else if (mode == CherryBlossom && palette) {
		drawCherryBlossom(result, *palette, tick, clearing);
	} else {
		result.fill(SnesStyle::toQColor(fallback));
	}
	if (!source.isNull()) {
		switch (mode) {
		case AmbientExtension: {
			QImage ambient = result;
			compositeCover(ambient, source, focal_point, Qt::SmoothTransformation);
			QPainter painter(&result);
			painter.setOpacity(0.45);
			painter.drawImage(0, 0, ambient);
			painter.setOpacity(1.0);
			QImage contained = source.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
			painter.drawImage((target.width() - contained.width()) / 2, (target.height() - contained.height()) / 2, contained);
			break;
		}
		case CleanCover:
			compositeCover(result, source, focal_point, Qt::FastTransformation);
			break;
		case Tile: {
			QImage tile = source;
			if (tile.width() > target.width() || tile.height() > target.height()) {
				tile = tile.scaled(target, Qt::KeepAspectRatio, Qt::FastTransformation);
			}
			QPainter painter(&result);
			for (int y = 0; y < target.height(); y += std::max(1, tile.height())) {
				for (int x = 0; x < target.width(); x += std::max(1, tile.width())) {
					painter.drawImage(x, y, tile);
				}
			}
			break;
		}
		case Stretch: {
			QPainter painter(&result);
			painter.drawImage(QRect(QPoint(), target), source);
			break;
		}
		case SolidPattern:
		case CherryBlossom:
		case SolidColor:
			break;
		case PixelatedCover:
		default:
			compositeCover(result, source, focal_point, Qt::SmoothTransformation);
			break;
		}
	}

	ThemeToneMapper::mapWallpaper(result, SnesStyle::toQColor(black_replacement));
	return result;
}

QImage PresentationSurface::renderWallpaper(const Theme& theme, const QSize& target, int tick,
	const QRect& clearing)
{
	QImage source;
	if (theme.wallpaperSource() == Theme::WallpaperSource::Image) {
		QImageReader reader(theme.backgroundImage());
		reader.setAutoTransform(true);
		source = reader.read();
	}
	const SnesColor replacement = SnesStyle::toSnesColor(theme.blackReplacement());
	const SnesColor fallback = SnesStyle::toSnesColor(theme.backgroundColor(), replacement);
	WallpaperMode mode = SolidColor;
	switch (theme.wallpaperSource()) {
	case Theme::WallpaperSource::GeneratedMeadow: mode = SolidPattern; break;
	case Theme::WallpaperSource::GeneratedCherryBlossom: mode = CherryBlossom; break;
	case Theme::WallpaperSource::Image: mode = static_cast<WallpaperMode>(theme.wallpaperFit()); break;
	case Theme::WallpaperSource::SolidColor: break;
	}
	const ThemePalette palette = theme.palette();
	return preprocessWallpaper(
		source,
		target,
		mode,
		theme.wallpaperFocalPoint(),
		fallback,
		replacement,
		&palette,
		tick,
		clearing);
}

QImage PresentationSurface::render(
	const Theme& theme,
	const QSize& logical_size,
	QRect& document_surface,
	qreal device_pixel_ratio,
	bool panel_visible,
	PanelSide panel_side,
	int tick)
{
	const PresentationLayout layout = PresentationGeometry::calculate(
		{ logical_size.width(), logical_size.height() }, panel_visible, panel_side);
	const int pitch = std::max(1, layout.virtual_pixel_pitch);
	const QSize virtual_size(
		(logical_size.width() + pitch - 1) / pitch,
		(logical_size.height() + pitch - 1) / pitch);
	// The document rectangle in virtual (source-grid) pixels. The generated
	// scenes frame their trees around this clearing so the blossoms land in the
	// visible margins rather than behind the writing surface.
	const QRect clearing(
		layout.document.x / pitch,
		layout.document.y / pitch,
		layout.document.width / pitch,
		layout.document.height / pitch);
	const QImage virtual_wallpaper = renderWallpaper(theme, virtual_size, tick, clearing);
	QImage image = upscaleVirtual(virtual_wallpaper, logical_size, pitch, device_pixel_ratio);

	document_surface = QRect(
		layout.document.x,
		layout.document.y,
		layout.document.width,
		layout.document.height);

	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, false);
	const int unit = std::max(1, pitch);
	QColor paper = theme.foregroundColor();
	paper.setAlpha(qRound(theme.foregroundOpacity() * 2.55));
	drawShell(painter, document_surface, unit, theme, paper);
	if (panel_visible) {
		const QRect panel_surface(layout.panel.x, layout.panel.y, layout.panel.width, layout.panel.height);
		drawShell(painter, panel_surface, unit, theme, paper);
	}
	painter.end();
	// Legacy themes intentionally use translucent paper. Requantize the
	// composite so the restored opacity never creates off-palette pixels.
	ThemeToneMapper::mapWallpaper(image, theme.blackReplacement());

	const int inset = theme.shellMode() == 1 ? 7 * unit : 6 * unit;
	document_surface.adjust(inset, inset, -inset, -inset);
	return image;
}

QImage PresentationSurface::renderMotionFrame(
	const Theme& theme,
	const QSize& logical_size,
	qreal device_pixel_ratio,
	int tick,
	bool panel_visible,
	PanelSide panel_side)
{
	QRect document_surface;
	return render(theme, logical_size, document_surface, device_pixel_ratio, panel_visible, panel_side, tick);
}

bool PresentationSurface::wallpaperAnimates(const Theme& theme)
{
	return theme.wallpaperSource() == Theme::WallpaperSource::GeneratedCherryBlossom
		|| theme.wallpaperSource() == Theme::WallpaperSource::GeneratedMeadow;
}

bool PresentationSurface::hasPureBlack(const QImage& source)
{
	const QImage image = source.convertToFormat(QImage::Format_RGB32);
	for (int y = 0; y < image.height(); ++y) {
		const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (int x = 0; x < image.width(); ++x) {
			if ((line[x] & 0x00ffffffu) == 0u) {
				return true;
			}
		}
	}
	return false;
}
