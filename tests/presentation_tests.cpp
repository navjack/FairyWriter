/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "presentation_geometry.h"
#include "presentation_surface.h"
#include "snes_style.h"
#include "snes_side_panel.h"
#include "theme.h"
#include "theme_tone_mapper.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPalette>
#include <QSettings>
#include <QTemporaryDir>
#include <QScrollArea>
#include <QSet>

#include <iostream>

namespace
{

int failures = 0;

void expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

bool isQuantized(const QImage& source, SnesColor replacement)
{
	const QImage image = source.convertToFormat(QImage::Format_RGB32);
	for (int y = 0; y < image.height(); ++y) {
		const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (int x = 0; x < image.width(); ++x) {
			const QColor actual(line[x]);
			if (SnesStyle::toQColor(SnesStyle::toSnesColor(actual, replacement)) != actual) {
				return false;
			}
		}
	}
	return true;
}

void testWallpaperModes()
{
	QImage transparent(37, 19, QImage::Format_ARGB32_Premultiplied);
	transparent.fill(Qt::transparent);
	transparent.setPixelColor(4, 4, QColor(0, 0, 0, 255));
	transparent.setPixelColor(5, 4, QColor(255, 0, 255, 255));
	const SnesColor fallback = SnesColor::fromComponents(23, 28, 29);
	const SnesColor replacement = SnesColor::fromComponents(3, 5, 7);

	const ThemePalette palette = Theme::neutralDraft().palette();
	for (int mode = PresentationSurface::PixelatedCover; mode <= PresentationSurface::SolidColor; ++mode) {
		const QImage result = PresentationSurface::preprocessWallpaper(
			transparent,
			QSize(320, 180),
			static_cast<PresentationSurface::WallpaperMode>(mode),
			QPointF(0.25, 0.75),
			fallback,
			replacement,
			&palette);
		expect(result.size() == QSize(320, 180), "wallpaper mode paints the exact target size");
		expect(!PresentationSurface::hasPureBlack(result), "wallpaper mode rejects imported pure black");
		expect(isQuantized(result, replacement), "wallpaper mode emits only BGR555 colors");
	}
}

void testToneMapperExhaustive()
{
	const QColor deep(QStringLiteral("#39214a"));
	const QColor paper(QStringLiteral("#493158"));
	const auto lut = ThemeToneMapper::buildWallpaperLut(deep);
	for (int value = 0; value < 32768; ++value) {
		const QColor mapped(lut[value]);
		expect(mapped.rgb() != QColor(Qt::black).rgb(), "tone mapper never emits pure black");
		expect(SnesStyle::toQColor(SnesStyle::toSnesColor(mapped)) == mapped,
			"tone mapper emits exact BGR555 colors");
		const QColor source = SnesStyle::toQColor(SnesColor::fromBgr555(value));
		const QColor text = ThemeToneMapper::readableText(source, paper, deep);
		expect(SnesStyle::contrastRatio(text, ThemeToneMapper::mapColor(paper, deep)) >= 4.5,
			"tone mapper automatically resolves readable document text");
		ThemePalette palette;
		palette.background = QColor(QStringLiteral("#b5efce"));
		palette.paper = paper;
		palette.deep = deep;
		palette.primary = source;
		palette.secondary = source;
		palette.accent = source;
		palette.ui_text = source;
		palette.document_text = source;
		palette.misspelled = source;
		const ThemePalette mapped_palette = ThemeToneMapper::mapPalette(palette);
		expect(SnesStyle::contrastRatio(mapped_palette.ui_text, mapped_palette.background) >= 4.5
				&& SnesStyle::contrastRatio(mapped_palette.document_text, mapped_palette.paper) >= 4.5,
			"tone mapper automatically resolves every stored text role against its surface");
	}
}

void testCherryBlossomGenerator()
{
	const Theme theme = Theme::neutralDraft();
	expect(theme.wallpaperSource() == Theme::WallpaperSource::GeneratedCherryBlossom,
		"new themes start with the generated cherry-blossom scene");
	expect(theme.shellStyle() == Theme::ShellStyle::LayeredFrame,
		"new themes start with the layered FairyWriter shell");
	expect(theme.uiFontFamily() == QStringLiteral("Blasphemous") && theme.documentFontPixels() == 8,
		"new themes use redistributable bundled typography and the eight-pixel prose grid");
	expect(theme.textColor() == ThemeToneMapper::readableText(theme.textColor(), theme.foregroundColor(), theme.blackReplacement()),
		"new theme yellow prose is already tone-mapped against its plum paper");
	const ThemePalette palette = theme.palette();
	const QImage virtual_scene = PresentationSurface::preprocessWallpaper(
		QImage(), QSize(256, 224), PresentationSurface::CherryBlossom,
		QPointF(0.5, 0.5), SnesStyle::toSnesColor(palette.background),
		SnesStyle::toSnesColor(palette.deep), &palette);
	expect(virtual_scene.size() == QSize(256, 224),
		"cherry-blossom scene is authored on the exact 256x224 source grid");
	const QSet<QRgb> allowed{
		palette.background.rgb(), palette.primary.rgb(), palette.accent.rgb(),
		palette.deep.rgb(), palette.secondary.rgb(), palette.ui_text.rgb()
	};
	bool palette_only = true;
	int scene_pink = 0;
	int scene_mint = 0;
	int scene_deep = 0;
	for (int y = 0; y < virtual_scene.height(); ++y) {
		for (int x = 0; x < virtual_scene.width(); ++x) {
			palette_only = palette_only && allowed.contains(virtual_scene.pixel(x, y));
			const QColor color = virtual_scene.pixelColor(x, y);
			scene_pink += color == palette.secondary;
			scene_mint += color == palette.primary;
			scene_deep += color == palette.deep;
		}
	}
	expect(palette_only, "cherry-blossom scene uses authored palette roles only");
	expect(scene_pink > 100 && scene_mint > 100 && scene_deep > 100,
		"source scene contains petal, field, grass, and branching-tree coverage");

	QRect first_paper;
	QRect second_paper;
	const QImage first = PresentationSurface::render(theme, QSize(1280, 720), first_paper, 1.0);
	const QImage second = PresentationSurface::render(theme, QSize(1280, 720), second_paper, 1.0);
	const QString audit_path = qEnvironmentVariable("FAIRYWRITER_PRESENTATION_AUDIT_DIR");
	if (!audit_path.isEmpty()) {
		QDir().mkpath(audit_path);
		virtual_scene.save(QDir(audit_path).filePath(QStringLiteral("cherry-blossom-256x224.png")));
		first.save(QDir(audit_path).filePath(QStringLiteral("cherry-blossom-1280x720.png")));
	}
	expect(first == second && first_paper == second_paper,
		"cherry-blossom wallpaper generation is byte-for-byte deterministic");
	expect(!PresentationSurface::hasPureBlack(first), "cherry-blossom scene contains no pure black");
	expect(isQuantized(first, SnesStyle::toSnesColor(theme.blackReplacement())),
		"cherry-blossom scene remains entirely BGR555 after composition");

	int pink = 0;
	int mint = 0;
	int deep_pixels = 0;
	for (int y = 0; y < first.height(); ++y) {
		for (int x = 0; x < first.width(); ++x) {
			const QColor color = first.pixelColor(x, y);
			pink += color == theme.uiSecondaryColor();
			mint += color == theme.uiPrimaryColor();
			deep_pixels += color == theme.blackReplacement();
		}
	}
	expect(pink > 100 && mint > 100 && deep_pixels > 100,
		"generated landscape contains blossom, field, and branch color regions");
}

void testWallpaperMotion()
{
	const Theme cherry = Theme::neutralDraft();
	Theme meadow = Theme::neutralDraft();
	meadow.setWallpaperSource(Theme::WallpaperSource::GeneratedMeadow);
	Theme still_theme = Theme::neutralDraft();
	still_theme.setWallpaperSource(Theme::WallpaperSource::SolidColor);
	expect(PresentationSurface::wallpaperAnimates(cherry) && PresentationSurface::wallpaperAnimates(meadow),
		"both generated scenes report animated wallpaper");
	expect(!PresentationSurface::wallpaperAnimates(still_theme),
		"solid-color wallpaper reports no animation");

	const QSize logical(1280, 720);
	const SnesColor replacement = SnesStyle::toSnesColor(cherry.blackReplacement());
	for (const Theme& theme : { cherry, meadow }) {
		// Determinism: every animated frame is a pure function of its tick.
		for (const int tick : { 0, 7, 100, 1000 }) {
			const QImage first = PresentationSurface::renderMotionFrame(theme, logical, 1.0, tick);
			const QImage second = PresentationSurface::renderMotionFrame(theme, logical, 1.0, tick);
			expect(first == second, "animated wallpaper frames are deterministic per tick");
		}
		// Invariants hold at arbitrary ticks, exactly as they do at tick zero.
		for (const int tick : { 1, 63, 509 }) {
			const QImage frame = PresentationSurface::renderMotionFrame(theme, logical, 1.0, tick);
			expect(frame.size() == QSize(1280, 720), "animated frame paints the exact logical size");
			expect(!PresentationSurface::hasPureBlack(frame), "animated frame contains no pure black");
			expect(isQuantized(frame, replacement), "animated frame emits only BGR555 colors");
		}
	}

	// Motion actually happens, and the defaulted static path is tick zero.
	QRect paper;
	const QImage still = PresentationSurface::render(cherry, logical, paper, 1.0);
	const QImage tick_zero = PresentationSurface::renderMotionFrame(cherry, logical, 1.0, 0);
	expect(still == tick_zero, "the defaulted presentation render is the tick-zero motion frame");
	const QImage moved = PresentationSurface::renderMotionFrame(cherry, logical, 1.0, 5);
	expect(still != moved, "wallpaper motion changes pixels across ticks");
	bool paper_untouched = true;
	for (int y = paper.top(); y <= paper.bottom() && paper_untouched; ++y) {
		for (int x = paper.left(); x <= paper.right(); ++x) {
			if (still.pixel(x, y) != moved.pixel(x, y)) {
				paper_untouched = false;
				break;
			}
		}
	}
	expect(paper_untouched, "wallpaper motion never repaints the document paper");

	// The virtual scene stays palette-pure while animating.
	const ThemePalette palette = cherry.palette();
	const QSet<QRgb> allowed{
		palette.background.rgb(), palette.primary.rgb(), palette.accent.rgb(),
		palette.deep.rgb(), palette.secondary.rgb(), palette.ui_text.rgb()
	};
	const QString audit_path = qEnvironmentVariable("FAIRYWRITER_PRESENTATION_AUDIT_DIR");
	for (const int tick : { 0, 8, 16, 32, 64 }) {
		const QImage scene = PresentationSurface::renderWallpaper(cherry, QSize(256, 224), tick);
		bool palette_only = true;
		for (int y = 0; y < scene.height() && palette_only; ++y) {
			for (int x = 0; x < scene.width(); ++x) {
				if (!allowed.contains(scene.pixel(x, y))) {
					palette_only = false;
					break;
				}
			}
		}
		expect(palette_only, "animated cherry scene uses authored palette roles only");
		if (!audit_path.isEmpty()) {
			QDir().mkpath(audit_path);
			scene.save(QDir(audit_path).filePath(QStringLiteral("cherry-motion-tick%1.png").arg(tick)));
		}
	}
	if (!audit_path.isEmpty()) {
		PresentationSurface::renderWallpaper(meadow, QSize(256, 224), 8)
			.save(QDir(audit_path).filePath(QStringLiteral("meadow-motion-tick8.png")));
		moved.save(QDir(audit_path).filePath(QStringLiteral("cherry-motion-1280x720-tick5.png")));
	}

	// Frame budget: a generous smoke bound so a runaway pixel pass fails CI.
	QElapsedTimer timer;
	timer.start();
	for (int tick = 1; tick <= 24; ++tick) {
		PresentationSurface::renderMotionFrame(cherry, QSize(2048, 1152), 1.0, tick);
	}
	const qint64 average_ms = timer.elapsed() / 24;
	if (average_ms >= 40) {
		std::cerr << "FAIL: animated frame average " << average_ms << "ms exceeds the 40ms budget\n";
		++failures;
	}
}

void testTypedThemeMigration()
{
	QTemporaryDir temporary;
	expect(temporary.isValid(), "typed migration has an isolated theme directory");
	QFile::copy(QStringLiteral(FAIRYWRITER_TEST_THEME_PATH "/pastelmeadow.theme"), temporary.filePath(QStringLiteral("pastelmeadow.theme")));
	QFile::copy(QStringLiteral(FAIRYWRITER_TEST_THEME_PATH "/bitterskies.theme"), temporary.filePath(QStringLiteral("bitterskies.theme")));
	for (const QString& id : {QStringLiteral("pastelmeadow"), QStringLiteral("bitterskies")}) {
		QSettings settings(temporary.filePath(id + QStringLiteral(".theme")), QSettings::IniFormat);
		settings.remove(QStringLiteral("Presentation/WallpaperSource"));
		settings.remove(QStringLiteral("Presentation/WallpaperFit"));
	}
	const struct LegacyFit { int type; Theme::WallpaperFit fit; } legacy_fits[] = {
		{ 1, Theme::WallpaperFit::Tile },
		{ 2, Theme::WallpaperFit::CleanCover },
		{ 3, Theme::WallpaperFit::Stretch },
		{ 4, Theme::WallpaperFit::AmbientExtension },
		{ 5, Theme::WallpaperFit::PixelCover }
	};
	for (const LegacyFit& legacy : legacy_fits) {
		QSettings settings(temporary.filePath(QStringLiteral("legacy-%1.theme").arg(legacy.type)), QSettings::IniFormat);
		settings.setValue(QStringLiteral("Name"), QStringLiteral("Legacy %1").arg(legacy.type));
		settings.setValue(QStringLiteral("Background/Type"), legacy.type);
		settings.setValue(QStringLiteral("Background/ImageFile"), QStringLiteral("legacy.png"));
	}
	Theme::setDefaultPath(temporary.path());
	const Theme meadow(QStringLiteral("pastelmeadow"), true);
	const Theme image(QStringLiteral("bitterskies"), true);
	expect(meadow.wallpaperSource() == Theme::WallpaperSource::GeneratedMeadow,
		"legacy generated-meadow mode migrates to the typed wallpaper source");
	expect(image.wallpaperSource() == Theme::WallpaperSource::Image,
		"legacy image theme migrates to the typed wallpaper source");
	expect(image.wallpaperFit() == Theme::WallpaperFit::PixelCover,
		"legacy zoomed image migrates to pixel-cover fitting");
	for (const LegacyFit& legacy : legacy_fits) {
		const Theme migrated(QStringLiteral("legacy-%1").arg(legacy.type), true);
		expect(migrated.wallpaperSource() == Theme::WallpaperSource::Image
				&& migrated.wallpaperFit() == legacy.fit,
			"every legacy Background/Type migrates to its typed image fit");
	}
	const QString custom_path = temporary.filePath(QStringLiteral("Custom"));
	QDir().mkpath(custom_path + QStringLiteral("/Images"));
	Theme::setPath(custom_path);
	Theme round_trip = Theme::neutralDraft();
	round_trip.applyDesign(Theme(QStringLiteral("legacy-4"), true));
	round_trip.saveChanges(true);
	const QSettings preserved(Theme::filePath(round_trip.id()), QSettings::IniFormat);
	expect(preserved.value(QStringLiteral("Background/Type")).toInt() == 4
			&& preserved.value(QStringLiteral("Presentation/WallpaperMode")).toInt()
				== static_cast<int>(Theme::WallpaperFit::AmbientExtension),
		"unchanged legacy wallpaper values survive a typed import/export rewrite");
	Theme::setDefaultPath(QStringLiteral(FAIRYWRITER_TEST_THEME_PATH));
	Theme::setPath(QStringLiteral(FAIRYWRITER_TEST_THEME_PATH));
}

void testTransactionalThemePersistence()
{
	QTemporaryDir temporary;
	expect(temporary.isValid(), "theme persistence has an isolated directory");
	const QString themes = temporary.filePath(QStringLiteral("Themes"));
	QDir().mkpath(themes + QStringLiteral("/Images"));
	QDir().mkpath(themes + QStringLiteral("/Previews/Default"));
	Theme::setDefaultPath(QStringLiteral(FAIRYWRITER_TEST_THEME_PATH));
	Theme::setPath(themes);

	const QString imported_path = temporary.filePath(QStringLiteral("chosen-wallpaper.png"));
	QImage imported(19, 13, QImage::Format_RGB32);
	imported.fill(QColor(QStringLiteral("#ef94bd")));
	expect(imported.save(imported_path), "transactional wallpaper fixture is writable");

	Theme draft = Theme::neutralDraft();
	draft.setName(QStringLiteral("Transactional Theme"));
	draft.setBackgroundImage(imported_path);
	draft.setWallpaperFit(Theme::WallpaperFit::AmbientExtension);
	const QString theme_file = Theme::filePath(draft.id());
	expect(!QFile::exists(theme_file), "preview draft does not write a theme file");
	expect(QDir(themes + QStringLiteral("/Images")).entryList(QDir::Files).isEmpty(),
		"preview draft does not copy an imported wallpaper");

	draft.saveChanges(true);
	expect(QFile::exists(theme_file), "explicit theme commit writes the draft once");
	const QDir stored_images(themes + QStringLiteral("/Images"));
	const QStringList copied = stored_images.entryList(QDir::Files);
	expect(copied.size() == 1, "explicit theme commit copies exactly one imported wallpaper");
	QSettings committed(theme_file, QSettings::IniFormat);
	expect(committed.value(QStringLiteral("Presentation/WallpaperSource")).toInt()
			== static_cast<int>(Theme::WallpaperSource::Image),
		"theme commit persists the typed wallpaper source");
	expect(committed.value(QStringLiteral("Presentation/WallpaperFit")).toInt()
			== static_cast<int>(Theme::WallpaperFit::AmbientExtension),
		"theme commit persists the typed wallpaper fit");
	expect(committed.value(QStringLiteral("Presentation/WallpaperMode")).toInt()
			== static_cast<int>(Theme::WallpaperFit::AmbientExtension)
			&& committed.value(QStringLiteral("Background/Type")).toInt() == 5,
		"theme commit round-trips both legacy wallpaper keys");
	expect(QFile::exists(stored_images.filePath(committed.value(QStringLiteral("Background/ImageFile")).toString())),
		"committed wallpaper reference resolves inside theme storage");
	const QString managed_image = committed.value(QStringLiteral("Background/ImageFile")).toString();
	QFile::remove(imported_path);
	Theme missing_original(draft.id(), false);
	missing_original.setUiFontSize(20);
	missing_original.saveChanges();
	QSettings after_missing_original(theme_file, QSettings::IniFormat);
	expect(after_missing_original.value(QStringLiteral("Background/ImageFile")).toString() == managed_image
			&& QFile::exists(stored_images.filePath(managed_image)),
		"resaving keeps the managed wallpaper when its original import path disappears");

	after_missing_original.setValue(QStringLiteral("Future/UnknownRole"), QStringLiteral("preserved"));
	after_missing_original.sync();
	Theme edited(draft.id(), false);
	edited.setUiFontSize(19);
	edited.saveChanges();
	QSettings round_trip(theme_file, QSettings::IniFormat);
	expect(round_trip.value(QStringLiteral("Future/UnknownRole")).toString() == QStringLiteral("preserved"),
		"theme rewrite preserves unknown legacy and future settings");

	Theme generated = edited;
	generated.setWallpaperSource(Theme::WallpaperSource::GeneratedCherryBlossom);
	generated.saveChanges();
	QSettings generated_settings(theme_file, QSettings::IniFormat);
	expect(!generated_settings.contains(QStringLiteral("Background/Image"))
			&& !generated_settings.contains(QStringLiteral("Background/ImageFile")),
		"saving a generated scene drops inactive legacy wallpaper references");

	Theme untouched = Theme::neutralDraft();
	const QString untouched_file = Theme::filePath(untouched.id());
	untouched.saveChanges(true);
	expect(QFile::exists(untouched_file),
		"an untouched neutral value draft still persists at the explicit commit boundary");

	Theme::setPath(QStringLiteral(FAIRYWRITER_TEST_THEME_PATH));
}

void testTypedPresentationMatrix()
{
	QTemporaryDir temporary;
	const QString image_path = temporary.filePath(QStringLiteral("matrix.png"));
	QImage source(23, 17, QImage::Format_RGB32);
	for (int y = 0; y < source.height(); ++y) {
		for (int x = 0; x < source.width(); ++x) {
			source.setPixelColor(x, y, QColor((x * 37) & 0xff, (y * 53) & 0xff, ((x + y) * 29) & 0xff));
		}
	}
	expect(source.save(image_path), "typed presentation matrix fixture is writable");
	const QPointF focal_points[] = { QPointF(0.0, 0.0), QPointF(1.0, 0.0), QPointF(0.0, 1.0), QPointF(1.0, 1.0) };
	for (int source_index = 0; source_index <= static_cast<int>(Theme::WallpaperSource::Image); ++source_index) {
		for (int fit_index = 0; fit_index <= static_cast<int>(Theme::WallpaperFit::Stretch); ++fit_index) {
			for (const QPointF& focal : focal_points) {
				for (int shell = 0; shell <= static_cast<int>(Theme::ShellStyle::TerminalBezel); ++shell) {
					Theme theme = Theme::neutralDraft();
					theme.setBackgroundImage(image_path);
					theme.setWallpaperSource(static_cast<Theme::WallpaperSource>(source_index));
					theme.setWallpaperFit(static_cast<Theme::WallpaperFit>(fit_index));
					theme.setWallpaperFocalPoint(focal);
					theme.setShellStyle(static_cast<Theme::ShellStyle>(shell));
					QRect paper;
					const QImage frame = PresentationSurface::render(theme, QSize(320, 180), paper, 1.0);
					expect(frame.size() == QSize(320, 180) && paper.isValid(),
						"every typed source, fit, focal extreme, and shell renders exact geometry");
					expect(!PresentationSurface::hasPureBlack(frame),
						"typed presentation matrix never emits pure black");
				}
			}
		}
	}
	Theme focal_theme = Theme::neutralDraft();
	focal_theme.setBackgroundImage(image_path);
	focal_theme.setWallpaperFit(Theme::WallpaperFit::PixelCover);
	focal_theme.setWallpaperFocalPoint(QPointF(0.0, 0.0));
	const QImage top_left = PresentationSurface::renderWallpaper(focal_theme, QSize(256, 224));
	focal_theme.setWallpaperFocalPoint(QPointF(1.0, 1.0));
	const QImage bottom_right = PresentationSurface::renderWallpaper(focal_theme, QSize(256, 224));
	expect(top_left != bottom_right,
		"pixel-cover focal extremes select different regions of an imported wallpaper");
}

void testIntegerWallpaperScaling()
{
	QTemporaryDir temporary;
	const QString image_path = temporary.filePath(QStringLiteral("pixel-grid.png"));
	QImage source(7, 5, QImage::Format_RGB32);
	for (int y = 0; y < source.height(); ++y) {
		for (int x = 0; x < source.width(); ++x) {
			source.setPixelColor(x, y, QColor((x * 41) & 0xff, (y * 67) & 0xff, ((x * 3 + y) * 31) & 0xff));
		}
	}
	expect(source.save(image_path), "integer-scaling fixture is writable");
	Theme theme = Theme::neutralDraft();
	theme.setBackgroundImage(image_path);
	theme.setWallpaperFit(Theme::WallpaperFit::Tile);
	QRect paper;
	const QImage frame = PresentationSurface::render(theme, QSize(1280, 720), paper, 1.0);
	const int pitch = PresentationGeometry::calculate({1280, 720}, false).scale;
	bool integer_blocks = true;
	for (int y = 0; y + pitch <= paper.top(); y += pitch) {
		for (int x = 0; x + pitch <= paper.left(); x += pitch) {
			const QColor expected = frame.pixelColor(x, y);
			for (int dy = 0; dy < pitch; ++dy) {
				for (int dx = 0; dx < pitch; ++dx) {
					integer_blocks = integer_blocks && frame.pixelColor(x + dx, y + dy) == expected;
				}
			}
		}
	}
	expect(integer_blocks, "wallpaper pixels scale only by the selected integer presentation pitch");
}

void testFullPresentation()
{
	Theme::setDefaultPath(FAIRYWRITER_TEST_THEME_PATH);
	Theme::setPath(FAIRYWRITER_TEST_THEME_PATH);
	const Theme theme("pastelmeadow", true);
	expect(theme.foregroundPadding() == 24,
		"Pastel Meadow reserves a twenty-four-source-pixel writing inset");
	expect(theme.documentFontPixels() == 8,
		"Pastel Meadow uses an eight-source-pixel document font grid");
	QRect document;
	const QImage image = PresentationSurface::render(theme, QSize(1280, 720), document, 2.0);
	const PresentationLayout layout = PresentationGeometry::calculate({ 1280, 720 }, false);
	const int frame = 6 * layout.scale;

	expect(image.size() == QSize(2560, 1440), "Retina frame uses physical dimensions");
	expect(qFuzzyCompare(image.devicePixelRatio(), 2.0), "Retina frame retains its device pixel ratio");
	expect(document == QRect(layout.document.x, layout.document.y, layout.document.width, layout.document.height)
		.adjusted(frame, frame, -frame, -frame), "document surface uses corrected 4:3 geometry and pixel frame");
	expect(!PresentationSurface::hasPureBlack(image), "full presentation has no pure-black pixel");
	expect(isQuantized(image, SnesStyle::toSnesColor(theme.blackReplacement())),
		"full presentation contains only BGR555 colors");
	for (const PanelSide side : {PanelSide::Left, PanelSide::Right}) {
		QRect panel_document;
		const QImage panel_image = PresentationSurface::render(theme, QSize(1920, 1080), panel_document, 1.0, true, side);
		const PresentationLayout panel = PresentationGeometry::calculate({1920, 1080}, true, side);
		expect(panel_document == QRect(panel.document.x, panel.document.y, panel.document.width, panel.document.height)
			.adjusted(6 * panel.scale, 6 * panel.scale, -6 * panel.scale, -6 * panel.scale),
			"side-panel render keeps the corrected document surface in the combined layout");
		expect(!PresentationSurface::hasPureBlack(panel_image) && isQuantized(panel_image, SnesStyle::toSnesColor(theme.blackReplacement())),
			"side-panel render paints a complete quantized nonblack frame");
	}
}

void testStylePitch()
{
	SnesStyle style;
	SnesStyle::setUiMagnification(1);
	SnesStyle::setVirtualPixelPitch(5);
	expect(style.pixelMetric(QStyle::PM_ToolBarIconSize) == 40,
		"toolbar icons use eight virtual pixels at the presentation pitch");
	expect(style.pixelMetric(QStyle::PM_DefaultFrameWidth) == 5,
		"control frames use the same virtual-pixel pitch as the document");
	SnesStyle::setUiMagnification(2);
	expect(style.pixelMetric(QStyle::PM_ToolBarIconSize) == 80,
		"two-times interface magnification doubles pitch-derived controls");
	SnesStyle::applyTheme(Theme("pastelmeadow", true));
	expect(qAbs(QApplication::font().pointSizeF() - 18.0) < 0.01,
		"two-times controls retain a compact eighteen-point panel font");
	QFont document_font(QStringLiteral("Blasphemous"), 8);
	expect(SnesStyle::scaledDocumentFont(document_font, 8, 5).pixelSize() == 40,
		"eight-source-pixel document glyphs scale to forty pixels at pitch five");
}

void testThemeReadability()
{
	const QDir themes(FAIRYWRITER_TEST_THEME_PATH, QStringLiteral("*.theme"), QDir::Name, QDir::Files);
	for (const QString& filename : themes.entryList()) {
		const QString id = filename.chopped(6);
		const QSettings settings(themes.filePath(filename), QSettings::IniFormat);
		expect(settings.contains("Presentation/WallpaperMode"), "built-in theme explicitly chooses a wallpaper mode");
		expect(settings.contains("Presentation/FocalX") && settings.contains("Presentation/FocalY"),
			"built-in theme explicitly chooses wallpaper focal coordinates");
		expect(settings.contains("Presentation/ShellMode"), "built-in theme explicitly chooses a shell mode");
		expect(settings.contains("Presentation/UiPrimary") && settings.contains("Presentation/UiSecondary")
			&& settings.contains("Presentation/UiAccent") && settings.contains("Presentation/UiText"),
			"built-in theme explicitly owns every interface palette role");
		const Theme theme(id, true);
		const QPalette palette = SnesStyle::themePalette(theme);
		auto readable = [&](QPalette::ColorRole foreground, QPalette::ColorRole background, const char* role) {
			if (SnesStyle::contrastRatio(palette.color(foreground), palette.color(background)) < 4.5) {
				std::cerr << "FAIL: " << id.toStdString() << " has unreadable " << role << '\n';
				++failures;
			}
		};
		readable(QPalette::WindowText, QPalette::Window, "window text");
		readable(QPalette::Text, QPalette::Base, "field text");
		readable(QPalette::ButtonText, QPalette::Button, "button text");
		readable(QPalette::HighlightedText, QPalette::Highlight, "highlight text");
		readable(QPalette::ToolTipText, QPalette::ToolTipBase, "tooltip text");
		expect(SnesStyle::toSnesColor(theme.uiTextColor(), SnesStyle::toSnesColor(theme.blackReplacement())).value() != 0,
			"theme UI text never stores pure black");
	}
}

QString bundledPaletteLine(const QString& id, const ThemePalette& palette)
{
	const QColor roles[] = {
		palette.background, palette.paper, palette.deep,
		palette.primary, palette.secondary, palette.accent,
		palette.ui_text, palette.document_text, palette.misspelled
	};
	QString line = id;
	for (const QColor& role : roles) {
		line += QLatin1Char(' ') + role.name();
	}
	return line;
}

void testBundledPaletteStability()
{
	// Golden mapped palettes for every bundled theme. Loader or tone-mapper
	// changes that shift any published role must be deliberate and re-recorded.
	const QStringList golden{
		QStringLiteral("bitterskies #211921 #e6e6e6 #211921 #4a3a63 #bd94bd #735a84 #f7d6e6 #636363 #ef1010"),
		QStringLiteral("enchantment #636363 #dee6ef #312942 #735a9c #d6b5e6 #4a3a6b #ffdeff #4a2929 #ef1010"),
		QStringLiteral("gentleblues #a5bdce #a5bdce #294252 #42637b #bdd6e6 #52737b #211931 #294252 #ef1010"),
		QStringLiteral("hunterterminal #19293a #193142 #19293a #21526b #42c5c5 #194252 #8ce6de #8ce6de #ef8494"),
		QStringLiteral("oldschool #083119 #083119 #082910 #085a29 #29a55a #083a21 #a5f7bd #10e652 #105221"),
		QStringLiteral("pastelmeadow #bde6ef #fff7ce #293142 #5a9c6b #efce6b #316352 #4a315a #4a315a #d6636b"),
		QStringLiteral("spacedreams #212131 #bdad9c #212131 #63527b #c5b5d6 #3a315a #e6ceff #212131 #ef1010"),
		QStringLiteral("spygames #94adbd #dee6ef #213142 #3a6b7b #9cc5c5 #294a63 #211931 #213142 #ef1010"),
		QStringLiteral("tranquility #d6dede #e6e6e6 #314a52 #6b94a5 #d6e6e6 #52737b #19313a #314a52 #ef1010"),
		QStringLiteral("writingdesk #945a21 #ffffce #4a2919 #9c5a29 #deb57b #6b3a21 #ffe6ce #4a2919 #ef1010")
	};
	const QDir themes(FAIRYWRITER_TEST_THEME_PATH, QStringLiteral("*.theme"), QDir::Name, QDir::Files);
	QStringList actual;
	for (const QString& filename : themes.entryList()) {
		const QString id = filename.chopped(6);
		actual += bundledPaletteLine(id, Theme(id, true).palette());
	}
	expect(actual.size() == golden.size(), "every bundled theme contributes one golden palette line");
	for (const QString& line : actual) {
		if (!golden.contains(line)) {
			std::cerr << "FAIL: bundled palette drifted: " << line.toStdString() << '\n';
			++failures;
		}
	}
}

void testShellModes()
{
	const Theme meadow(QStringLiteral("pastelmeadow"), true);
	const Theme terminal(QStringLiteral("hunterterminal"), true);
	QRect meadow_paper;
	QRect terminal_paper;
	const QImage meadow_image = PresentationSurface::render(meadow, QSize(1280, 720), meadow_paper, 1.0);
	const QImage terminal_image = PresentationSurface::render(terminal, QSize(1280, 720), terminal_paper, 1.0);
	expect(terminal.shellMode() == 1 && meadow.shellMode() == 0,
		"the two flagship themes select distinct presentation shells");
	expect(terminal_paper.width() < meadow_paper.width() && terminal_paper.height() < meadow_paper.height(),
		"terminal shell reserves its own deeper bezel geometry");
	expect(meadow_image.pixelColor(meadow_paper.topLeft() - QPoint(2, 2)) != terminal_image.pixelColor(terminal_paper.topLeft() - QPoint(2, 2)),
		"shell choice changes the rendered bezel, not only the application palette");
}

void testLegacyPaperOpacity()
{
	const Theme theme(QStringLiteral("bitterskies"), true);
	QRect paper;
	const QImage image = PresentationSurface::render(theme, QSize(1280, 720), paper, 1.0);
	const QColor center = image.pixelColor(paper.center());
	expect(theme.foregroundOpacity() == 0, "Bitter Skies retains its transparent legacy paper setting");
	expect(center != theme.foregroundColor(),
		"transparent legacy paper exposes its wallpaper instead of hiding white text on white");
	expect(!PresentationSurface::hasPureBlack(image),
		"legacy opacity compositing still rejects pure black");
	expect(isQuantized(image, SnesStyle::toSnesColor(theme.blackReplacement())),
		"legacy opacity compositing is requantized to BGR555");
}

void testPanelOwnedPagination()
{
	SnesSidePanel panel;
	panel.resize(576, 672);
	QWidget* preferences = new QWidget;
	preferences->setProperty("fairywriter.panelFill", true);
	panel.showTool(preferences, QStringLiteral("Preferences"));
	QScrollArea* viewport = panel.findChild<QScrollArea*>(QStringLiteral("FairyWriterToolViewport"));
	expect(viewport && viewport->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
		"side-panel tools never overflow through a whole-dialog horizontal scrollbar");
	expect(viewport && viewport->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
		"paginated tools fit without a whole-dialog vertical scrollbar");
}

}

int main(int argc, char** argv)
{
	QApplication application(argc, argv);
	testWallpaperModes();
	testToneMapperExhaustive();
	testCherryBlossomGenerator();
	testWallpaperMotion();
	testTypedThemeMigration();
	testTransactionalThemePersistence();
	testTypedPresentationMatrix();
	testIntegerWallpaperScaling();
	testFullPresentation();
	testStylePitch();
	testThemeReadability();
	testBundledPaletteStability();
	testShellModes();
	testLegacyPaperOpacity();
	testPanelOwnedPagination();
	if (failures == 0) {
		std::cout << "All FairyWriter presentation tests passed.\n";
	}
	return failures == 0 ? 0 : 1;
}
