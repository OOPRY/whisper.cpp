#pragma once

#include <SDL.h>
#include <SDL_audio.h>

#include <atomic>
#include <cstdint>
#include <vector>
#include <mutex>
#include "buf.hpp"

class AudioCapturer {
public:
    using AudioTime = std::chrono::duration<double, std::milli>;
    AudioCapturer(const AudioTime& len_ms);
    ~AudioCapturer();

    bool init(int capture_id, int sample_rate,const AudioTime& sampleTime);

    // start capturing audio via the provided SDL callback
    // keep last len_ms seconds of audio in a circular buffer
    bool resume();
    bool pause();
    bool clear();

    // callback to be called by SDL
    void callback(uint8_t * stream, int len);

    // get audio data from the circular buffer
    AudioTime get(const AudioTime& ms, mv::Buf& audio);

    AudioTime bufferTime() const;
    AudioTime availableBufferTime() const;
    static constexpr AudioTime bytes2Time(int bytes, int sample_rate){
        return AudioCapturer::AudioTime(static_cast<int64_t>(static_cast<double>(bytes/sizeof(float))/sample_rate*1000));
    }

    template <class _Rep, class _Period>
    static constexpr std::size_t time2Bytes(const std::chrono::duration<_Rep, _Period>& time, int sample_rate){
        return (std::chrono::duration_cast<AudioTime>(time).count()*sizeof(float)*sample_rate)/1000;
    }

private:
    SDL_AudioDeviceID m_dev_id_in = 0;

    AudioTime m_len_ms;
    int m_sample_rate = 0;

    std::atomic_bool m_running;
    std::mutex       m_mutex;
    mv::Buf m_audio;
};