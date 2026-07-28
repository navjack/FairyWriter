/*
	SPDX-FileCopyrightText: 2008 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FOCUSWRITER_COLOR_BUTTON_H
#define FOCUSWRITER_COLOR_BUTTON_H

#include <QColor>
#include <QList>
#include <QPushButton>

#include <functional>
#include <utility>

class ColorButton : public QPushButton
{
	Q_OBJECT

public:
	explicit ColorButton(QWidget* parent = nullptr);

	QColor color() const;
	QString toString() const;
	void setCompanionColors(const QList<QColor>& colors) { m_companion_colors = colors; }
	void setColorMapper(std::function<QColor(const QColor&)> mapper) { m_color_mapper = std::move(mapper); }
	void setSwatchWidth(int width);

Q_SIGNALS:
	void changed(const QColor& color);

public Q_SLOTS:
	void setColor(const QColor& color);

private Q_SLOTS:
	void onClicked();

private:
	void updateSwatch();

	int m_swatch_width;
	QColor m_color;
	QList<QColor> m_companion_colors;
	std::function<QColor(const QColor&)> m_color_mapper;
};

inline QColor ColorButton::color() const
{
	return m_color;
}

#endif // FOCUSWRITER_COLOR_BUTTON_H
