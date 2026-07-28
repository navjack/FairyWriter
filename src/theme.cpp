/*
	SPDX-FileCopyrightText: 2009 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "theme.h"

#include "presentation_geometry.h"
#include "presentation_surface.h"
#include "snes_style.h"
#include "theme_tone_mapper.h"
#include "utils.h"

#include <QtConcurrentRun>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QPainter>
#include <QSettings>
#include <QTextEdit>
#include <QUuid>

#include <algorithm>
#include <cmath>

//-----------------------------------------------------------------------------

namespace
{

QColor averageImage(const QString& filename, const QColor& fallback)
{
	QImageReader reader(filename);
	if (!reader.canRead()) {
		return fallback;
	}

	QImage image(reader.size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(fallback.rgb());
	{
		QPainter painter(&image);
		painter.drawImage(0, 0, reader.read());
	}
	const unsigned int width = image.width();
	const unsigned int height = image.height();

	quint64 sum_r = 0;
	quint64 sum_g = 0;
	quint64 sum_b = 0;
	quint64 sum_a = 0;

	for (unsigned int y = 0; y < height; ++y) {
		const QRgb* scanline = reinterpret_cast<const QRgb*>(image.scanLine(y));
		for (unsigned int x = 0; x < width; ++x) {
			const QRgb pixel = scanline[x];
			sum_r += qRed(pixel);
			sum_g += qGreen(pixel);
			sum_b += qBlue(pixel);
			sum_a += qAlpha(pixel);
		}
	}

	const qreal divisor = 1.0 / (width * height);
	return QColor(sum_r * divisor, sum_g * divisor, sum_b * divisor, sum_a * divisor);
}

QString checksumName(const QString& image)
{
	QCryptographicHash hash(QCryptographicHash::Sha1);
	QFile file(image);
	if (file.open(QFile::ReadOnly)) {
		hash.addData(&file);
		file.close();
	}

	const QString suffix = QFileInfo(image).suffix().toLower();

	return "2-" + hash.result().toHex() + "." + suffix;
}

QString copyImage(const QString& image)
{
	const QString name = checksumName(image);
	const QString path = Theme::path() + "/Images/" + name;
	if (!QFile::exists(path)) {
		QFile::copy(image, path);
	}
	return name;
}

int legacyBackgroundType(Theme::WallpaperSource source, Theme::WallpaperFit fit)
{
	if (source != Theme::WallpaperSource::Image) {
		return 0;
	}
	switch (fit) {
	case Theme::WallpaperFit::Tile: return 1;
	case Theme::WallpaperFit::Stretch: return 3;
	case Theme::WallpaperFit::PixelCover:
	case Theme::WallpaperFit::AmbientExtension:
	case Theme::WallpaperFit::CleanCover: return 5;
	}
	return 0;
}

QDir listIcons(const QString& id, bool is_default)
{
	const QString icon = Theme::iconPath(id, is_default, 1.0);

	const int dirindex = icon.lastIndexOf('/');
	const int baseindex = icon.lastIndexOf('.');
	const QString basename = icon.mid(dirindex + 1, baseindex - dirindex - 1);

	return QDir(icon.left(dirindex), basename + "*");
}

}

//-----------------------------------------------------------------------------

QString Theme::m_path_default;
QString Theme::m_path;

//-----------------------------------------------------------------------------

Theme::ThemeData::ThemeData(const QString& theme_id, bool theme_default, bool create)
	: id(theme_id)
	, is_default(theme_default)
	, background_type(0, 5)
	, wallpaper_mode(0, 7)
	, wallpaper_source(0, 3)
	, wallpaper_fit(0, 4)
	, wallpaper_focal_x(0, 1000)
	, wallpaper_focal_y(0, 1000)
	, ui_font_size(6, 32)
	, shell_mode(0, 1)
	, foreground_opacity(0, 100)
	, foreground_margin(1, 250)
	, foreground_padding(0, 250)
	, foreground_width(500, 9999)
	, foreground_position(0, 3)
	, round_corners_enabled(false)
	, corner_radius(1, 100)
	, blur_enabled(false)
	, blur_radius(1, 128)
	, shadow_enabled(false)
	, shadow_offset(0, 128)
	, shadow_radius(1, 128)
	, document_font_pixels(4, 32)
	, line_spacing(50, 1000)
	, paragraph_spacing_above(0, 1000)
	, paragraph_spacing_below(0, 1000)
	, tab_width(1, 1000)
{
	if (id.isEmpty() && create) {
		QString untitled;
		int count = 0;
		do {
			count++;
			untitled = Theme::tr("Untitled %1").arg(count);
		} while (exists(untitled));
		name = untitled;
		id = createId();
	}
}

//-----------------------------------------------------------------------------

Theme::Theme()
{
	d = new ThemeData(QString(), false, false);
}

//-----------------------------------------------------------------------------

Theme::Theme(const Theme& theme)
	: d(theme.d)
{
}

//-----------------------------------------------------------------------------

Theme::Theme(const QString& id, bool is_default)
{
	d = new ThemeData(id, is_default, true);
	forgetChanges();
}

//-----------------------------------------------------------------------------

Theme::~Theme()
{
}

//-----------------------------------------------------------------------------

Theme& Theme::operator=(const Theme& theme)
{
	d = theme.d;
	return *this;
}

//-----------------------------------------------------------------------------

Theme Theme::neutralDraft()
{
	Theme theme(QString(), false);
	theme.setWallpaperSource(WallpaperSource::GeneratedCherryBlossom);
	theme.setWallpaperFit(WallpaperFit::PixelCover);
	theme.setWallpaperFocalPoint(QPointF(0.5, 0.5));
	theme.setShellStyle(ShellStyle::LayeredFrame);
	theme.setUiFontFamily(QStringLiteral("Blasphemous"));
	theme.setUiFontSize(18);
	theme.setTextFont(QFont(QStringLiteral("Blasphemous")));
	theme.setDocumentFontPixels(8);
	theme.setForegroundOpacity(100);
	theme.setForegroundPadding(16);
	theme.setLineSpacing(115);
	theme.setSpacingAboveParagraph(0);
	theme.setSpacingBelowParagraph(8);
	theme.setTabWidth(32);
	theme.setIndentFirstLine(false);

	ThemePalette palette;
	palette.background = QColor(QStringLiteral("#b5efce"));
	palette.paper = QColor(QStringLiteral("#493158"));
	palette.deep = QColor(QStringLiteral("#39214a"));
	palette.primary = QColor(QStringLiteral("#63bd84"));
	palette.secondary = QColor(QStringLiteral("#ef94bd"));
	palette.accent = QColor(QStringLiteral("#296b52"));
	palette.ui_text = QColor(QStringLiteral("#f7de84"));
	palette.document_text = QColor(QStringLiteral("#f7de84"));
	palette.misspelled = QColor(QStringLiteral("#ef5263"));
	theme.setPalette(palette);
	theme.setLoadColor(theme.backgroundColor());
	return theme;
}

//-----------------------------------------------------------------------------

QString Theme::clone(const QString& id, bool is_default, const QString& name)
{
	if (id.isEmpty()) {
		return id;
	}

	// Find name for duplicate theme
	const QStringList values = splitStringAtLastNumber(name);
	int count = values.at(1).toInt();
	QString new_name;
	do {
		++count;
		new_name = values.at(0) + QString::number(count);
	} while (exists(new_name));

	// Create duplicate
	const QString new_id = createId();
	{
		Theme duplicate(id, is_default);
		duplicate.setValue(duplicate.d->name, new_name);
		duplicate.setValue(duplicate.d->id, new_id);
		duplicate.saveChanges();
	}

	// Copy icon
	const QDir dir = listIcons(id, is_default);
	const int suffix = dir.nameFilters().constFirst().length() -1;
	const QStringList files = dir.entryList();
	for (const QString& file : files) {
		QFile::copy(dir.filePath(file), dir.filePath(new_id + file.mid(suffix)));
	}

	return new_id;
}

//-----------------------------------------------------------------------------

void Theme::copyBackgrounds()
{
	QDir dir(path() + "/Images");
	QStringList images;
	QHash<QString, QString> old_images;
	static const QHash<QString, QString> source_images{
		{ "2-77534bf3da7fb42c830772be8d279be79869deb7.jpg", m_path_default + "/images/spacedreams.jpg" },
		{ "2-1ccf9867f755b306830852e8fbf36952f93ab3fe.jpg", m_path_default + "/images/writingdesk.jpg" }
	};

	// Copy images
	const QStringList themes = QDir(path(), "*.theme").entryList(QDir::Files);
	for (const QString& theme : themes) {
		QSettings settings(path() + "/" + theme, QSettings::IniFormat);
		const QString background_path = settings.value("Background/Image").toString();
		QString background_image = settings.value("Background/ImageFile").toString();
		if (background_path.isEmpty() && background_image.isEmpty()) {
			continue;
		}
		if (!background_path.isEmpty() && (background_image.isEmpty() || !dir.exists(background_image))) {
			background_image = copyImage(background_path);
			settings.setValue("Background/ImageFile", background_image);
		}

		// Set image filename to checksum of image contents
		if (!background_image.startsWith("2-")) {
			if (!old_images.contains(background_image)) {
				const QString file = checksumName(dir.filePath(background_image));
				old_images.insert(background_image, file);
				dir.rename(background_image, file);
			}
			background_image = old_images[background_image];
			settings.setValue("Background/ImageFile", background_image);
		}

		// Replace lower resolution copies of default images
		if (source_images.contains(background_image)) {
			background_image = copyImage(source_images[background_image]);
			settings.setValue("Background/ImageFile", background_image);
		}

		images.append(background_image);
	}

	// Delete unused images
	const QStringList files = dir.entryList(QDir::Files);
	for (const QString& file : files) {
		if (!images.contains(file)) {
			dir.remove(file);
		}
	}
}

//-----------------------------------------------------------------------------

QString Theme::createId()
{
	QString file;
	do
	{
		file = QUuid::createUuid().toString().mid(1, 36);
	} while (QFile::exists(filePath(file, false)));
	return file;
}

//-----------------------------------------------------------------------------

bool Theme::exists(const QString& name)
{
	const QDir dir(m_path, "*.theme");
	const QStringList themes = dir.entryList(QDir::Files);
	for (const QString& theme : themes) {
		const QSettings settings(dir.filePath(theme), QSettings::IniFormat);
		if (settings.value("Name").toString() == name) {
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------

QString Theme::filePath(const QString& id, bool is_default)
{
	return (!is_default ? m_path : m_path_default) + "/" + id + ".theme";
}

//-----------------------------------------------------------------------------

QString Theme::iconPath(const QString& id, bool is_default, qreal pixelratio)
{
	QString pixel;
	if (pixelratio > 1.0) {
		pixel = QString("@%1x").arg(pixelratio);
	}
	return m_path + (!is_default ? "/Previews/" : "/Previews/Default/") + id + pixel + ".png";
}

//-----------------------------------------------------------------------------

void Theme::removeIcon(const QString& id, bool is_default)
{
	QDir dir = listIcons(id, is_default);
	const QStringList files = dir.entryList();
	for (const QString& file : files) {
		dir.remove(file);
	}
}

//-----------------------------------------------------------------------------

void Theme::setDefaultPath(const QString& path)
{
	m_path_default = path;
}

//-----------------------------------------------------------------------------

void Theme::setPath(const QString& path)
{
	m_path = path;
	if (m_path_default.isEmpty()) {
		m_path_default = m_path;
	}
}

//-----------------------------------------------------------------------------

QImage Theme::render(const QSize& background, QRect& foreground, const int margin, const qreal pixelratio,
	bool panel_visible, PanelSide panel_side) const
{
	Q_UNUSED(margin)
	return PresentationSurface::render(*this, background, foreground, pixelratio, panel_visible, panel_side);
}

//-----------------------------------------------------------------------------

void Theme::renderText(QImage background, const QRect& foreground, const qreal pixelratio, QImage* preview, QImage* icon) const
{
	// Create preview text
	QTextEdit preview_text;
	preview_text.setAutoFillBackground(false);
	preview_text.setFrameStyle(QFrame::NoFrame);
	preview_text.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	preview_text.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	QFile file(":/lorem.txt");
	if (file.open(QFile::ReadOnly)) {
		preview_text.setPlainText(QString::fromLatin1(file.readAll()));
		file.close();
	}

	// Position preview text
	const int padding = foregroundPadding();
	const int x = foreground.x() + padding;
	const int y = foreground.y() + padding + spacingAboveParagraph();
	const int width = foreground.width() - (padding * 2);
	const int height = foreground.height() - (padding * 2) - spacingAboveParagraph();
	preview_text.setGeometry(x, y, width, height);

	// Set colors
	QColor text_color = textColor();
	text_color.setAlpha(255);

	QPalette p = preview_text.palette();
	p.setBrush(QPalette::Window, Qt::transparent);
	p.setBrush(QPalette::Base, Qt::transparent);
	p.setColor(QPalette::Text, text_color);
	p.setColor(QPalette::Highlight, text_color);
	p.setColor(QPalette::HighlightedText, (qGray(text_color.rgb()) > 127) ? Qt::black : Qt::white);
	preview_text.setPalette(p);

	// Set spacings
	const int tab_width = tabWidth();
	QTextBlockFormat block_format;
	block_format.setLineHeight(lineSpacing(), (lineSpacing() == 100) ? QTextBlockFormat::SingleHeight : QTextBlockFormat::ProportionalHeight);
	block_format.setTextIndent(tab_width * indentFirstLine());
	block_format.setTopMargin(spacingAboveParagraph());
	block_format.setBottomMargin(spacingBelowParagraph());
	preview_text.textCursor().mergeBlockFormat(block_format);
	for (int i = 0, count = preview_text.document()->allFormats().count(); i < count; ++i) {
		QTextFormat& f = preview_text.document()->allFormats()[i];
		if (f.isBlockFormat()) {
			f.merge(block_format);
		}
	}
	preview_text.setTabStopDistance(tab_width);
	preview_text.document()->setIndentWidth(tab_width);

	// Set font
	preview_text.setFont(textFont());

	// Render text
	preview_text.render(&background, preview_text.pos());

	// Create preview pixmap
	if (preview) {
		*preview = background.scaled(480 * pixelratio, 270 * pixelratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);

		QPainter painter(preview);
		painter.setPen(Qt::NoPen);

		// Draw text cutout shadow
		painter.fillRect(QRectF(22, 46, 170, 118), QColor(0, 0, 0, 32));
		painter.fillRect(QRectF(24, 48, 166, 114), Qt::white);

		// Draw text cutout
		const int x2 = (x >= 24) ? (x - 24) : 0;
		const int y2 = (y >= 24) ? (y - 24) : 0;
		painter.drawImage(QPointF(26, 50), background, QRectF(x2 * pixelratio, y2 * pixelratio, 162 * pixelratio, 110 * pixelratio));
	}

	// Create preview icon
	if (icon) {
		*icon = QImage(258 * pixelratio, 153 * pixelratio, QImage::Format_ARGB32_Premultiplied);
		icon->fill(Qt::transparent);

		// Draw a flat quantized drop shadow; soft blurs sit outside the
		// FairyWriter pixel language and required a private Qt symbol.
		icon->setDevicePixelRatio(pixelratio);
		QPainter painter(icon);
		painter.setPen(Qt::NoPen);
		painter.fillRect(QRectF(12, 13, 240, 135), blackReplacement());
		painter.drawImage(QPointF(9, 9), background.scaled(240 * pixelratio, 135 * pixelratio, Qt::KeepAspectRatio, Qt::SmoothTransformation));

		// Draw text cutout shadow
		painter.fillRect(QRectF(20, 32, 85, 59), QColor(0, 0, 0, 32));
		painter.fillRect(QRectF(21, 33, 83, 57), Qt::white);

		// Draw text cutout
		const int x2 = (x >= 24) ? (x - 12 + tabWidth()) : 12 + tabWidth();
		const int y2 = (y >= 24) ? (y - 6) : 0;
		painter.drawImage(QPointF(22, 34), background, QRectF(x2 * pixelratio, y2 * pixelratio, 81 * pixelratio, 55 * pixelratio));
	}
}

//-----------------------------------------------------------------------------

QFuture<QColor> Theme::calculateLoadColor() const
{
	return QtConcurrent::run(averageImage, backgroundImage(), backgroundColor());
}

//-----------------------------------------------------------------------------

void Theme::setLoadColor(const QColor& color)
{
	setValue(d->load_color, ThemeToneMapper::mapColor(color, d->black_replacement));
}

//-----------------------------------------------------------------------------

void Theme::setBackgroundColor(const QColor& color)
{
	setValue(d->background_color, ThemeToneMapper::mapColor(color, d->black_replacement));
	if (d->ui_text_color.isValid()) {
		setValue(d->ui_text_color, ThemeToneMapper::readableText(
			d->ui_text_color, d->background_color, d->black_replacement));
	}
}

//-----------------------------------------------------------------------------

void Theme::setForegroundColor(const QColor& color)
{
	setValue(d->foreground_color, ThemeToneMapper::mapColor(color, d->black_replacement));
	if (d->text_color.isValid()) {
		setValue(d->text_color, ThemeToneMapper::readableText(
			d->text_color, d->foreground_color, d->black_replacement));
	}
}

//-----------------------------------------------------------------------------

void Theme::setShadowColor(const QColor& color)
{
	setValue(d->shadow_color, ThemeToneMapper::mapColor(color, d->black_replacement));
}

//-----------------------------------------------------------------------------

void Theme::setTextColor(const QColor& color)
{
	setValue(d->text_color, ThemeToneMapper::readableText(color, d->foreground_color, d->black_replacement));
}

//-----------------------------------------------------------------------------

void Theme::setMisspelledColor(const QColor& color)
{
	setValue(d->misspelled_color, ThemeToneMapper::mapColor(color, d->black_replacement));
}

//-----------------------------------------------------------------------------

void Theme::setBlackReplacement(const QColor& color)
{
	const QColor replacement = ThemeToneMapper::mapDeepColor(color);
	setValue(d->black_replacement, replacement);
	toneMap();
	setValue(d->load_color, ThemeToneMapper::mapColor(d->load_color, replacement));
	setValue(d->shadow_color, ThemeToneMapper::mapColor(d->shadow_color, replacement));
}

//-----------------------------------------------------------------------------

void Theme::setUiPrimaryColor(const QColor& color)
{
	setValue(d->ui_primary_color, ThemeToneMapper::mapColor(color, d->black_replacement));
}

//-----------------------------------------------------------------------------

void Theme::setUiSecondaryColor(const QColor& color)
{
	setValue(d->ui_secondary_color, ThemeToneMapper::mapColor(color, d->black_replacement));
}

//-----------------------------------------------------------------------------

void Theme::setUiAccentColor(const QColor& color)
{
	setValue(d->ui_accent_color, ThemeToneMapper::mapColor(color, d->black_replacement));
}

//-----------------------------------------------------------------------------

void Theme::setUiTextColor(const QColor& color)
{
	setValue(d->ui_text_color, ThemeToneMapper::readableText(
		color, d->background_color, d->black_replacement));
}

//-----------------------------------------------------------------------------

ThemePalette Theme::palette() const
{
	return {
		d->background_color,
		d->foreground_color,
		d->black_replacement,
		d->ui_primary_color,
		d->ui_secondary_color,
		d->ui_accent_color,
		d->ui_text_color,
		d->text_color,
		d->misspelled_color
	};
}

//-----------------------------------------------------------------------------

void Theme::setPalette(const ThemePalette& palette)
{
	const ThemePalette mapped = ThemeToneMapper::mapPalette(palette);
	setValue(d->black_replacement, mapped.deep);
	setValue(d->background_color, mapped.background);
	setValue(d->foreground_color, mapped.paper);
	setValue(d->ui_primary_color, mapped.primary);
	setValue(d->ui_secondary_color, mapped.secondary);
	setValue(d->ui_accent_color, mapped.accent);
	setValue(d->ui_text_color, mapped.ui_text);
	setValue(d->text_color, mapped.document_text);
	setValue(d->misspelled_color, mapped.misspelled);
}

//-----------------------------------------------------------------------------

void Theme::toneMap()
{
	setPalette(palette());
}

//-----------------------------------------------------------------------------

void Theme::applyDesign(const Theme& source)
{
	const QString id = d->id;
	const QString name = d->name;
	const bool is_default = d->is_default;
	const QString source_image = source.backgroundImage();
	const bool copy_default_image = source.isDefault()
		&& source.wallpaperSource() == WallpaperSource::Image
		&& QFile::exists(source_image);
	d = source.d;
	d.detach();
	setValue(d->id, id);
	setValue(d->name, name);
	setValue(d->is_default, is_default);
	if (copy_default_image) {
		setValue(d->background_path, source_image);
		setValue(d->background_image, QString());
	}
}

//-----------------------------------------------------------------------------

void Theme::setWallpaperSource(WallpaperSource source)
{
	const bool source_changed = wallpaperSource() != source;
	setValue(d->wallpaper_source, static_cast<int>(source));
	if (source_changed) {
		setValue(d->background_type, legacyBackgroundType(source, wallpaperFit()));
	}
	if (source == WallpaperSource::GeneratedMeadow) {
		setValue(d->wallpaper_mode, static_cast<int>(PresentationSurface::SolidPattern));
	} else if (source == WallpaperSource::GeneratedCherryBlossom) {
		setValue(d->wallpaper_mode, static_cast<int>(PresentationSurface::CherryBlossom));
	} else if (source == WallpaperSource::SolidColor) {
		setValue(d->wallpaper_mode, static_cast<int>(PresentationSurface::SolidColor));
	} else {
		setValue(d->wallpaper_mode, static_cast<int>(d->wallpaper_fit.value()));
	}
}

//-----------------------------------------------------------------------------

void Theme::setWallpaperFit(WallpaperFit fit)
{
	const bool fit_changed = wallpaperFit() != fit;
	setValue(d->wallpaper_fit, static_cast<int>(fit));
	if (fit_changed) {
		setValue(d->background_type, legacyBackgroundType(wallpaperSource(), fit));
	}
	if (wallpaperSource() == WallpaperSource::Image) {
		setValue(d->wallpaper_mode, static_cast<int>(fit));
	}
}

//-----------------------------------------------------------------------------

QString Theme::backgroundImage() const
{
	if (!d->background_path.isEmpty() && QFile::exists(d->background_path)) {
		return d->background_path;
	}
	if (!d->is_default) {
		return m_path + "/Images/" + d->background_image;
	} else {
		return m_path_default + "/images/" + d->background_image;
	}
}

//-----------------------------------------------------------------------------

void Theme::setBackgroundImage(const QString& path)
{
	if (d->background_path != path) {
		setValue(d->background_path, path);
		if (!d->background_path.isEmpty()) {
			setWallpaperSource(WallpaperSource::Image);
		} else if (wallpaperSource() == WallpaperSource::Image) {
			setWallpaperSource(WallpaperSource::SolidColor);
		}
	}
}

//-----------------------------------------------------------------------------

void Theme::setWallpaperFocalPoint(const QPointF& point)
{
	setValue(d->wallpaper_focal_x, qRound(std::clamp(point.x(), 0.0, 1.0) * 1000.0));
	setValue(d->wallpaper_focal_y, qRound(std::clamp(point.y(), 0.0, 1.0) * 1000.0));
}

//-----------------------------------------------------------------------------

QRect Theme::foregroundRect(const QSize& size, int margin, const qreal pixelratio,
	bool panel_visible, PanelSide panel_side) const
{
	Q_UNUSED(margin)
	Q_UNUSED(pixelratio)
	const PresentationLayout layout = PresentationGeometry::calculate({ size.width(), size.height() }, panel_visible, panel_side);
	const int frame = (shellMode() == 1 ? 7 : 6) * layout.scale;
	return QRect(layout.document.x, layout.document.y, layout.document.width, layout.document.height)
		.adjusted(frame, frame, -frame, -frame);
}

//-----------------------------------------------------------------------------

bool Theme::operator==(const Theme& theme) const
{
	return (d->name == theme.d->name)

		&& (d->background_type == theme.d->background_type)
		&& (d->background_color == theme.d->background_color)
		&& (d->background_path == theme.d->background_path)
		&& (d->background_image == theme.d->background_image)
		&& (d->wallpaper_mode == theme.d->wallpaper_mode)
		&& (d->wallpaper_source == theme.d->wallpaper_source)
		&& (d->wallpaper_fit == theme.d->wallpaper_fit)
		&& (d->wallpaper_focal_x == theme.d->wallpaper_focal_x)
		&& (d->wallpaper_focal_y == theme.d->wallpaper_focal_y)
		&& (d->black_replacement == theme.d->black_replacement)
		&& (d->ui_primary_color == theme.d->ui_primary_color)
		&& (d->ui_secondary_color == theme.d->ui_secondary_color)
		&& (d->ui_accent_color == theme.d->ui_accent_color)
		&& (d->ui_text_color == theme.d->ui_text_color)
		&& (d->ui_font_family == theme.d->ui_font_family)
		&& (d->ui_font_size == theme.d->ui_font_size)
		&& (d->shell_mode == theme.d->shell_mode)

		&& (d->foreground_color == theme.d->foreground_color)
		&& (d->foreground_opacity == theme.d->foreground_opacity)
		&& (d->foreground_margin == theme.d->foreground_margin)
		&& (d->foreground_padding == theme.d->foreground_padding)

		// Legacy import compatibility only (round-tripped, never rendered)
		&& (d->foreground_width == theme.d->foreground_width)
		&& (d->foreground_position == theme.d->foreground_position)
		&& (d->round_corners_enabled == theme.d->round_corners_enabled)
		&& (d->corner_radius == theme.d->corner_radius)
		&& (d->blur_enabled == theme.d->blur_enabled)
		&& (d->blur_radius == theme.d->blur_radius)
		&& (d->shadow_enabled == theme.d->shadow_enabled)
		&& (d->shadow_offset == theme.d->shadow_offset)
		&& (d->shadow_radius == theme.d->shadow_radius)
		&& (d->shadow_color == theme.d->shadow_color)

		&& (d->text_color == theme.d->text_color)
		&& (d->text_font == theme.d->text_font)
		&& (d->document_font_pixels == theme.d->document_font_pixels)
		&& (d->misspelled_color == theme.d->misspelled_color)

		&& (d->indent_first_line == theme.d->indent_first_line)
		&& (d->line_spacing == theme.d->line_spacing)
		&& (d->paragraph_spacing_above == theme.d->paragraph_spacing_above)
		&& (d->paragraph_spacing_below == theme.d->paragraph_spacing_below)
		&& (d->tab_width == theme.d->tab_width);
}

//-----------------------------------------------------------------------------

void Theme::reload()
{
	if (d->id.isEmpty()) {
		return;
	}

	const QSettings settings(filePath(d->id, d->is_default), QSettings::IniFormat);

	d->name = settings.value("Name", d->name).toString();

	// Load background settings
	d->background_type = settings.value("Background/Type", 0).toInt();
	d->background_color = settings.value("Background/Color", "#666666").toString();
	d->background_path = settings.value("Background/Image").toString();
	d->background_image = settings.value("Background/ImageFile").toString();

	const int legacy_type = d->background_type.value();
	const int migrated_mode = (legacy_type == 1) ? PresentationSurface::Tile
		: (legacy_type == 2 ? PresentationSurface::CleanCover
		: (legacy_type == 3 ? PresentationSurface::Stretch
		: (legacy_type == 4 ? PresentationSurface::AmbientExtension
		: (legacy_type >= 5 ? PresentationSurface::PixelatedCover : PresentationSurface::SolidPattern))));
	d->wallpaper_mode = settings.value("Presentation/WallpaperMode", migrated_mode).toInt();
	const int stored_mode = d->wallpaper_mode.value();
	const bool has_image = !d->background_path.isEmpty() || !d->background_image.isEmpty();
	const int migrated_source = stored_mode == PresentationSurface::CherryBlossom
		? static_cast<int>(WallpaperSource::GeneratedCherryBlossom)
		: (stored_mode == PresentationSurface::SolidPattern
			? static_cast<int>(WallpaperSource::GeneratedMeadow)
			: (stored_mode == PresentationSurface::SolidColor || !has_image
				? static_cast<int>(WallpaperSource::SolidColor)
				: static_cast<int>(WallpaperSource::Image)));
	d->wallpaper_source = settings.value("Presentation/WallpaperSource", migrated_source).toInt();
	const int migrated_fit = std::clamp(stored_mode, 0, static_cast<int>(WallpaperFit::Stretch));
	d->wallpaper_fit = settings.value("Presentation/WallpaperFit", migrated_fit).toInt();
	d->wallpaper_focal_x = settings.value("Presentation/FocalX", 500).toInt();
	d->wallpaper_focal_y = settings.value("Presentation/FocalY", 500).toInt();
	// Palette loading is a deliberate two-stage pipeline: every stored role is
	// first snapped to BGR555 exactly as historical releases did (this fixes
	// the published bundled-theme colors), then the single toneMap() call at
	// the end of reload() applies the shared tonal curve and readability
	// rules. Collapsing the first stage into toneMap() shifts shipped
	// palettes — see testBundledPaletteStability before touching this.
	d->black_replacement = SnesStyle::quantize(settings.value("Presentation/BlackReplacement", "#293142").toString());
	const SnesColor replacement = SnesStyle::toSnesColor(d->black_replacement);
	d->ui_primary_color = SnesStyle::quantize(settings.value("Presentation/UiPrimary", "#579b68").toString(), replacement);
	d->ui_secondary_color = SnesStyle::quantize(settings.value("Presentation/UiSecondary", "#f1cf68").toString(), replacement);
	d->ui_accent_color = SnesStyle::quantize(settings.value("Presentation/UiAccent", "#315f50").toString(), replacement);
	const QColor ui_text_preference = SnesStyle::quantize(
		settings.value("Presentation/UiText", d->black_replacement.name()).toString(), replacement);
	d->ui_text_color = SnesStyle::readableTextColor(ui_text_preference, d->background_color, replacement);
	d->ui_font_family = settings.value("Presentation/UiFont", "SMW2: Yoshi's Island").toString();
	d->ui_font_size = settings.value("Presentation/UiFontSize", 18).toInt();
	d->shell_mode = settings.value("Presentation/ShellMode", 0).toInt();
	d->background_color = SnesStyle::quantize(d->background_color, replacement);

	// Load foreground settings
	d->foreground_color = SnesStyle::quantize(settings.value("Foreground/Color", "#ffffff").toString(), replacement);
	d->foreground_opacity = settings.value("Foreground/Opacity", 100).toInt();
	d->foreground_margin = settings.value("Foreground/Margin", 65).toInt();
	d->foreground_padding = settings.value("Foreground/Padding", 10).toInt();

	// Legacy import compatibility only: round-tripped for old readers and
	// .fwtz exchange, never rendered, so raw values pass through untouched.
	d->foreground_width = settings.value("Foreground/Width", 700).toInt();
	d->foreground_position = settings.value("Foreground/Position", 1).toInt();
	const int rounding = settings.value("Foreground/Rounding", 0).toInt();
	if (rounding > 0) {
		d->round_corners_enabled = true;
		d->corner_radius = rounding;
	} else {
		d->round_corners_enabled = false;
		d->corner_radius = settings.value("Foreground/RoundingDisabled", 10).toInt();
	}
	d->blur_enabled = settings.value("ForegroundBlur/Enabled", false).toBool();
	d->blur_radius = settings.value("ForegroundBlur/Radius", 32).toInt();
	d->shadow_enabled = settings.value("ForegroundShadow/Enabled", !settings.contains("Foreground/Color")).toBool();
	d->shadow_color = settings.value("ForegroundShadow/Color", "#000000").toString();
	d->shadow_radius = settings.value("ForegroundShadow/Radius", 8).toInt();
	d->shadow_offset = settings.value("ForegroundShadow/Offset", 2).toInt();

	// Load text settings
	d->text_color = SnesStyle::quantize(settings.value("Text/Color", "#000000").toString(), replacement);
	d->text_font.fromString(settings.value("Text/Font", QFont("Times New Roman").toString()).toString());
	d->document_font_pixels = settings.value("Text/VirtualPixelHeight", 8).toInt();
	if (d->is_default) {
#if defined(Q_OS_MAC)
		int point_size = 14;
#elif defined(Q_OS_UNIX)
		int point_size = 10;
#else
		int point_size = 12;
#endif
		d->text_font.setPointSize(std::max(point_size, QFont().pointSize()));
	}
	d->misspelled_color = SnesStyle::quantize(settings.value("Text/Misspelled", "#ff0000").toString(), replacement);

	// Load spacings
	d->indent_first_line = settings.value("Spacings/IndentFirstLine", false).toBool();
	d->line_spacing = settings.value("Spacings/LineSpacing", 100).toInt();
	d->paragraph_spacing_above = settings.value("Spacings/ParagraphAbove", 0).toInt();
	d->paragraph_spacing_below = settings.value("Spacings/ParagraphBelow", 0).toInt();
	d->tab_width = settings.value("Spacings/TabWidth", 48).toInt();

	const QColor stored_load_color = settings.value("LoadColor", d->background_color.name()).toString();
	toneMap();
	d->load_color = ThemeToneMapper::mapColor(stored_load_color, d->black_replacement);
}

//-----------------------------------------------------------------------------

void Theme::write()
{
	if (d->name.isEmpty()) {
		return;
	}

	const bool uses_image = wallpaperSource() == WallpaperSource::Image;
	if (uses_image && d->is_default) {
		if (!d->background_image.isEmpty()) {
			d->background_image = copyImage(m_path_default + "/images/" + d->background_image);
		}
	}
	if (uses_image && !d->background_path.isEmpty() && QFile::exists(d->background_path)) {
		d->background_image = copyImage(d->background_path);
	}
	if (!uses_image) {
		d->background_path.clear();
		d->background_image.clear();
	}
	d->is_default = false;

	QSettings settings(filePath(d->id), QSettings::IniFormat);

	settings.setValue("LoadColor", d->load_color.name());
	settings.setValue("Name", d->name);

	// Store background settings
	settings.setValue("Background/Type", d->background_type.value());
	settings.setValue("Background/Color", d->background_color.name());
	if (uses_image && !d->background_path.isEmpty()) {
		settings.setValue("Background/Image", d->background_path);
	} else {
		settings.remove("Background/Image");
	}
	if (uses_image && !d->background_image.isEmpty()) {
		settings.setValue("Background/ImageFile", d->background_image);
	} else {
		settings.remove("Background/ImageFile");
	}

	settings.setValue("Presentation/WallpaperMode", d->wallpaper_mode.value());
	settings.setValue("Presentation/WallpaperSource", d->wallpaper_source.value());
	settings.setValue("Presentation/WallpaperFit", d->wallpaper_fit.value());
	settings.setValue("Presentation/FocalX", d->wallpaper_focal_x.value());
	settings.setValue("Presentation/FocalY", d->wallpaper_focal_y.value());
	settings.setValue("Presentation/BlackReplacement", d->black_replacement.name());
	settings.setValue("Presentation/UiPrimary", d->ui_primary_color.name());
	settings.setValue("Presentation/UiSecondary", d->ui_secondary_color.name());
	settings.setValue("Presentation/UiAccent", d->ui_accent_color.name());
	settings.setValue("Presentation/UiText", d->ui_text_color.name());
	settings.setValue("Presentation/UiFont", d->ui_font_family);
	settings.setValue("Presentation/UiFontSize", d->ui_font_size.value());
	settings.setValue("Presentation/ShellMode", d->shell_mode.value());

	// Store foreground settings
	settings.setValue("Foreground/Color", d->foreground_color.name());
	settings.setValue("Foreground/Opacity", d->foreground_opacity.value());
	settings.setValue("Foreground/Margin", d->foreground_margin.value());
	settings.setValue("Foreground/Padding", d->foreground_padding.value());

	// Legacy import compatibility only (round-tripped, never rendered)
	settings.setValue("Foreground/Width", d->foreground_width.value());
	settings.setValue("Foreground/Position", d->foreground_position.value());
	if (d->round_corners_enabled) {
		settings.setValue("Foreground/Rounding", d->corner_radius.value());
		settings.setValue("Foreground/RoundingDisabled", 0);
	} else {
		settings.setValue("Foreground/Rounding", 0);
		settings.setValue("Foreground/RoundingDisabled", d->corner_radius.value());
	}
	settings.setValue("ForegroundBlur/Enabled", d->blur_enabled);
	settings.setValue("ForegroundBlur/Radius", d->blur_radius.value());
	settings.setValue("ForegroundShadow/Enabled", d->shadow_enabled);
	settings.setValue("ForegroundShadow/Color", d->shadow_color.name());
	settings.setValue("ForegroundShadow/Radius", d->shadow_radius.value());
	settings.setValue("ForegroundShadow/Offset", d->shadow_offset.value());

	// Store text settings
	settings.setValue("Text/Color", d->text_color.name());
	settings.setValue("Text/Font", d->text_font.toString());
	settings.setValue("Text/VirtualPixelHeight", d->document_font_pixels.value());
	settings.setValue("Text/Misspelled", d->misspelled_color.name());

	// Store spacings
	settings.setValue("Spacings/IndentFirstLine", d->indent_first_line);
	settings.setValue("Spacings/LineSpacing", d->line_spacing.value());
	settings.setValue("Spacings/ParagraphAbove", d->paragraph_spacing_above.value());
	settings.setValue("Spacings/ParagraphBelow", d->paragraph_spacing_below.value());
	settings.setValue("Spacings/TabWidth", d->tab_width.value());
}

//-----------------------------------------------------------------------------
