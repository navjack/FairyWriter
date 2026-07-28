/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "focus_presentation.h"

#include <QTimer>

#include <algorithm>

namespace
{

constexpr int Bayer4x4[4][4] = {
	{ 0, 8, 2, 10 },
	{ 12, 4, 14, 6 },
	{ 3, 11, 1, 9 },
	{ 15, 7, 13, 5 }
};

}

FocusPresentation::FocusPresentation(QObject* parent)
	: QObject(parent)
	, m_auto_hide_enabled(true)
	, m_pinned(false)
	, m_interaction_active(false)
	, m_pointer_in_edge_zone(false)
	, m_has_typed(false)
	, m_chrome_step(VisibleStep)
	, m_fade_target(VisibleStep)
{
	m_hide_delay = new QTimer(this);
	m_hide_delay->setSingleShot(true);
	m_hide_delay->setInterval(HideDelayMilliseconds);
	connect(m_hide_delay, &QTimer::timeout, this, &FocusPresentation::beginHide);

	m_fade_timer = new QTimer(this);
	connect(m_fade_timer, &QTimer::timeout, this, &FocusPresentation::advanceFade);
}

bool FocusPresentation::hideDelayActive() const
{
	return m_hide_delay->isActive();
}

bool FocusPresentation::animationActive() const
{
	return m_fade_timer->isActive();
}

bool FocusPresentation::ditherCovered(int x, int y, int chrome_step)
{
	const int clamped_step = std::clamp(chrome_step, HiddenStep, VisibleStep);
	const int coverage = (VisibleStep - clamped_step) * 4;
	return Bayer4x4[y & 3][x & 3] < coverage;
}

void FocusPresentation::editorTyped()
{
	m_has_typed = true;
	restartHideDelay();
}

void FocusPresentation::pointerMoved(int y, int height, int virtual_pixel_pitch)
{
	const int edge = EdgeZoneVirtualPixels * std::max(1, virtual_pixel_pitch);
	const bool in_edge = y >= 0 && y < height && (y < edge || y >= (height - edge));
	if (in_edge) {
		m_pointer_in_edge_zone = true;
		m_hide_delay->stop();
		reveal();
	} else if (m_pointer_in_edge_zone) {
		m_pointer_in_edge_zone = false;
		restartHideDelay();
	}
}

void FocusPresentation::setAutoHideEnabled(bool enabled)
{
	if (m_auto_hide_enabled == enabled) {
		return;
	}
	m_auto_hide_enabled = enabled;
	if (!enabled) {
		m_hide_delay->stop();
		reveal();
	} else {
		restartHideDelay();
	}
}

void FocusPresentation::setPinned(bool pinned)
{
	if (m_pinned == pinned) {
		return;
	}
	m_pinned = pinned;
	if (pinned) {
		m_hide_delay->stop();
		reveal();
	} else {
		restartHideDelay();
	}
}

void FocusPresentation::setInteractionActive(bool active)
{
	if (m_interaction_active == active) {
		return;
	}
	m_interaction_active = active;
	if (active) {
		m_hide_delay->stop();
		reveal();
	} else {
		restartHideDelay();
	}
}

void FocusPresentation::reveal()
{
	m_hide_delay->stop();
	startFade(VisibleStep, RevealFadeMilliseconds);
}

void FocusPresentation::hideImmediately()
{
	m_hide_delay->stop();
	m_fade_timer->stop();
	setChromeStep(HiddenStep);
}

void FocusPresentation::beginHide()
{
	if (!m_auto_hide_enabled || m_pinned || m_interaction_active || m_pointer_in_edge_zone) {
		return;
	}
	startFade(HiddenStep, HideFadeMilliseconds);
}

void FocusPresentation::advanceFade()
{
	if (m_chrome_step == m_fade_target) {
		m_fade_timer->stop();
		return;
	}
	setChromeStep(m_chrome_step + ((m_fade_target > m_chrome_step) ? 1 : -1));
	if (m_chrome_step == m_fade_target) {
		m_fade_timer->stop();
	}
}

void FocusPresentation::startFade(int target, int total_milliseconds)
{
	target = std::clamp(target, HiddenStep, VisibleStep);
	if (target == m_chrome_step) {
		m_fade_timer->stop();
		return;
	}
	m_fade_target = target;
	const int steps = std::abs(m_fade_target - m_chrome_step);
	m_fade_timer->setInterval(std::max(1, total_milliseconds / steps));
	m_fade_timer->start();
}

void FocusPresentation::restartHideDelay()
{
	if (m_has_typed && m_auto_hide_enabled && !m_pinned && !m_interaction_active && !m_pointer_in_edge_zone) {
		m_hide_delay->start();
	}
}

void FocusPresentation::setChromeStep(int step)
{
	step = std::clamp(step, HiddenStep, VisibleStep);
	if (m_chrome_step == step) {
		return;
	}
	m_chrome_step = step;
	Q_EMIT chromeStepChanged(step);
}
