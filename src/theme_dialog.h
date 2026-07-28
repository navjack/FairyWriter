/*
	SPDX-FileCopyrightText: 2009 Graeme Gott <graeme@gottcode.org>
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FOCUSWRITER_THEME_DIALOG_H
#define FOCUSWRITER_THEME_DIALOG_H

#include "theme.h"

#include <QDialog>
#include <QVector>

class ColorButton;
class FocalPointControl;
class ImageButton;
class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

class ThemeDialog : public QDialog
{
	Q_OBJECT

public:
	explicit ThemeDialog(const Theme& theme, bool new_theme = false, QWidget* parent = nullptr);
	const Theme& draft() const { return m_draft; }

public Q_SLOTS:
	void accept() override;
	void reject() override;

Q_SIGNALS:
	void previewChanged(const Theme& theme);
	void themeSaved(const Theme& theme);
	void previewCanceled();

protected:
	void hideEvent(QHideEvent* event) override;
	void showEvent(QShowEvent* event) override;

private Q_SLOTS:
	void controlsChanged();
	void imageChanged();
	void undo();
	void redo();
	void resetPage();
	void resetAll();
	void restoreBaseline();
	void derivePalette();
	void remapPalette();

private:
	void buildUi();
	void syncControls(const Theme& theme);
	Theme themeFromControls() const;
	void recordDraft(const Theme& theme);
	void pushHistory(const Theme& theme);
	void publishDraft(const Theme& theme, bool record);
	void updateHistoryButtons();
	void updateConditionalControls();
	void updateSaveButton();
	void savePreview();

private:
	Theme m_opening;
	Theme m_draft;
	bool m_syncing;
	bool m_finished;
	bool m_preview_started;
	bool m_continuous_edit;
	Theme m_continuous_origin;
	QVector<Theme> m_history;
	int m_history_index;

	QComboBox* m_category;
	QStackedWidget* m_pages;
	QLineEdit* m_name;
	QComboBox* m_wallpaper_source;
	ImageButton* m_background_image;
	QPushButton* m_clear_image;
	QComboBox* m_wallpaper_fit;
	FocalPointControl* m_focal_point;
	QSpinBox* m_focal_x;
	QSpinBox* m_focal_y;
	ColorButton* m_background_color;
	QComboBox* m_shell_style;
	ColorButton* m_paper_color;
	QSpinBox* m_paper_opacity;
	QSpinBox* m_paper_padding;
	ColorButton* m_deep_color;
	ColorButton* m_primary_color;
	ColorButton* m_secondary_color;
	ColorButton* m_accent_color;
	ColorButton* m_ui_text_color;
	ColorButton* m_document_text_color;
	ColorButton* m_misspelled_color;
	QFontComboBox* m_ui_font;
	QSpinBox* m_ui_font_size;
	QFontComboBox* m_document_font;
	QSpinBox* m_document_font_pixels;
	QComboBox* m_line_spacing_type;
	QSpinBox* m_line_spacing;
	QSpinBox* m_tab_width;
	QSpinBox* m_paragraph_above;
	QSpinBox* m_paragraph_below;
	QCheckBox* m_indent_first_line;
	QPushButton* m_undo;
	QPushButton* m_redo;
	QPushButton* m_reset_page;
	QPushButton* m_reset_all;
	QPushButton* m_save;
};

#endif // FOCUSWRITER_THEME_DIALOG_H
