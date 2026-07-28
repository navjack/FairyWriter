/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FAIRYWRITER_FOCUS_PRESENTATION_H
#define FAIRYWRITER_FOCUS_PRESENTATION_H

#include <QObject>

class QTimer;

class FocusPresentation final : public QObject
{
	Q_OBJECT

public:
	static constexpr int HiddenStep = 0;
	static constexpr int VisibleStep = 4;
	static constexpr int HideDelayMilliseconds = 1500;
	static constexpr int HideFadeMilliseconds = 240;
	static constexpr int RevealFadeMilliseconds = 120;
	static constexpr int EdgeZoneVirtualPixels = 8;

	explicit FocusPresentation(QObject* parent = nullptr);

	bool autoHideEnabled() const { return m_auto_hide_enabled; }
	bool pinned() const { return m_pinned; }
	bool interactionActive() const { return m_interaction_active; }
	bool pointerInEdgeZone() const { return m_pointer_in_edge_zone; }
	bool hideDelayActive() const;
	bool animationActive() const;
	int chromeStep() const { return m_chrome_step; }

	static bool ditherCovered(int x, int y, int chrome_step);

public Q_SLOTS:
	void editorTyped();
	void pointerMoved(int y, int height, int virtual_pixel_pitch);
	void setAutoHideEnabled(bool enabled);
	void setPinned(bool pinned);
	void setInteractionActive(bool active);
	void reveal();
	void hideImmediately();

Q_SIGNALS:
	void chromeStepChanged(int step);

private Q_SLOTS:
	void beginHide();
	void advanceFade();

private:
	void startFade(int target, int total_milliseconds);
	void restartHideDelay();
	void setChromeStep(int step);

private:
	QTimer* m_hide_delay;
	QTimer* m_fade_timer;
	bool m_auto_hide_enabled;
	bool m_pinned;
	bool m_interaction_active;
	bool m_pointer_in_edge_zone;
	bool m_has_typed;
	int m_chrome_step;
	int m_fade_target;
};

#endif // FAIRYWRITER_FOCUS_PRESENTATION_H
