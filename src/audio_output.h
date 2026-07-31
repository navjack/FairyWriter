/*
	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FAIRYWRITER_AUDIO_OUTPUT_H
#define FAIRYWRITER_AUDIO_OUTPUT_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace FairyWriter {

// Lock-free single-producer/single-consumer ring of interleaved stereo frames.
// The producer is the thread that runs the emulated frame; the consumer is the
// audio device callback. Capacity is a power of two so the monotonic counters
// can index with a mask and survive wraparound.
class AudioRing final {
public:
	explicit AudioRing(std::size_t frames);

	// Producer side. Writes as many frames as fit and returns that count; a
	// short write means the consumer is behind and the surplus is dropped
	// rather than allowed to grow latency without bound.
	std::size_t write(const std::int16_t* frames, std::size_t count);

	// Consumer side. Fills `count` frames, zero-padding any shortfall, and
	// returns how many were real. An underrun is silence, never a stall.
	std::size_t read(std::int16_t* frames, std::size_t count);

	std::size_t available() const;
	std::size_t capacity() const noexcept { return m_capacity; }

private:
	std::size_t m_capacity;
	std::vector<std::int16_t> m_samples;
	std::atomic<std::size_t> m_write{0};
	std::atomic<std::size_t> m_read{0};
};

// A stereo 16-bit playback device. Opening one is best-effort: if no device is
// available the object stays inactive and every call is a no-op, because losing
// audio must never stop the editor from running.
class AudioOutput final {
public:
	AudioOutput();
	~AudioOutput();
	AudioOutput(const AudioOutput&) = delete;
	AudioOutput& operator=(const AudioOutput&) = delete;

	bool start();
	void stop();
	bool isActive() const noexcept { return m_active; }
	int sampleRate() const noexcept { return m_sample_rate; }

	// Frames of audio one emulated video frame is nominally worth, and the
	// count the caller should actually render right now. The second is the
	// first nudged toward a steady buffer fill: the host frame timer is 17 ms
	// rather than a true 16.67 ms, so a fixed block size drifts into a
	// permanent underrun. The S-DSP block is resampled either way, so
	// absorbing the drift here costs a fraction of a percent of pitch instead
	// of a dropout every few seconds.
	int nominalFramesPerVideoFrame() const noexcept { return m_sample_rate / 60; }
	int pacedFramesPerVideoFrame() const;

	std::size_t queuedFrames() const;
	void submit(const std::int16_t* frames, std::size_t count);

private:
	struct Device;
	std::unique_ptr<Device> m_device;
	std::unique_ptr<AudioRing> m_ring;
	int m_sample_rate = 48000;
	bool m_active = false;
};

} // namespace FairyWriter

#endif // FAIRYWRITER_AUDIO_OUTPUT_H
