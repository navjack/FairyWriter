/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "focus_presentation.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

class FocusPresentationTests : public QObject
{
	Q_OBJECT

private Q_SLOTS:
	void defaultsToEnabledAndVisible();
	void typingHidesAfterDelayAndFourSteps();
	void eitherEdgeRevealsBothChromeRegions();
	void pinAndDisableKeepChromeVisible();
	void ditherCoverageUsesFourOrderedLevels();
};

void FocusPresentationTests::defaultsToEnabledAndVisible()
{
	FocusPresentation focus;
	QVERIFY(focus.autoHideEnabled());
	QVERIFY(!focus.pinned());
	QCOMPARE(focus.chromeStep(), FocusPresentation::VisibleStep);
	QVERIFY(!focus.animationActive());
}

void FocusPresentationTests::typingHidesAfterDelayAndFourSteps()
{
	FocusPresentation focus;
	QSignalSpy steps(&focus, &FocusPresentation::chromeStepChanged);
	focus.pointerMoved(100, 720, 2);
	focus.editorTyped();
	QVERIFY(focus.hideDelayActive());
	QTest::qWait(FocusPresentation::HideDelayMilliseconds - 100);
	QCOMPARE(focus.chromeStep(), FocusPresentation::VisibleStep);
	QTRY_COMPARE_WITH_TIMEOUT(focus.chromeStep(), FocusPresentation::HiddenStep,
		100 + FocusPresentation::HideFadeMilliseconds + 500);
	QCOMPARE(steps.count(), 4);
	QVERIFY(!focus.animationActive());
}

void FocusPresentationTests::eitherEdgeRevealsBothChromeRegions()
{
	FocusPresentation focus;
	focus.hideImmediately();
	QSignalSpy steps(&focus, &FocusPresentation::chromeStepChanged);
	focus.pointerMoved(0, 720, 3);
	QTest::qWait(FocusPresentation::RevealFadeMilliseconds + 60);
	QCOMPARE(focus.chromeStep(), FocusPresentation::VisibleStep);
	QCOMPARE(steps.count(), 4);
	QVERIFY(!focus.animationActive());

	focus.hideImmediately();
	steps.clear();
	focus.pointerMoved(719, 720, 3);
	QTest::qWait(FocusPresentation::RevealFadeMilliseconds + 60);
	QCOMPARE(focus.chromeStep(), FocusPresentation::VisibleStep);
	QCOMPARE(steps.count(), 4);
}

void FocusPresentationTests::pinAndDisableKeepChromeVisible()
{
	FocusPresentation focus;
	focus.hideImmediately();
	focus.setPinned(true);
	QTest::qWait(FocusPresentation::RevealFadeMilliseconds + 60);
	QCOMPARE(focus.chromeStep(), FocusPresentation::VisibleStep);
	focus.editorTyped();
	QVERIFY(!focus.hideDelayActive());

	focus.setPinned(false);
	focus.setAutoHideEnabled(false);
	focus.editorTyped();
	QVERIFY(!focus.hideDelayActive());
	QCOMPARE(focus.chromeStep(), FocusPresentation::VisibleStep);
}

void FocusPresentationTests::ditherCoverageUsesFourOrderedLevels()
{
	for (int step = FocusPresentation::HiddenStep; step <= FocusPresentation::VisibleStep; ++step) {
		int covered = 0;
		for (int y = 0; y < 4; ++y) {
			for (int x = 0; x < 4; ++x) {
				covered += FocusPresentation::ditherCovered(x, y, step);
			}
		}
		QCOMPARE(covered, (FocusPresentation::VisibleStep - step) * 4);
	}
}

QTEST_MAIN(FocusPresentationTests)

#include "focus_presentation_tests.moc"
