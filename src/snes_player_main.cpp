/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <QApplication>
#include "cartridge_image.h"
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLibrary>
#include <QPainter>
#include <QQueue>
#include <QTimer>
#include <QWidget>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Libretro
{

constexpr unsigned EnvironmentGetCanDupe = 3;
constexpr unsigned EnvironmentGetSystemDirectory = 9;
constexpr unsigned EnvironmentSetPixelFormat = 10;
constexpr unsigned EnvironmentGetSaveDirectory = 31;
constexpr unsigned EnvironmentGetVariableUpdate = 17;
constexpr unsigned DeviceJoypad = 1;
constexpr unsigned MemorySaveRam = 0;

enum PixelFormat { ZeroRgb1555 = 0, Xrgb8888 = 1, Rgb565 = 2 };

struct GameInfo { const char* path; const void* data; std::size_t size; const char* meta; };
struct Geometry { unsigned base_width, base_height, max_width, max_height; float aspect_ratio; };
struct Timing { double fps, sample_rate; };
struct AvInfo { Geometry geometry; Timing timing; };

using Environment = bool (*)(unsigned, void*);
using Video = void (*)(const void*, unsigned, unsigned, std::size_t);
using Audio = void (*)(std::int16_t, std::int16_t);
using AudioBatch = std::size_t (*)(const std::int16_t*, std::size_t);
using InputPoll = void (*)();
using InputState = std::int16_t (*)(unsigned, unsigned, unsigned, unsigned);

}

class SnesPlayer;
static SnesPlayer* g_player = nullptr;

class SnesPlayer final : public QWidget
{
public:
	 explicit SnesPlayer(const QString& core_path)
		: m_core(core_path)
		, m_rom_path(QStringLiteral("FairyWriter"))
	{
		setWindowTitle(QStringLiteral("FairyWriter — SNES Cartridge"));
		setMinimumSize(512, 448);
		resize(768, 672);
		setFocusPolicy(Qt::StrongFocus);
		setAttribute(Qt::WA_OpaquePaintEvent);
		g_player = this;

		if (!m_core.load()) {
			qFatal("Unable to load bsnes core: %s", qPrintable(m_core.errorString()));
		}
		loadApi();
		m_set_environment(environmentCallback);
		m_init();
		m_set_video(videoCallback);
		m_set_audio(audioCallback);
		m_set_audio_batch(audioBatchCallback);
		m_set_input_poll(inputPollCallback);
		m_set_input_state(inputStateCallback);

		m_rom = QByteArray(reinterpret_cast<const char*>(FairyWriter::cartridgeImage()),
			static_cast<qsizetype>(FairyWriter::cartridgeImageSize()));
		m_rom_path_utf8 = QByteArrayLiteral("FairyWriter.sfc");
		Libretro::GameInfo game{m_rom_path_utf8.constData(), m_rom.constData(),
			static_cast<std::size_t>(m_rom.size()), nullptr};
		if (!m_load_game(&game)) {
			qFatal("bsnes rejected FairyWriter cartridge");
		}
		if (m_set_controller) {
			m_set_controller(0, Libretro::DeviceJoypad);
		}
		loadSaveRam();

		Libretro::AvInfo av{};
		m_get_av_info(&av);
		const double fps = av.timing.fps > 1.0 ? av.timing.fps : 60.098;
		m_timer.setTimerType(Qt::PreciseTimer);
		m_timer.setInterval(qMax(1, qRound(1000.0 / fps)));
		connect(&m_timer, &QTimer::timeout, this, [this] {
			pumpBringupMailbox();
			m_run();
		});
		m_timer.start();
	}

	~SnesPlayer() override
	{
		m_timer.stop();
		saveSaveRam();
		if (m_unload_game) m_unload_game();
		if (m_deinit) m_deinit();
		g_player = nullptr;
		m_core.unload();
	}

	bool environment(unsigned command, void* data)
	{
		switch (command) {
		case Libretro::EnvironmentGetCanDupe:
			*static_cast<bool*>(data) = true;
			return true;
		case Libretro::EnvironmentSetPixelFormat:
			m_pixel_format = *static_cast<const unsigned*>(data);
			return m_pixel_format <= Libretro::Rgb565;
		case Libretro::EnvironmentGetSystemDirectory:
		case Libretro::EnvironmentGetSaveDirectory:
			*static_cast<const char**>(data) = ".";
			return true;
		case Libretro::EnvironmentGetVariableUpdate:
			*static_cast<bool*>(data) = false;
			return true;
		default:
			return false;
		}
	}

	void video(const void* source, unsigned width, unsigned height, std::size_t pitch)
	{
		if (!source || width == 0 || height == 0) return;
		QImage frame(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGB32);
		for (unsigned y = 0; y < height; ++y) {
			const auto* row = static_cast<const std::uint8_t*>(source) + y * pitch;
			auto* target = reinterpret_cast<QRgb*>(frame.scanLine(static_cast<int>(y)));
			for (unsigned x = 0; x < width; ++x) {
				if (m_pixel_format == Libretro::Xrgb8888) {
					std::uint32_t value;
					std::memcpy(&value, row + x * 4, 4);
					target[x] = 0xff000000u | value;
				} else {
					std::uint16_t value;
					std::memcpy(&value, row + x * 2, 2);
					if (m_pixel_format == Libretro::Rgb565) {
						target[x] = qRgb(((value >> 11) & 31) * 255 / 31,
							((value >> 5) & 63) * 255 / 63, (value & 31) * 255 / 31);
					} else {
						target[x] = qRgb(((value >> 10) & 31) * 255 / 31,
							((value >> 5) & 31) * 255 / 31, (value & 31) * 255 / 31);
					}
				}
			}
		}
		m_frame = frame;
		update();
	}

	std::int16_t inputState(unsigned port, unsigned device, unsigned, unsigned id) const
	{
		return port == 0 && device == Libretro::DeviceJoypad && id < m_buttons.size() && m_buttons[id] ? 1 : 0;
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.fillRect(rect(), Qt::black);
		if (m_frame.isNull()) return;
		const int scale = qMax(1, qMin(width() / m_frame.width(), height() / m_frame.height()));
		const QSize size = m_frame.size() * scale;
		const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
		painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
		painter.drawImage(target, m_frame);
	}

	void keyPressEvent(QKeyEvent* event) override
	{
		setButton(event->key(), true);
		if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
			const QByteArray text = event->text().toLatin1();
			for (const char value : text) {
				const auto byte = static_cast<unsigned char>(value);
				if (byte >= 32 && byte <= 126 && m_text.size() < 255) m_text.enqueue(byte);
			}
		}
		event->accept();
	}

	void keyReleaseEvent(QKeyEvent* event) override
	{
		if (!event->isAutoRepeat()) setButton(event->key(), false);
		event->accept();
	}

private:
	template<typename T> T resolve(const char* name)
	{
		const QFunctionPointer pointer = m_core.resolve(name);
		if (!pointer) qFatal("bsnes core is missing %s", name);
		return reinterpret_cast<T>(pointer);
	}

	void loadApi()
	{
		m_init = resolve<void (*)()>("retro_init");
		m_deinit = resolve<void (*)()>("retro_deinit");
		m_set_environment = resolve<void (*)(Libretro::Environment)>("retro_set_environment");
		m_set_video = resolve<void (*)(Libretro::Video)>("retro_set_video_refresh");
		m_set_audio = resolve<void (*)(Libretro::Audio)>("retro_set_audio_sample");
		m_set_audio_batch = resolve<void (*)(Libretro::AudioBatch)>("retro_set_audio_sample_batch");
		m_set_input_poll = resolve<void (*)(Libretro::InputPoll)>("retro_set_input_poll");
		m_set_input_state = resolve<void (*)(Libretro::InputState)>("retro_set_input_state");
		m_load_game = resolve<bool (*)(const Libretro::GameInfo*)>("retro_load_game");
		m_unload_game = resolve<void (*)()>("retro_unload_game");
		m_run = resolve<void (*)()>("retro_run");
		m_get_av_info = resolve<void (*)(Libretro::AvInfo*)>("retro_get_system_av_info");
		m_get_memory_data = resolve<void* (*)(unsigned)>("retro_get_memory_data");
		m_get_memory_size = resolve<std::size_t (*)(unsigned)>("retro_get_memory_size");
		m_set_controller = resolve<void (*)(unsigned, unsigned)>("retro_set_controller_port_device");
	}

	void setButton(int key, bool down)
	{
		int id = -1;
		switch (key) {
		case Qt::Key_Z: id = 0; break; case Qt::Key_A: id = 1; break;
		case Qt::Key_Shift: id = 2; break; case Qt::Key_Return: case Qt::Key_Enter: id = 3; break;
		case Qt::Key_Up: id = 4; break; case Qt::Key_Down: id = 5; break;
		case Qt::Key_Left: id = 6; break; case Qt::Key_Right: id = 7; break;
		case Qt::Key_X: id = 8; break; case Qt::Key_S: id = 9; break;
		case Qt::Key_C: id = 10; break; case Qt::Key_V: id = 11; break;
		default: break;
		}
		if (id >= 0) m_buttons[static_cast<std::size_t>(id)] = down;
	}

	// Bring-up oracle only. Production text input is an emulated controller-port
	// 2 XBAND keyboard; see docs/SNES_FRONTEND.md.
	void pumpBringupMailbox()
	{
		auto* ram = static_cast<std::uint8_t*>(m_get_memory_data(Libretro::MemorySaveRam));
		const std::size_t size = m_get_memory_size(Libretro::MemorySaveRam);
		if (!ram || size < 3 || m_text.isEmpty() || ram[1] != ram[2]) return;
		ram[0] = m_text.dequeue();
		ram[1] = static_cast<std::uint8_t>(ram[1] + 1);
	}

	QString savePath() const { return m_rom_path + QStringLiteral(".srm"); }
	void loadSaveRam()
	{
		auto* ram = static_cast<char*>(m_get_memory_data(Libretro::MemorySaveRam));
		const auto size = static_cast<qint64>(m_get_memory_size(Libretro::MemorySaveRam));
		QFile file(savePath());
		if (ram && size > 0 && file.open(QFile::ReadOnly)) {
			const QByteArray saved = file.read(size);
			std::memcpy(ram, saved.constData(), static_cast<std::size_t>(qMin(size, saved.size())));
		}
	}

	void saveSaveRam()
	{
		const auto* ram = static_cast<const char*>(m_get_memory_data(Libretro::MemorySaveRam));
		const auto size = static_cast<qint64>(m_get_memory_size(Libretro::MemorySaveRam));
		QFile file(savePath());
		if (ram && size > 0 && file.open(QFile::WriteOnly)) file.write(ram, size);
	}

	static bool environmentCallback(unsigned command, void* data) { return g_player && g_player->environment(command, data); }
	static void videoCallback(const void* data, unsigned w, unsigned h, std::size_t pitch) { if (g_player) g_player->video(data, w, h, pitch); }
	static void audioCallback(std::int16_t, std::int16_t) {}
	static std::size_t audioBatchCallback(const std::int16_t*, std::size_t frames) { return frames; }
	static void inputPollCallback() {}
	static std::int16_t inputStateCallback(unsigned p, unsigned d, unsigned i, unsigned id) { return g_player ? g_player->inputState(p,d,i,id) : 0; }

	QLibrary m_core;
	QString m_rom_path;
	QByteArray m_rom_path_utf8;
	QByteArray m_rom;
	QImage m_frame;
	QTimer m_timer;
	QQueue<std::uint8_t> m_text;
	std::array<bool, 16> m_buttons{};
	unsigned m_pixel_format = Libretro::ZeroRgb1555;

	void (*m_init)() = nullptr; void (*m_deinit)() = nullptr;
	void (*m_set_environment)(Libretro::Environment) = nullptr;
	void (*m_set_video)(Libretro::Video) = nullptr; void (*m_set_audio)(Libretro::Audio) = nullptr;
	void (*m_set_audio_batch)(Libretro::AudioBatch) = nullptr; void (*m_set_input_poll)(Libretro::InputPoll) = nullptr;
	void (*m_set_input_state)(Libretro::InputState) = nullptr; bool (*m_load_game)(const Libretro::GameInfo*) = nullptr;
	void (*m_unload_game)() = nullptr; void (*m_run)() = nullptr; void (*m_get_av_info)(Libretro::AvInfo*) = nullptr;
	void* (*m_get_memory_data)(unsigned) = nullptr; std::size_t (*m_get_memory_size)(unsigned) = nullptr;
	void (*m_set_controller)(unsigned, unsigned) = nullptr;
};

int main(int argc, char** argv)
{
	QApplication application(argc, argv);
	if (argc != 2) {
		qCritical("usage: FairyWriter-SNES CORE.dylib");
		return 2;
	}
	SnesPlayer player(QString::fromLocal8Bit(argv[1]));
	player.show();
	return application.exec();
}
