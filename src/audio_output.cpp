/*
	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "audio_output.h"

// miniaudio is compiled here and nowhere else. Everything above device I/O is
// disabled: FairyWriter never decodes, encodes or synthesises anything on the
// host -- the only samples that exist are the ones the emulated S-DSP produced.
#define MA_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_CUSTOM
#ifdef _WIN32
// windows.h, which miniaudio includes for WASAPI, defines min and max as
// macros. Without this they capture every std::min call below and the
// expansion becomes std::(...), which MSVC reports as an illegal token after
// '::'. Nothing outside Windows sees this, so it only surfaced in CI.
#define NOMINMAX
#endif
#include "miniaudio.h"

#include <algorithm>
#include <cstring>

namespace FairyWriter {

namespace {

constexpr std::size_t Channels = 2;

// ~340 ms at 48 kHz. Large enough that a stalled host frame cannot empty it,
// small enough that a full buffer is not audible latency on a keystroke.
constexpr std::size_t RingFrames = 16384;

// Steady-state fill the pacer aims for, in video frames of audio. Three frames
// is ~50 ms: enough to ride out scheduler jitter, short enough that a blip
// still lands with the keypress.
constexpr int TargetVideoFrames = 3;

std::size_t roundUpToPowerOfTwo(std::size_t value)
{
	std::size_t result = 1;
	while (result < value) result <<= 1;
	return result;
}

} // namespace

AudioRing::AudioRing(std::size_t frames)
	: m_capacity(roundUpToPowerOfTwo(std::max<std::size_t>(frames, 2)))
	, m_samples(m_capacity * Channels, 0)
{
}

std::size_t AudioRing::available() const
{
	const std::size_t write = m_write.load(std::memory_order_acquire);
	const std::size_t read = m_read.load(std::memory_order_acquire);
	return write - read;
}

std::size_t AudioRing::write(const std::int16_t* frames, std::size_t count)
{
	if (!frames || count == 0) return 0;
	const std::size_t write = m_write.load(std::memory_order_relaxed);
	const std::size_t read = m_read.load(std::memory_order_acquire);
	const std::size_t space = m_capacity - (write - read);
	const std::size_t writable = (std::min)(count, space);
	for (std::size_t i = 0; i < writable; ++i) {
		const std::size_t slot = ((write + i) & (m_capacity - 1)) * Channels;
		m_samples[slot] = frames[i * Channels];
		m_samples[slot + 1] = frames[i * Channels + 1];
	}
	m_write.store(write + writable, std::memory_order_release);
	return writable;
}

std::size_t AudioRing::read(std::int16_t* frames, std::size_t count)
{
	if (!frames || count == 0) return 0;
	const std::size_t read = m_read.load(std::memory_order_relaxed);
	const std::size_t write = m_write.load(std::memory_order_acquire);
	const std::size_t readable = (std::min)(count, write - read);
	for (std::size_t i = 0; i < readable; ++i) {
		const std::size_t slot = ((read + i) & (m_capacity - 1)) * Channels;
		frames[i * Channels] = m_samples[slot];
		frames[i * Channels + 1] = m_samples[slot + 1];
	}
	if (readable < count) {
		std::memset(frames + readable * Channels, 0,
			(count - readable) * Channels * sizeof(std::int16_t));
	}
	m_read.store(read + readable, std::memory_order_release);
	return readable;
}

struct AudioOutput::Device {
	ma_device device{};
	bool initialized = false;
};

namespace {

void deviceCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount)
{
	(void)input;
	auto* ring = static_cast<AudioRing*>(device->pUserData);
	if (!ring) {
		std::memset(output, 0, static_cast<std::size_t>(frameCount) * Channels * sizeof(std::int16_t));
		return;
	}
	ring->read(static_cast<std::int16_t*>(output), frameCount);
}

} // namespace

AudioOutput::AudioOutput()
	: m_device(std::make_unique<Device>())
	, m_ring(std::make_unique<AudioRing>(RingFrames))
{
}

AudioOutput::~AudioOutput()
{
	stop();
}

bool AudioOutput::start()
{
	if (m_active) return true;

	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format = ma_format_s16;
	config.playback.channels = static_cast<ma_uint32>(Channels);
	// Rate 0 asks miniaudio for the device's own native rate. The S-DSP block is
	// resampled to whatever comes back, so there is no reason to force a rate
	// and make the OS resample a second time.
	config.sampleRate = 0;
	config.dataCallback = deviceCallback;
	config.pUserData = m_ring.get();

	if (ma_device_init(nullptr, &config, &m_device->device) != MA_SUCCESS) return false;
	m_device->initialized = true;
	m_sample_rate = static_cast<int>(m_device->device.sampleRate);
	if (m_sample_rate <= 0) m_sample_rate = 48000;
	// Prime the ring to the target fill before the device is allowed to pull.
	// The pacer can only trim production by +/-12.5%, which is the right
	// authority for correcting drift but nowhere near enough to build a buffer
	// up from empty: starting at zero leaves the ring permanently one late
	// callback away from an underrun. Half a video frame of latency buys a
	// cushion the pacer can then simply hold.
	const std::size_t primed =
		static_cast<std::size_t>(nominalFramesPerVideoFrame()) * TargetVideoFrames;
	const std::vector<std::int16_t> silence(primed * Channels, 0);
	m_ring->write(silence.data(), primed);

	if (ma_device_start(&m_device->device) != MA_SUCCESS) {
		ma_device_uninit(&m_device->device);
		m_device->initialized = false;
		return false;
	}
	m_active = true;
	return true;
}

void AudioOutput::stop()
{
	if (m_device->initialized) {
		ma_device_uninit(&m_device->device);
		m_device->initialized = false;
	}
	m_active = false;
}

std::size_t AudioOutput::queuedFrames() const
{
	return m_ring->available();
}

int AudioOutput::pacedFramesPerVideoFrame() const
{
	const int nominal = nominalFramesPerVideoFrame();
	if (nominal <= 0) return 0;
	const int target = nominal * TargetVideoFrames;
	const int fill = static_cast<int>(m_ring->available());
	// Proportional correction, bounded to +/-12.5%. Steady state settles near
	// the ~2% the 17 ms timer actually needs; the bound only matters while
	// recovering from a stall, where a brief pitch wobble beats a dropout.
	const int correction = (target - fill) / 8;
	const int limit = nominal / 8;
	return nominal + std::clamp(correction, -limit, limit);
}

void AudioOutput::submit(const std::int16_t* frames, std::size_t count)
{
	if (!m_active) return;
	m_ring->write(frames, count);
}

} // namespace FairyWriter
