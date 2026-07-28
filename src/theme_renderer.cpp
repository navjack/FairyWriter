/*
	SPDX-FileCopyrightText: 2014 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "theme_renderer.h"

#include <algorithm>

//-----------------------------------------------------------------------------

ThemeRenderer::ThemeRenderer(QObject* parent)
	: QThread(parent)
{
}

//-----------------------------------------------------------------------------

void ThemeRenderer::setCacheLimit(int limit)
{
	const int bounded = std::max(0, limit);
	m_cache_limit.store(bounded);
	if (!isRunning()) {
		while (m_cache.size() > bounded) {
			m_cache.removeLast();
		}
	}
}

//-----------------------------------------------------------------------------

void ThemeRenderer::create(const Theme& theme, const QSize& background, const int margin, const qreal pixelratio,
	bool panel_visible, PanelSide panel_side)
{
	// Check if already rendered
	const CacheFile file{ theme, background, QRect(), QImage(), margin, pixelratio, panel_visible, panel_side };
	if (!isRunning()) {
		const int index = m_cache.indexOf(file);
		if (index != -1) {
			m_cache.move(index, 0);
			Q_EMIT rendered(m_cache.constFirst().image, m_cache.constFirst().foreground, file.theme);
			return;
		}
	}

	// Start render thread
	m_file_mutex.lock();
	m_files.append(file);
	m_file_mutex.unlock();

	start();
}

//-----------------------------------------------------------------------------

void ThemeRenderer::run()
{
	m_file_mutex.lock();
	do {
		// Fetch theme to render
		CacheFile file = m_files.takeLast();
		m_files.clear();
		m_file_mutex.unlock();

		// Render theme
		file.image = file.theme.render(file.background, file.foreground, file.margin, file.pixelratio,
			file.panel_visible, file.panel_side);
		m_cache.prepend(file);
		while (m_cache.count() > m_cache_limit.load()) {
			m_cache.removeLast();
		}
		Q_EMIT rendered(file.image, file.foreground, file.theme);

		// Check if done
		m_file_mutex.lock();
	} while (!m_files.isEmpty());
	m_file_mutex.unlock();
}

//-----------------------------------------------------------------------------
