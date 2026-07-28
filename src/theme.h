/*
	SPDX-FileCopyrightText: 2009 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FOCUSWRITER_THEME_H
#define FOCUSWRITER_THEME_H

#include "ranged_int.h"
#include "presentation_geometry.h"
#include "settings_file.h"
#include "theme_palette.h"

#include <QColor>
#include <QCoreApplication>
#include <QExplicitlySharedDataPointer>
#include <QFont>
#include <QFuture>
#include <QPointF>
#include <QSharedData>
class QImage;
class QSize;

class Theme : public SettingsFile
{
	Q_DECLARE_TR_FUNCTIONS(Theme)

	class ThemeData : public QSharedData
	{
	public:
		ThemeData(const QString& id, bool is_default, bool create);

		QString id;
		QString name;
		bool is_default;

		QColor load_color;

		RangedInt background_type;
		QColor background_color;
		QString background_path;
		QString background_image;
		RangedInt wallpaper_mode;
		RangedInt wallpaper_source;
		RangedInt wallpaper_fit;
		RangedInt wallpaper_focal_x;
		RangedInt wallpaper_focal_y;
		QColor black_replacement;
		QColor ui_primary_color;
		QColor ui_secondary_color;
		QColor ui_accent_color;
		QColor ui_text_color;
		QString ui_font_family;
		RangedInt ui_font_size;
		RangedInt shell_mode;

		QColor foreground_color;
		RangedInt foreground_opacity;
		RangedInt foreground_margin;
		RangedInt foreground_padding;

		// --- Legacy import compatibility only (round-tripped, never
		// rendered by the FairyWriter presentation; see plan.md) ---
		RangedInt foreground_width;
		RangedInt foreground_position;
		bool round_corners_enabled;
		RangedInt corner_radius;
		bool blur_enabled;
		RangedInt blur_radius;
		bool shadow_enabled;
		RangedInt shadow_offset;
		RangedInt shadow_radius;
		QColor shadow_color;

		QColor text_color;
		QFont text_font;
		RangedInt document_font_pixels;
		QColor misspelled_color;

		bool indent_first_line;
		RangedInt line_spacing;
		RangedInt paragraph_spacing_above;
		RangedInt paragraph_spacing_below;
		RangedInt tab_width;
	};
	QSharedDataPointer<ThemeData> d;

public:
	enum class WallpaperSource
	{
		SolidColor = 0,
		GeneratedMeadow,
		GeneratedCherryBlossom,
		Image
	};

	enum class WallpaperFit
	{
		PixelCover = 0,
		AmbientExtension,
		CleanCover,
		Tile,
		Stretch
	};

	enum class ShellStyle
	{
		LayeredFrame = 0,
		TerminalBezel
	};

	explicit Theme();
	Theme(const Theme& theme);
	Theme(const QString& id, bool is_default);
	~Theme();
	Theme& operator=(const Theme& theme);

	static QString clone(const QString& id, bool is_default, const QString& name);
	static Theme neutralDraft();
	static void copyBackgrounds();
	static QString createId();
	static QString defaultId() { return "pastelmeadow"; }
	static bool exists(const QString& name);
	static QString filePath(const QString& id, bool is_default = false);
	static QString iconPath(const QString& id, bool is_default, qreal pixelratio);
	static QString path() { return m_path; }
	static void removeIcon(const QString& id, bool is_default);
	static void setDefaultPath(const QString& path);
	static void setPath(const QString& path);

	QImage render(const QSize& background, QRect& foreground, const int margin, const qreal pixelratio,
		bool panel_visible = false, PanelSide panel_side = PanelSide::Right) const;
	void renderText(QImage background, const QRect& foreground, const qreal pixelratio, QImage* preview, QImage* icon) const;

	// Name settings
	bool isDefault() const { return d->is_default; }
	QString id() const { return d->id; }
	QString name() const { return d->name; }
	void setName(const QString& name) { setValue(d->name, name); }

	QFuture<QColor> calculateLoadColor() const;
	QColor loadColor() const { return d->load_color; }
	void setLoadColor(const QColor& color);

	// Background settings
	int backgroundType() const { return d->background_type; }
	QColor backgroundColor() const { return d->background_color; }
	QString backgroundImage() const;
	QString backgroundPath() const { return d->background_path; }

	void setBackgroundType(int type) { setValue(d->background_type, type); }
	void setBackgroundColor(const QColor& color);
	void setBackgroundImage(const QString& path);

	// SNES presentation settings
	int wallpaperMode() const { return d->wallpaper_mode; }
	WallpaperSource wallpaperSource() const { return static_cast<WallpaperSource>(d->wallpaper_source.value()); }
	WallpaperFit wallpaperFit() const { return static_cast<WallpaperFit>(d->wallpaper_fit.value()); }
	QPointF wallpaperFocalPoint() const { return QPointF(d->wallpaper_focal_x.value() / 1000.0, d->wallpaper_focal_y.value() / 1000.0); }
	QColor blackReplacement() const { return d->black_replacement; }
	QColor uiPrimaryColor() const { return d->ui_primary_color; }
	QColor uiSecondaryColor() const { return d->ui_secondary_color; }
	QColor uiAccentColor() const { return d->ui_accent_color; }
	QColor uiTextColor() const { return d->ui_text_color; }
	QString uiFontFamily() const { return d->ui_font_family; }
	int uiFontSize() const { return d->ui_font_size; }
	int shellMode() const { return d->shell_mode; }
	ShellStyle shellStyle() const { return static_cast<ShellStyle>(d->shell_mode.value()); }
	ThemePalette palette() const;

	void setWallpaperMode(int mode) { setValue(d->wallpaper_mode, mode); }
	void setWallpaperSource(WallpaperSource source);
	void setWallpaperFit(WallpaperFit fit);
	void setWallpaperFocalPoint(const QPointF& point);
	void setBlackReplacement(const QColor& color);
	void setUiPrimaryColor(const QColor& color);
	void setUiSecondaryColor(const QColor& color);
	void setUiAccentColor(const QColor& color);
	void setUiTextColor(const QColor& color);
	void setUiFontFamily(const QString& family) { setValue(d->ui_font_family, family); }
	void setUiFontSize(int size) { setValue(d->ui_font_size, size); }
	void setShellMode(int mode) { setValue(d->shell_mode, mode); }
	void setShellStyle(ShellStyle style) { setShellMode(static_cast<int>(style)); }
	void setPalette(const ThemePalette& palette);
	void toneMap();
	void applyDesign(const Theme& source);

	// Foreground settings
	QColor foregroundColor() const { return d->foreground_color; }
	RangedInt foregroundOpacity() const { return d->foreground_opacity; }
	RangedInt foregroundMargin() const { return d->foreground_margin; }
	RangedInt foregroundPadding() const { return d->foreground_padding; }
	QRect foregroundRect(const QSize& size, int margin, const qreal pixelratio,
		bool panel_visible = false, PanelSide panel_side = PanelSide::Right) const;

	void setForegroundColor(const QColor& color);
	void setForegroundOpacity(int opacity) { setValue(d->foreground_opacity, opacity); }
	void setForegroundMargin(int margin) { setValue(d->foreground_margin, margin); }
	void setForegroundPadding(int padding) { setValue(d->foreground_padding, padding); }

	// Legacy import compatibility only: these settings round-trip through
	// .theme/.fwtz files and the session importer, but the FairyWriter
	// presentation never renders them and the editor never exposes them.
	RangedInt foregroundWidth() const { return d->foreground_width; }
	RangedInt foregroundPosition() const { return d->foreground_position; }
	void setForegroundWidth(int width) { setValue(d->foreground_width, width); }
	void setForegroundPosition(int position) { setValue(d->foreground_position, position); }

	bool roundCornersEnabled() const { return d->round_corners_enabled; }
	RangedInt cornerRadius() const { return d->corner_radius; }
	void setRoundCornersEnabled(bool enabled) { setValue(d->round_corners_enabled, enabled); }
	void setCornerRadius(int radius) { setValue(d->corner_radius, radius); }

	bool blurEnabled() const { return d->blur_enabled; }
	RangedInt blurRadius() const { return d->blur_radius; }
	void setBlurEnabled(bool enabled) { setValue(d->blur_enabled, enabled); }
	void setBlurRadius(int radius) { setValue(d->blur_radius, radius); }

	bool shadowEnabled() const { return d->shadow_enabled; }
	QColor shadowColor() const { return d->shadow_color; }
	RangedInt shadowRadius() const { return d->shadow_radius; }
	RangedInt shadowOffset() const { return d->shadow_offset; }
	void setShadowEnabled(bool enabled) { setValue(d->shadow_enabled, enabled); }
	void setShadowColor(const QColor& color);
	void setShadowRadius(int radius) { setValue(d->shadow_radius, radius); }
	void setShadowOffset(int offset) { setValue(d->shadow_offset, offset); }

	// Text settings
	QColor textColor() const { return d->text_color; }
	QFont textFont() const { return d->text_font; }
	RangedInt documentFontPixels() const { return d->document_font_pixels; }
	QColor misspelledColor() const { return d->misspelled_color; }

	void setTextColor(const QColor& color);
	void setTextFont(const QFont& font) { setValue(d->text_font, font); }
	void setDocumentFontPixels(int pixels) { setValue(d->document_font_pixels, pixels); }
	void setMisspelledColor(const QColor& color);

	// Spacing settings
	bool indentFirstLine() const { return d->indent_first_line; }
	RangedInt lineSpacing() const { return d->line_spacing; }
	RangedInt spacingAboveParagraph() const { return d->paragraph_spacing_above; }
	RangedInt spacingBelowParagraph() const { return d->paragraph_spacing_below; }
	RangedInt tabWidth() const { return d->tab_width; }

	void setIndentFirstLine(bool indent) { setValue(d->indent_first_line, indent); }
	void setLineSpacing(int spacing) { setValue(d->line_spacing, spacing); }
	void setSpacingAboveParagraph(int spacing) { setValue(d->paragraph_spacing_above, spacing); }
	void setSpacingBelowParagraph(int spacing) { setValue(d->paragraph_spacing_below, spacing); }
	void setTabWidth(int width) { setValue(d->tab_width, width); }

	bool operator==(const Theme& theme) const;

private:
	void reload() override;
	void write() override;

private:
	static QString m_path_default;
	static QString m_path;
};

Q_DECLARE_METATYPE(Theme)

#endif // FOCUSWRITER_THEME_H
