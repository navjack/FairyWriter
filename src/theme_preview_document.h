/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FAIRYWRITER_THEME_PREVIEW_DOCUMENT_H
#define FAIRYWRITER_THEME_PREVIEW_DOCUMENT_H

#include <QWidget>

class QGridLayout;
class QTextDocument;
class QTextEdit;
class Theme;

class ThemePreviewDocument final : public QWidget
{
	Q_OBJECT

public:
	explicit ThemePreviewDocument(QWidget* parent = nullptr);
	void cloneFrom(const QTextDocument* source);
	void applyTheme(const Theme& theme, int virtual_pixel_pitch);
	QTextEdit* text() const { return m_text; }

private:
	QGridLayout* m_layout;
	QTextEdit* m_text;
};

#endif // FAIRYWRITER_THEME_PREVIEW_DOCUMENT_H
