/*
	SPDX-FileCopyrightText: 2013 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "daily_progress_dialog.h"

#include "daily_progress.h"
#include "preferences.h"

#include <QApplication>
#include <QDate>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QProgressBar>
#include <QScrollBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

class DailyProgressDialog::Delegate : public QStyledItemDelegate
{
public:
	explicit Delegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

	void changeEvent(QEvent* event)
	{
		if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
			m_pixmap = QPixmap();
		}
	}

	void paint(QPainter* painter, const QStyleOptionViewItem& option,
		const QModelIndex& index) const override
	{
		QStyleOptionViewItem opt = option;
		initStyleOption(&opt, index);
		if (opt.text.isEmpty()) return;

		if (index.column() > 0 && index.column() < 8) {
			opt.rect = opt.rect.adjusted(2, 2, -2, -2);
			const int progress = qBound(0, index.data(Qt::UserRole).toInt(), 100);
			const QColor base = opt.palette.color(QPalette::Active, QPalette::AlternateBase);
			const QColor highlight = opt.palette.color(QPalette::Active, QPalette::Highlight);
			if (progress == 100) {
				opt.backgroundBrush = highlight;
				opt.font.setBold(true);
			} else {
				const qreal amount = progress == 0 ? 0.0 : 0.18 + progress * 0.0065;
				opt.backgroundBrush = QColor(
					std::lround(base.red() * (1.0 - amount) + highlight.red() * amount),
					std::lround(base.green() * (1.0 - amount) + highlight.green() * amount),
					std::lround(base.blue() * (1.0 - amount) + highlight.blue() * amount));
			}
			if (progress >= 50) {
				opt.palette.setColor(QPalette::Text,
					opt.palette.color(QPalette::Active, QPalette::HighlightedText));
			}
		} else {
			opt.backgroundBrush = opt.palette.color(QPalette::Active, QPalette::Base);
		}

		const QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
		style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
	}

private:
	mutable QPixmap m_pixmap;
};

DailyProgressDialog::DailyProgressDialog(DailyProgress* progress, QWidget* parent)
	: QDialog(parent, Qt::Widget)
	, m_progress(progress)
	, m_today_title(new QLabel(this))
	, m_today_value(new QLabel(this))
	, m_today_secondary(new QLabel(this))
	, m_today_remaining(new QLabel(this))
	, m_today_meter(new QProgressBar(this))
	, m_longest_streak(new QLabel(this))
	, m_current_streak(new QLabel(this))
	, m_display(new QTableView(this))
	, m_delegate(new Delegate(this))
{
	setObjectName(QStringLiteral("FairyWriterDailyProgress"));
	setProperty("fairywriter.panelFill", true);
	setWindowFlags(Qt::Widget);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
	setMinimumSize(0, 0);

	QLabel* today_heading = new QLabel(tr("Today"), this);
	today_heading->setObjectName(QStringLiteral("DailyProgressSectionHeading"));
	m_today_title->setObjectName(QStringLiteral("DailyProgressGoalTitle"));
	m_today_value->setObjectName(QStringLiteral("DailyProgressValue"));
	m_today_secondary->setObjectName(QStringLiteral("DailyProgressSecondary"));
	m_today_remaining->setObjectName(QStringLiteral("DailyProgressRemaining"));
	m_today_value->setWordWrap(true);
	m_today_secondary->setWordWrap(true);
	m_today_remaining->setWordWrap(true);
	m_today_meter->setRange(0, 100);
	m_today_meter->setObjectName(QStringLiteral("DailyProgressMeter"));
	m_today_meter->setTextVisible(false);
	m_today_meter->setMinimumHeight(10);

	QVBoxLayout* today_layout = new QVBoxLayout;
	today_layout->setContentsMargins(0, 0, 0, 0);
	today_layout->setSpacing(3);
	today_layout->addWidget(today_heading);
	today_layout->addWidget(m_today_title);
	today_layout->addWidget(m_today_value);
	today_layout->addWidget(m_today_secondary);
	today_layout->addWidget(m_today_meter);
	today_layout->addWidget(m_today_remaining);

	QLabel* streak_heading = new QLabel(tr("Streaks"), this);
	streak_heading->setObjectName(QStringLiteral("DailyProgressSectionHeading"));
	QFrame* streaks = new QFrame(this);
	streaks->setObjectName(QStringLiteral("DailyProgressStreaks"));
	m_current_streak->setWordWrap(true);
	m_longest_streak->setWordWrap(true);
	m_current_streak->setAlignment(Qt::AlignCenter);
	m_longest_streak->setAlignment(Qt::AlignCenter);
	QHBoxLayout* streak_layout = new QHBoxLayout(streaks);
	streak_layout->setContentsMargins(6, 4, 6, 4);
	streak_layout->setSpacing(8);
	streak_layout->addWidget(m_current_streak, 1);
	streak_layout->addWidget(m_longest_streak, 1);

	QLabel* history_heading = new QLabel(tr("History"), this);
	history_heading->setObjectName(QStringLiteral("DailyProgressSectionHeading"));
	m_display->setObjectName(QStringLiteral("DailyProgressCalendar"));
	m_display->setModel(progress);
	m_display->setItemDelegate(m_delegate);
	m_display->setShowGrid(false);
	m_display->verticalHeader()->hide();
	m_display->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_display->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	m_display->setSelectionMode(QAbstractItemView::NoSelection);
	m_display->setFocusPolicy(Qt::NoFocus);
	m_display->horizontalHeader()->setSectionsClickable(false);
	m_display->horizontalHeader()->setSectionsMovable(false);
	m_display->horizontalHeader()->setMinimumSectionSize(0);
	m_display->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_display->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Stretch);
	for (int column = 1; column < 8; ++column) {
		m_display->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Stretch);
	}

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);
	layout->addLayout(today_layout);
	layout->addWidget(streak_heading);
	layout->addWidget(streaks);
	layout->addWidget(history_heading);
	layout->addWidget(m_display, 1);

	connect(progress, &DailyProgress::modelReset, this, &DailyProgressDialog::modelReset);
	connect(progress, &DailyProgress::progressChanged, this, &DailyProgressDialog::progressChanged);
	connect(progress, &DailyProgress::streaksChanged, this, &DailyProgressDialog::streaksChanged);
	loadPreferences();
	modelReset();
	updateToday();
	streaksChanged();
}

void DailyProgressDialog::loadPreferences()
{
	const bool visible = Preferences::instance().goalStreaks();
	m_longest_streak->parentWidget()->setVisible(visible);
	updateToday();
}

void DailyProgressDialog::changeEvent(QEvent* event)
{
	m_delegate->changeEvent(event);
	QDialog::changeEvent(event);
}

void DailyProgressDialog::hideEvent(QHideEvent* event)
{
	Q_EMIT visibleChanged(false);
	QDialog::hideEvent(event);
}

void DailyProgressDialog::showEvent(QShowEvent* event)
{
	Q_EMIT visibleChanged(true);
	m_display->scrollToBottom();
	QDialog::showEvent(event);
}

void DailyProgressDialog::modelReset()
{
	const int width = std::max(18, fontMetrics().height() + 8);
	for (int row = 0; row < m_progress->rowCount(); ++row) {
		m_display->setRowHeight(row, width);
	}
	m_display->scrollToBottom();
}

void DailyProgressDialog::progressChanged()
{
	updateToday();
}

void DailyProgressDialog::updateToday()
{
	const int percent = qBound(0, m_progress->percentComplete(), 100);
	const int words = m_progress->currentWords();
	const int minutes = m_progress->currentMilliseconds() / 60000;
	const int goal = m_progress->goalValue();
	const bool word_goal = m_progress->goalType() == 2;
	const bool time_goal = m_progress->goalType() == 1;

	if (word_goal) {
		m_today_title->setText(tr("Daily writing goal"));
		m_today_value->setText(tr("%L1 of %L2 words").arg(words).arg(goal));
		m_today_remaining->setText(tr("%L1 words remaining").arg(std::max(0, goal - words)));
	} else if (time_goal) {
		const int goal_minutes = goal / 60000;
		m_today_title->setText(tr("Daily writing goal"));
		m_today_value->setText(tr("%L1 of %L2 minutes").arg(minutes).arg(goal_minutes));
		m_today_remaining->setText(tr("%L1 minutes remaining").arg(std::max(0, goal_minutes - minutes)));
	} else {
		m_today_title->setText(tr("Writing today"));
		m_today_value->setText(tr("%L1 words · %L2 minutes").arg(words).arg(minutes));
		m_today_remaining->setText(tr("Set a daily goal in Preferences when you want progress targets."));
	}
	m_today_secondary->setText(tr("%L1 words · %L2 minutes · %L3% complete")
		.arg(words).arg(minutes).arg(percent));
	m_today_meter->setValue(percent);
}

void DailyProgressDialog::streaksChanged()
{
	QDate start, end;
	m_progress->findCurrentStreak(start, end);
	m_current_streak->setText(createStreakText(tr("Current streak"), start, end));
	m_current_streak->setEnabled(end == QDate::currentDate());
	m_progress->findLongestStreak(start, end);
	m_longest_streak->setText(createStreakText(tr("Longest streak"), start, end));
	m_longest_streak->setEnabled(end.isValid());
}

QString DailyProgressDialog::createStreakText(const QString& title, const QDate& start, const QDate& end) const
{
	const int length = start.isValid() ? start.daysTo(end) + 1 : 0;
	const QLocale locale;
	const QString dates = length ? tr("%1 – %2").arg(locale.toString(start, QLocale::ShortFormat),
		locale.toString(end, QLocale::ShortFormat)) : tr("No completed days yet");
	return tr("<b>%1</b><br><big>%2</big><br><small>%3</small>")
		.arg(title, tr("%n day(s)", "", length), dates);
}
