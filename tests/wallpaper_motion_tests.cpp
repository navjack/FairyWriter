/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "presentation_surface.h"
#include "theme.h"
#include "wallpaper_motion_engine.h"

#include <QSignalSpy>
#include <QTest>

class WallpaperMotionTests : public QObject
{
	Q_OBJECT

private Q_SLOTS:
	void engineStartsIdle();
	void activationRendersImmediately();
	void ticksAdvanceAndWrap();
	void sceneChangeInvalidatesInFlightFrames();
	void freezeKeepsTickForResume();
};

void WallpaperMotionTests::engineStartsIdle()
{
	WallpaperMotionEngine engine;
	QCOMPARE(engine.isActive(), false);
	QCOMPARE(engine.tick(), 0);
}

void WallpaperMotionTests::activationRendersImmediately()
{
	WallpaperMotionEngine engine;
	QSignalSpy frames(&engine, &WallpaperMotionEngine::frameReady);
	engine.setScene(Theme::neutralDraft(), QSize(320, 180), 1.0, false, PanelSide::Right);
	QCOMPARE(frames.count(), 0);

	engine.setActive(true);
	QVERIFY(engine.isActive());
	QTRY_VERIFY(frames.count() >= 1);
	const QList<QVariant> frame = frames.constFirst();
	QCOMPARE(frame.at(0).value<QImage>().size(), QSize(320, 180));
	QCOMPARE(frame.at(1).toInt(), 0);
}

void WallpaperMotionTests::ticksAdvanceAndWrap()
{
	WallpaperMotionEngine engine;
	QSignalSpy frames(&engine, &WallpaperMotionEngine::frameReady);
	engine.setScene(Theme::neutralDraft(), QSize(160, 90), 1.0, false, PanelSide::Right);
	engine.setActive(true);
	QTRY_VERIFY_WITH_TIMEOUT(engine.tick() >= 2, 2000);
	QTRY_VERIFY(frames.count() >= 2);
	// Frames always report the tick they rendered, never a stale generation.
	for (const QList<QVariant>& frame : frames) {
		QCOMPARE(frame.at(2).toULongLong(), engine.sceneGeneration());
		QVERIFY(frame.at(1).toInt() < PresentationSurface::MotionTickPeriod);
	}
}

void WallpaperMotionTests::sceneChangeInvalidatesInFlightFrames()
{
	WallpaperMotionEngine engine;
	QSignalSpy frames(&engine, &WallpaperMotionEngine::frameReady);
	engine.setScene(Theme::neutralDraft(), QSize(320, 180), 1.0, false, PanelSide::Right);
	engine.setActive(true);
	// Change the scene repeatedly while renders are in flight; every frame
	// that is delivered must carry the generation that was current when it
	// was accepted, so no stale-scene frame can ever be published.
	const quint64 opening_generation = engine.sceneGeneration();
	for (int step = 0; step < 4; ++step) {
		engine.setScene(Theme::neutralDraft(), QSize(320 + step * 16, 180 + step * 9), 1.0, false, PanelSide::Right);
	}
	QVERIFY(engine.sceneGeneration() > opening_generation);
	QTRY_VERIFY(frames.count() >= 1);
	QTRY_VERIFY_WITH_TIMEOUT([&] {
		for (const QList<QVariant>& frame : frames) {
			if (frame.at(2).toULongLong() == engine.sceneGeneration()) {
				return frame.at(0).value<QImage>().size() == QSize(368, 207);
			}
		}
		return false;
	}(), 2000);
}

void WallpaperMotionTests::freezeKeepsTickForResume()
{
	WallpaperMotionEngine engine;
	QSignalSpy frames(&engine, &WallpaperMotionEngine::frameReady);
	engine.setScene(Theme::neutralDraft(), QSize(160, 90), 1.0, false, PanelSide::Right);
	engine.setActive(true);
	QTRY_VERIFY_WITH_TIMEOUT(engine.tick() >= 1, 2000);

	engine.setActive(false);
	QCOMPARE(engine.isActive(), false);
	const int frozen_tick = engine.tick();
	QTest::qWait(WallpaperMotionEngine::TickIntervalMs * 3);
	QCOMPARE(engine.tick(), frozen_tick);

	// Resuming continues from the frozen tick instead of snapping to zero.
	engine.setActive(true);
	QTRY_VERIFY_WITH_TIMEOUT(engine.tick() > frozen_tick, 2000);
}

QTEST_MAIN(WallpaperMotionTests)

#include "wallpaper_motion_tests.moc"
