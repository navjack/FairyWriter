/*
	SPDX-FileCopyrightText: 2009 Graeme Gott <graeme@gottcode.org>
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "theme_dialog.h"

#include "color_button.h"
#include "focal_point_control.h"
#include "image_button.h"
#include "presentation_surface.h"
#include "snes_color.h"
#include "snes_style.h"
#include "theme_tone_mapper.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QShowEvent>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace
{

QGridLayout* compactGrid(QGroupBox* group)
{
	group->setTitle(QString());
	group->setFlat(true);
	auto* layout = new QGridLayout(group);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setHorizontalSpacing(6);
	layout->setVerticalSpacing(2);
	return layout;
}

void addFullField(QGridLayout* layout, int row, const QString& text, QWidget* field, QWidget* parent)
{
	layout->addWidget(new QLabel(text, parent), row, 0);
	layout->addWidget(field, row, 1, 1, 3);
}

void addHalfField(QGridLayout* layout, int row, int column, const QString& text, QWidget* field, QWidget* parent)
{
	const int base = column * 2;
	layout->addWidget(new QLabel(text, parent), row, base);
	layout->addWidget(field, row, base + 1);
}

QWidget* page(QStackedWidget* pages, std::initializer_list<QWidget*> widgets)
{
	auto* result = new QWidget(pages);
	result->setObjectName(QStringLiteral("FairyWriterThemeEditorPage_%1").arg(pages->count()));
	auto* layout = new QVBoxLayout(result);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	for (QWidget* widget : widgets) {
		layout->addWidget(widget);
	}
	pages->addWidget(result);
	return result;
}

}

ThemeDialog::ThemeDialog(const Theme& theme, bool new_theme, QWidget* parent)
	: QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint)
	, m_opening(theme)
	, m_draft(theme)
	, m_syncing(false)
	, m_finished(false)
	, m_preview_started(false)
	, m_continuous_edit(false)
	, m_history_index(0)
{
	setWindowTitle(new_theme ? tr("New Theme") : tr("Edit Theme"));
	setWindowModality(Qt::WindowModal);
	setProperty("fairywriter.panelFill", true);
	setProperty("fairywriter.compactControls", true);
	QFont inspector_font = font();
	if (inspector_font.pointSizeF() > 18.0) {
		inspector_font.setPointSizeF(18.0);
		setFont(inspector_font);
	}
	buildUi();
	syncControls(m_draft);
	m_history.append(m_draft);
	updateHistoryButtons();
	updateSaveButton();
	resize(QSettings().value("ThemeDialog/Size", sizeHint()).toSize());
}

void ThemeDialog::buildUi()
{
	m_pages = new QStackedWidget(this);
	m_pages->setObjectName(QStringLiteral("FairyWriterThemeEditorPages"));
	m_pages->setMinimumSize(0, 0);
	m_pages->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

	// 1. Identity and generated scene
	auto* identity = new QGroupBox(tr("Identity & Scene"), this);
	m_name = new QLineEdit(identity);
	m_wallpaper_source = new QComboBox(identity);
	m_wallpaper_source->setObjectName(QStringLiteral("FairyWriterThemeWallpaperSource"));
	m_wallpaper_source->addItems({ tr("Solid Color"), tr("Generated Meadow"), tr("Cherry Blossom"), tr("Image") });
	auto* identity_layout = compactGrid(identity);
	addFullField(identity_layout, 0, tr("Name:"), m_name, identity);
	addFullField(identity_layout, 1, tr("Scene:"), m_wallpaper_source, identity);
	page(m_pages, {identity});

	// 2. Image and fit
	auto* wallpaper = new QGroupBox(tr("Wallpaper Image"), this);
	m_background_image = new ImageButton(wallpaper);
	m_background_image->setIconSize(QSize(32, 32));
	m_clear_image = new QPushButton(tr("Remove Image"), wallpaper);
	m_wallpaper_fit = new QComboBox(wallpaper);
	m_wallpaper_fit->addItems({ tr("Pixel Cover"), tr("Ambient Extend"), tr("Clean Cover"), tr("Tile"), tr("Stretch") });
	auto* wallpaper_layout = compactGrid(wallpaper);
	wallpaper_layout->addWidget(new QLabel(tr("Image:"), wallpaper), 0, 0);
	wallpaper_layout->addWidget(m_background_image, 0, 1);
	wallpaper_layout->addWidget(m_clear_image, 0, 2, 1, 2);
	addFullField(wallpaper_layout, 1, tr("Fit:"), m_wallpaper_fit, wallpaper);
	page(m_pages, {wallpaper});

	// 3. Focal point and grade
	auto* focal = new QGroupBox(tr("Focal Point & Grade"), this);
	m_focal_point = new FocalPointControl(focal);
	m_focal_x = new QSpinBox(focal);
	m_focal_y = new QSpinBox(focal);
	for (QSpinBox* spin : {m_focal_x, m_focal_y}) {
		spin->setRange(0, 100);
		spin->setSuffix(QLocale().percent());
	}
	m_focal_x->setPrefix(tr("X "));
	m_focal_y->setPrefix(tr("Y "));
	m_background_color = new ColorButton(focal);
	m_background_color->setToolTip(tr("Sky / base color"));
	auto* focal_layout = compactGrid(focal);
	focal_layout->addWidget(m_focal_point, 0, 0, 1, 4);
	focal_layout->addWidget(m_focal_x, 1, 0);
	focal_layout->addWidget(m_focal_y, 1, 1);
	focal_layout->addWidget(new QLabel(tr("Base:"), focal), 1, 2);
	focal_layout->addWidget(m_background_color, 1, 3);
	page(m_pages, {focal});

	// 4. Shell and paper
	auto* shell = new QGroupBox(tr("Shell & Paper"), this);
	m_shell_style = new QComboBox(shell);
	m_shell_style->addItems({ tr("Layered Frame"), tr("Terminal Bezel") });
	m_paper_color = new ColorButton(shell);
	m_paper_opacity = new QSpinBox(shell);
	m_paper_opacity->setRange(0, 100);
	m_paper_opacity->setSuffix(QLocale().percent());
	m_paper_opacity->setPrefix(tr("Opacity "));
	m_paper_padding = new QSpinBox(shell);
	m_paper_padding->setRange(0, 250);
	m_paper_padding->setPrefix(tr("Pad "));
	m_paper_padding->setSuffix(tr(" px"));
	m_paper_padding->setToolTip(tr("Paper padding in source pixels"));
	auto* shell_layout = compactGrid(shell);
	addFullField(shell_layout, 0, tr("Shell:"), m_shell_style, shell);
	addFullField(shell_layout, 1, tr("Paper:"), m_paper_color, shell);
	shell_layout->addWidget(m_paper_opacity, 2, 0, 1, 2);
	shell_layout->addWidget(m_paper_padding, 2, 2, 1, 2);
	page(m_pages, {shell});

	// 5. Frame palette
	auto* frame_palette = new QGroupBox(tr("Frame Palette"), this);
	m_deep_color = new ColorButton(frame_palette);
	m_primary_color = new ColorButton(frame_palette);
	m_secondary_color = new ColorButton(frame_palette);
	m_accent_color = new ColorButton(frame_palette);
	auto* frame_layout = compactGrid(frame_palette);
	addHalfField(frame_layout, 0, 0, tr("Deep:"), m_deep_color, frame_palette);
	addHalfField(frame_layout, 0, 1, tr("Main:"), m_primary_color, frame_palette);
	addHalfField(frame_layout, 1, 0, tr("Cherry:"), m_secondary_color, frame_palette);
	addHalfField(frame_layout, 1, 1, tr("Accent:"), m_accent_color, frame_palette);
	page(m_pages, {frame_palette});

	// 6. Text palette
	auto* text_palette = new QGroupBox(tr("Text Palette"), this);
	m_ui_text_color = new ColorButton(text_palette);
	m_document_text_color = new ColorButton(text_palette);
	m_misspelled_color = new ColorButton(text_palette);
	auto* text_palette_layout = compactGrid(text_palette);
	addHalfField(text_palette_layout, 0, 0, tr("UI:"), m_ui_text_color, text_palette);
	addHalfField(text_palette_layout, 0, 1, tr("Doc:"), m_document_text_color, text_palette);
	addFullField(text_palette_layout, 1, tr("Spell:"), m_misspelled_color, text_palette);
	page(m_pages, {text_palette});

	// 7. Interface typography
	auto* ui_type = new QGroupBox(tr("Interface Typography"), this);
	m_ui_font = new QFontComboBox(ui_type);
	m_ui_font->setEditable(false);
	m_ui_font_size = new QSpinBox(ui_type);
	m_ui_font_size->setRange(6, 32);
	m_ui_font_size->setSuffix(tr(" pt"));
	auto* ui_type_layout = compactGrid(ui_type);
	addFullField(ui_type_layout, 0, tr("Font:"), m_ui_font, ui_type);
	addFullField(ui_type_layout, 1, tr("Size:"), m_ui_font_size, ui_type);
	page(m_pages, {ui_type});

	// 8. Document typography
	auto* document_type = new QGroupBox(tr("Document Typography"), this);
	m_document_font = new QFontComboBox(document_type);
	m_document_font->setEditable(false);
	m_document_font_pixels = new QSpinBox(document_type);
	m_document_font_pixels->setRange(4, 32);
	m_document_font_pixels->setSuffix(tr(" px"));
	m_document_font_pixels->setToolTip(tr("Document glyph height in source pixels"));
	auto* document_type_layout = compactGrid(document_type);
	addFullField(document_type_layout, 0, tr("Font:"), m_document_font, document_type);
	addFullField(document_type_layout, 1, tr("Glyphs:"), m_document_font_pixels, document_type);
	page(m_pages, {document_type});

	// 9. Paragraph layout
	auto* paragraph = new QGroupBox(tr("Paragraph Layout"), this);
	m_line_spacing_type = new QComboBox(paragraph);
	m_line_spacing_type->addItems({ tr("1×"), tr("1.5×"), tr("2×"), tr("Alt") });
	m_line_spacing_type->setItemData(3, tr("Custom line height"), Qt::ToolTipRole);
	m_line_spacing = new QSpinBox(paragraph);
	m_line_spacing->setRange(50, 1000);
	m_line_spacing->setSuffix(QLocale().percent());
	m_tab_width = new QSpinBox(paragraph);
	m_paragraph_above = new QSpinBox(paragraph);
	m_paragraph_below = new QSpinBox(paragraph);
	for (QSpinBox* spin : {m_tab_width, m_paragraph_above, m_paragraph_below}) {
		spin->setRange(spin == m_tab_width ? 1 : 0, 1000);
		spin->setSuffix(tr(" px"));
		spin->setToolTip(tr("Value in source pixels"));
	}
	m_indent_first_line = new QCheckBox(tr("Indent"), paragraph);
	m_indent_first_line->setToolTip(tr("Indent first line"));
	auto* paragraph_layout = compactGrid(paragraph);
	addHalfField(paragraph_layout, 0, 0, tr("Lines:"), m_line_spacing_type, paragraph);
	addHalfField(paragraph_layout, 0, 1, tr("Height:"), m_line_spacing, paragraph);
	addHalfField(paragraph_layout, 1, 0, tr("Tab:"), m_tab_width, paragraph);
	addHalfField(paragraph_layout, 1, 1, tr("Above:"), m_paragraph_above, paragraph);
	addHalfField(paragraph_layout, 2, 0, tr("Below:"), m_paragraph_below, paragraph);
	paragraph_layout->addWidget(m_indent_first_line, 2, 2, 1, 2);
	page(m_pages, {paragraph});

	// 10. Palette tools
	auto* tools = new QGroupBox(tr("Palette Tools"), this);
	auto* derive = new QPushButton(tr("Derive"), tools);
	auto* baseline = new QPushButton(tr("Baseline"), tools);
	auto* remap = new QPushButton(tr("Remap"), tools);
	derive->setToolTip(tr("Derive frame colors from the current wallpaper"));
	baseline->setToolTip(tr("Restore the mint-and-cherry baseline"));
	remap->setToolTip(tr("Remap every role through the shared tone mapper"));
	auto* tools_layout = compactGrid(tools);
	tools_layout->addWidget(derive, 0, 0);
	tools_layout->addWidget(baseline, 1, 0);
	tools_layout->addWidget(remap, 2, 0);
	page(m_pages, {tools});

	m_category = new QComboBox(this);
	m_category->setObjectName(QStringLiteral("FairyWriterThemeEditorCategory"));
	m_category->addItems({
		tr("Identity & Scene"), tr("Wallpaper Image"), tr("Focal Point & Grade"),
		tr("Shell & Paper"), tr("Frame Palette"), tr("Text Palette"),
		tr("Interface Typography"), tr("Document Typography"),
		tr("Paragraph Layout"), tr("Palette Tools")
	});
	connect(m_category, &QComboBox::currentIndexChanged, m_pages, &QStackedWidget::setCurrentIndex);

	m_undo = new QPushButton(tr("Undo"), this);
	m_redo = new QPushButton(tr("Redo"), this);
	m_reset_page = new QPushButton(tr("Page"), this);
	m_reset_all = new QPushButton(tr("All"), this);
	m_undo->setObjectName(QStringLiteral("FairyWriterThemeUndo"));
	m_redo->setObjectName(QStringLiteral("FairyWriterThemeRedo"));
	m_reset_page->setObjectName(QStringLiteral("FairyWriterThemeResetPage"));
	m_reset_all->setObjectName(QStringLiteral("FairyWriterThemeResetAll"));
	m_reset_page->setToolTip(tr("Reset Page"));
	m_reset_all->setToolTip(tr("Reset All"));
	auto* history_layout = new QHBoxLayout;
	history_layout->setContentsMargins(0, 0, 0, 0);
	history_layout->setSpacing(0);
	history_layout->addWidget(m_undo);
	history_layout->addWidget(m_redo);
	history_layout->addWidget(m_reset_page);
	history_layout->addWidget(m_reset_all);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, Qt::Horizontal, this);
	m_save = buttons->button(QDialogButtonBox::Save);
	connect(buttons, &QDialogButtonBox::accepted, this, &ThemeDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &ThemeDialog::reject);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(4);
	layout->addWidget(m_category);
	layout->addWidget(m_pages, 1);
	layout->addLayout(history_layout);
	layout->addWidget(buttons);

	connect(m_name, &QLineEdit::textChanged, this, &ThemeDialog::controlsChanged);
	for (QComboBox* combo : {m_wallpaper_source, m_wallpaper_fit, m_shell_style, m_line_spacing_type}) {
		connect(combo, &QComboBox::currentIndexChanged, this, &ThemeDialog::controlsChanged);
	}
	for (QSpinBox* spin : {m_focal_x, m_focal_y, m_paper_opacity, m_paper_padding, m_ui_font_size,
		m_document_font_pixels, m_line_spacing, m_tab_width, m_paragraph_above, m_paragraph_below}) {
		connect(spin, &QSpinBox::valueChanged, this, &ThemeDialog::controlsChanged);
	}
	for (ColorButton* color : {m_background_color, m_paper_color, m_deep_color, m_primary_color,
		m_secondary_color, m_accent_color, m_ui_text_color, m_document_text_color, m_misspelled_color}) {
		color->setSwatchWidth(48);
		connect(color, &ColorButton::changed, this, &ThemeDialog::controlsChanged);
	}
	connect(m_ui_font, &QFontComboBox::currentFontChanged, this, [this](const QFont& font) {
		if (m_syncing) return;
		Theme candidate = m_draft;
		candidate.setUiFontFamily(font.family());
		recordDraft(candidate);
	});
	connect(m_document_font, &QFontComboBox::currentFontChanged, this, [this](const QFont& font) {
		if (m_syncing) return;
		Theme candidate = m_draft;
		QFont document_font = candidate.textFont();
		document_font.setFamily(font.family());
		candidate.setTextFont(document_font);
		recordDraft(candidate);
	});
	connect(m_indent_first_line, &QCheckBox::toggled, this, &ThemeDialog::controlsChanged);
	connect(m_background_image, &ImageButton::changed, this, &ThemeDialog::imageChanged);
	connect(m_clear_image, &QPushButton::clicked, m_background_image, &ImageButton::unsetImage);
	connect(m_focal_point, &FocalPointControl::pointChanged, this, [this](const QPointF& point) {
		if (m_syncing) return;
		m_syncing = true;
		m_focal_x->setValue(qRound(point.x() * 100.0));
		m_focal_y->setValue(qRound(point.y() * 100.0));
		m_syncing = false;
		controlsChanged();
	});
	connect(m_focal_point, &FocalPointControl::editingStarted, this, [this] {
		if (m_continuous_edit) return;
		m_continuous_edit = true;
		m_continuous_origin = m_draft;
	});
	connect(m_focal_point, &FocalPointControl::editingFinished, this, [this] {
		if (!m_continuous_edit) return;
		m_continuous_edit = false;
		if (m_draft == m_continuous_origin) return;
		pushHistory(m_draft);
		updateHistoryButtons();
	});
	connect(m_undo, &QPushButton::clicked, this, &ThemeDialog::undo);
	connect(m_redo, &QPushButton::clicked, this, &ThemeDialog::redo);
	connect(m_reset_page, &QPushButton::clicked, this, &ThemeDialog::resetPage);
	connect(m_reset_all, &QPushButton::clicked, this, &ThemeDialog::resetAll);
	connect(derive, &QPushButton::clicked, this, &ThemeDialog::derivePalette);
	connect(baseline, &QPushButton::clicked, this, &ThemeDialog::restoreBaseline);
	connect(remap, &QPushButton::clicked, this, &ThemeDialog::remapPalette);

	auto* undo_shortcut = new QShortcut(QKeySequence::Undo, this);
	connect(undo_shortcut, &QShortcut::activated, this, &ThemeDialog::undo);
	auto* redo_shortcut = new QShortcut(QKeySequence::Redo, this);
	connect(redo_shortcut, &QShortcut::activated, this, &ThemeDialog::redo);
	auto* save_shortcut = new QShortcut(QKeySequence::Save, this);
	connect(save_shortcut, &QShortcut::activated, this, &ThemeDialog::accept);
}

void ThemeDialog::syncControls(const Theme& theme)
{
	m_syncing = true;
	const QColor deep = theme.blackReplacement();
	m_deep_color->setColorMapper([](const QColor& color) { return ThemeToneMapper::mapDeepColor(color); });
	auto mapped = [deep](const QColor& color) { return ThemeToneMapper::mapColor(color, deep); };
	for (ColorButton* color : {m_background_color, m_paper_color, m_primary_color,
		m_secondary_color, m_accent_color, m_misspelled_color}) {
		color->setColorMapper(mapped);
	}
	m_ui_text_color->setColorMapper([theme](const QColor& color) {
		return ThemeToneMapper::readableText(color, theme.backgroundColor(), theme.blackReplacement());
	});
	m_document_text_color->setColorMapper([theme](const QColor& color) {
		return ThemeToneMapper::readableText(color, theme.foregroundColor(), theme.blackReplacement());
	});
	m_name->setText(theme.name());
	m_wallpaper_source->setCurrentIndex(static_cast<int>(theme.wallpaperSource()));
	m_wallpaper_fit->setCurrentIndex(static_cast<int>(theme.wallpaperFit()));
	m_background_image->setImage(theme.backgroundImage(), theme.backgroundPath());
	m_focal_x->setValue(qRound(theme.wallpaperFocalPoint().x() * 100.0));
	m_focal_y->setValue(qRound(theme.wallpaperFocalPoint().y() * 100.0));
	m_focal_point->setPoint(theme.wallpaperFocalPoint());
	m_background_color->setColor(theme.backgroundColor());
	m_shell_style->setCurrentIndex(static_cast<int>(theme.shellStyle()));
	m_paper_color->setColor(theme.foregroundColor());
	m_paper_opacity->setValue(theme.foregroundOpacity());
	m_paper_padding->setValue(theme.foregroundPadding());
	m_deep_color->setColor(theme.blackReplacement());
	m_primary_color->setColor(theme.uiPrimaryColor());
	m_secondary_color->setColor(theme.uiSecondaryColor());
	m_accent_color->setColor(theme.uiAccentColor());
	m_ui_text_color->setColor(theme.uiTextColor());
	m_document_text_color->setColor(theme.textColor());
	m_misspelled_color->setColor(theme.misspelledColor());
	const QList<QColor> companions{
		theme.backgroundColor(), theme.foregroundColor(), theme.blackReplacement(),
		theme.uiPrimaryColor(), theme.uiSecondaryColor(), theme.uiAccentColor(),
		theme.uiTextColor(), theme.textColor(), theme.misspelledColor()
	};
	for (ColorButton* color : {m_background_color, m_paper_color, m_deep_color, m_primary_color,
		m_secondary_color, m_accent_color, m_ui_text_color, m_document_text_color, m_misspelled_color}) {
		color->setCompanionColors(companions);
	}
	m_ui_font->setCurrentFont(QFont(theme.uiFontFamily()));
	m_ui_font_size->setValue(theme.uiFontSize());
	m_document_font->setCurrentFont(theme.textFont());
	m_document_font_pixels->setValue(theme.documentFontPixels());
	const int spacing = theme.lineSpacing();
	m_line_spacing_type->setCurrentIndex(spacing == 100 ? 0 : (spacing == 150 ? 1 : (spacing == 200 ? 2 : 3)));
	m_line_spacing->setValue(spacing);
	m_tab_width->setValue(theme.tabWidth());
	m_paragraph_above->setValue(theme.spacingAboveParagraph());
	m_paragraph_below->setValue(theme.spacingBelowParagraph());
	m_indent_first_line->setChecked(theme.indentFirstLine());
	m_focal_point->setTheme(theme);
	m_syncing = false;
	updateConditionalControls();
}

Theme ThemeDialog::themeFromControls() const
{
	Theme theme = m_draft;
	theme.setName(m_name->text().simplified());
	theme.setBackgroundImage(m_background_image->toString());
	theme.setWallpaperSource(static_cast<Theme::WallpaperSource>(m_wallpaper_source->currentIndex()));
	theme.setWallpaperFit(static_cast<Theme::WallpaperFit>(m_wallpaper_fit->currentIndex()));
	theme.setWallpaperFocalPoint(QPointF(m_focal_x->value() / 100.0, m_focal_y->value() / 100.0));
	theme.setShellStyle(static_cast<Theme::ShellStyle>(m_shell_style->currentIndex()));
	theme.setUiFontSize(m_ui_font_size->value());
	theme.setDocumentFontPixels(m_document_font_pixels->value());
	theme.setForegroundOpacity(m_paper_opacity->value());
	theme.setForegroundPadding(m_paper_padding->value());
	theme.setLineSpacing(m_line_spacing_type->currentIndex() == 0 ? 100
		: (m_line_spacing_type->currentIndex() == 1 ? 150
		: (m_line_spacing_type->currentIndex() == 2 ? 200 : m_line_spacing->value())));
	theme.setTabWidth(m_tab_width->value());
	theme.setSpacingAboveParagraph(m_paragraph_above->value());
	theme.setSpacingBelowParagraph(m_paragraph_below->value());
	theme.setIndentFirstLine(m_indent_first_line->isChecked());
	ThemePalette palette;
	palette.background = m_background_color->color();
	palette.paper = m_paper_color->color();
	palette.deep = m_deep_color->color();
	palette.primary = m_primary_color->color();
	palette.secondary = m_secondary_color->color();
	palette.accent = m_accent_color->color();
	palette.ui_text = m_ui_text_color->color();
	palette.document_text = m_document_text_color->color();
	palette.misspelled = m_misspelled_color->color();
	theme.setPalette(palette);
	return theme;
}

void ThemeDialog::controlsChanged()
{
	if (m_syncing) return;
	const Theme candidate = themeFromControls();
	recordDraft(candidate);
	updateConditionalControls();
}

void ThemeDialog::imageChanged()
{
	if (m_syncing) return;
	if (!m_background_image->toString().isEmpty()) {
		m_wallpaper_source->setCurrentIndex(static_cast<int>(Theme::WallpaperSource::Image));
	} else if (m_wallpaper_source->currentIndex() == static_cast<int>(Theme::WallpaperSource::Image)) {
		m_wallpaper_source->setCurrentIndex(static_cast<int>(Theme::WallpaperSource::SolidColor));
	}
	controlsChanged();
}

void ThemeDialog::recordDraft(const Theme& theme)
{
	if (theme == m_draft) {
		updateSaveButton();
		return;
	}
	publishDraft(theme, !m_continuous_edit);
}

void ThemeDialog::pushHistory(const Theme& theme)
{
	while (m_history.size() > m_history_index + 1) m_history.removeLast();
	m_history.append(theme);
	if (m_history.size() > 64) m_history.removeFirst();
	m_history_index = m_history.size() - 1;
}

void ThemeDialog::publishDraft(const Theme& theme, bool record)
{
	m_draft = theme;
	if (record) {
		pushHistory(m_draft);
	}
	syncControls(m_draft);
	updateHistoryButtons();
	updateSaveButton();
	Q_EMIT previewChanged(m_draft);
}

void ThemeDialog::undo()
{
	if (m_history_index <= 0) return;
	--m_history_index;
	publishDraft(m_history.at(m_history_index), false);
}

void ThemeDialog::redo()
{
	if (m_history_index + 1 >= m_history.size()) return;
	++m_history_index;
	publishDraft(m_history.at(m_history_index), false);
}

void ThemeDialog::updateHistoryButtons()
{
	m_undo->setEnabled(m_history_index > 0);
	m_redo->setEnabled(m_history_index + 1 < m_history.size());
}

void ThemeDialog::updateSaveButton()
{
	m_save->setEnabled(!m_draft.name().isEmpty()
		&& (m_draft.name() == m_opening.name() || !Theme::exists(m_draft.name())));
}

void ThemeDialog::updateConditionalControls()
{
	const bool image = m_wallpaper_source->currentIndex() == static_cast<int>(Theme::WallpaperSource::Image);
	m_background_image->setEnabled(image);
	m_clear_image->setEnabled(image && !m_background_image->toString().isEmpty());
	m_wallpaper_fit->setEnabled(image);
	m_focal_point->setEnabled(image);
	m_focal_point->setToolTip(image ? QString() : tr("Focal point applies to image wallpapers."));
	m_focal_x->setEnabled(image);
	m_focal_y->setEnabled(image);
	m_line_spacing->setEnabled(m_line_spacing_type->currentIndex() == 3);
}

void ThemeDialog::resetPage()
{
	Theme reset = m_draft;
	const int category = m_category->currentIndex();
	if (category == 0) {
		reset.setName(m_opening.name());
		reset.setWallpaperSource(m_opening.wallpaperSource());
	} else if (category == 1) {
		reset.setBackgroundImage(m_opening.backgroundPath());
		reset.setWallpaperSource(m_opening.wallpaperSource());
		reset.setWallpaperFit(m_opening.wallpaperFit());
	} else if (category == 2) {
		reset.setWallpaperFocalPoint(m_opening.wallpaperFocalPoint());
		reset.setBackgroundColor(m_opening.backgroundColor());
	} else if (category == 3) {
		reset.setShellStyle(m_opening.shellStyle());
		reset.setForegroundColor(m_opening.foregroundColor());
		reset.setForegroundOpacity(m_opening.foregroundOpacity());
		reset.setForegroundPadding(m_opening.foregroundPadding());
	} else if (category == 4) {
		reset.setBlackReplacement(m_opening.blackReplacement());
		reset.setUiPrimaryColor(m_opening.uiPrimaryColor());
		reset.setUiSecondaryColor(m_opening.uiSecondaryColor());
		reset.setUiAccentColor(m_opening.uiAccentColor());
	} else if (category == 5) {
		reset.setUiTextColor(m_opening.uiTextColor());
		reset.setTextColor(m_opening.textColor());
		reset.setMisspelledColor(m_opening.misspelledColor());
	} else if (category == 6) {
		reset.setUiFontFamily(m_opening.uiFontFamily());
		reset.setUiFontSize(m_opening.uiFontSize());
	} else if (category == 7) {
		reset.setTextFont(m_opening.textFont());
		reset.setDocumentFontPixels(m_opening.documentFontPixels());
	} else if (category == 8) {
		reset.setLineSpacing(m_opening.lineSpacing());
		reset.setTabWidth(m_opening.tabWidth());
		reset.setSpacingAboveParagraph(m_opening.spacingAboveParagraph());
		reset.setSpacingBelowParagraph(m_opening.spacingBelowParagraph());
		reset.setIndentFirstLine(m_opening.indentFirstLine());
	} else {
		reset.setPalette(m_opening.palette());
	}
	recordDraft(reset);
}

void ThemeDialog::resetAll()
{
	recordDraft(m_opening);
}

void ThemeDialog::restoreBaseline()
{
	Theme baseline = Theme::neutralDraft();
	Theme reset = m_draft;
	reset.setPalette(baseline.palette());
	recordDraft(reset);
}

void ThemeDialog::derivePalette()
{
	QImage image = PresentationSurface::renderWallpaper(m_draft, QSize(256, 224));
	if (image.isNull()) return;
	std::array<int, 32768> histogram{};
	image = image.convertToFormat(QImage::Format_RGB32);
	for (int y = 0; y < image.height(); ++y) {
		const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (int x = 0; x < image.width(); ++x) {
			const SnesColor color = SnesStyle::toSnesColor(QColor(line[x]));
			++histogram[color.value()];
		}
	}
	std::vector<std::pair<int, int>> colors;
	for (int value = 1; value < 32768; ++value) {
		if (histogram[value]) colors.emplace_back(histogram[value], value);
	}
	std::sort(colors.begin(), colors.end(), [](const auto& a, const auto& b) {
		return a.first != b.first ? a.first > b.first : a.second < b.second;
	});
	if (colors.empty()) return;
	auto qcolor = [](int value) { return SnesStyle::toQColor(SnesColor::fromBgr555(value)); };
	QColor dominant = qcolor(colors.front().second);
	QColor darkest = dominant;
	QColor brightest = dominant;
	for (const auto& entry : colors) {
		const QColor color = qcolor(entry.second);
		if (qGray(color.rgb()) < qGray(darkest.rgb())) darkest = color;
		if (qGray(color.rgb()) > qGray(brightest.rgb())) brightest = color;
	}
	QColor accent = colors.size() > 1 ? qcolor(colors[1].second) : m_draft.uiAccentColor();
	ThemePalette palette = m_draft.palette();
	palette.deep = darkest;
	palette.primary = dominant;
	palette.secondary = brightest;
	palette.accent = accent;
	Theme derived = m_draft;
	derived.setPalette(palette);
	recordDraft(derived);
}

void ThemeDialog::remapPalette()
{
	Theme mapped = m_draft;
	mapped.toneMap();
	recordDraft(mapped);
}

void ThemeDialog::accept()
{
	if (m_finished || !m_save->isEnabled()) return;
	m_finished = true;
	m_draft.setLoadColor(m_draft.calculateLoadColor().result());
	// A value draft deliberately does not inherit SettingsFile's incidental
	// dirty bit. Commit is an explicit transaction boundary and must write once
	// even when an untouched neutral factory is saved as-is.
	m_draft.saveChanges(true);
	savePreview();
	QDialog::accept();
	Q_EMIT themeSaved(m_draft);
}

void ThemeDialog::reject()
{
	if (m_finished) {
		QDialog::reject();
		return;
	}
	m_finished = true;
	QDialog::reject();
	Q_EMIT previewCanceled();
}

void ThemeDialog::savePreview()
{
	Theme::removeIcon(m_draft.id(), m_draft.isDefault());
	QRect foreground;
	QImage background = m_draft.render(QSize(1920, 1080), foreground, 0, devicePixelRatioF());
	QImage icon;
	m_draft.renderText(background, foreground, devicePixelRatioF(), nullptr, &icon);
	icon.save(Theme::iconPath(m_draft.id(), m_draft.isDefault(), devicePixelRatioF()), "", 0);
}

void ThemeDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	if (!m_preview_started) {
		m_preview_started = true;
		Q_EMIT previewChanged(m_draft);
	}
}

void ThemeDialog::hideEvent(QHideEvent* event)
{
	QSettings().setValue("ThemeDialog/Size", size());
	QDialog::hideEvent(event);
}
