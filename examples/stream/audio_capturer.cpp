#include "audio_capturer.hpp"
#include <iostream>
#include <cassert>

using namespace mv;
using namespace std;

AudioCapturer::AudioCapturer(const AudioTime& ms): m_len_ms(ms),m_sample_rate(0), m_running(false), m_mutex(){

}

AudioCapturer::~AudioCapturer() {
    if (m_dev_id_in) {
        SDL_CloseAudioDevice(m_dev_id_in);
    }
    m_audio.retrieveAll();
}

bool AudioCapturer::init(int capture_id, int sampling_rate,const AudioTime& sampleTime) {
    std::lock_guard<std::mutex> lock(m_mutex);

    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s\n", SDL_GetError());
        return false;
    }

    SDL_SetHintWithPriority(SDL_HINT_AUDIO_RESAMPLING_MODE, "medium", SDL_HINT_OVERRIDE);

    {
        int nDevices = SDL_GetNumAudioDevices(SDL_TRUE);
        fprintf(stderr, "%s: found %d capture devices:\n", __func__, nDevices);
        for (int i = 0; i < nDevices; i++) {
            fprintf(stderr, "%s:    - Capture device #%d: '%s'\n", __func__, i, SDL_GetAudioDeviceName(i, SDL_TRUE));
        }
    }

    SDL_AudioSpec capture_spec_requested;
    SDL_AudioSpec capture_spec_obtained;

    SDL_zero(capture_spec_requested);
    SDL_zero(capture_spec_obtained);

    capture_spec_requested.freq     = sampling_rate;
    capture_spec_requested.format   = AUDIO_F32;
    capture_spec_requested.channels = 1;
    capture_spec_requested.samples  = std::round(sampling_rate*(sampleTime.count()/1000.0f));
    capture_spec_requested.callback = [](void * userdata, uint8_t * stream, int len) {
        AudioCapturer * audio = (AudioCapturer *) userdata;
        audio->callback(stream, len);
    };
    capture_spec_requested.userdata = this;

    if (capture_id >= 0) {
        fprintf(stderr, "%s: attempt to open capture device %d : '%s' ...\n", __func__, capture_id, SDL_GetAudioDeviceName(capture_id, SDL_TRUE));
        m_dev_id_in = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(capture_id, SDL_TRUE), SDL_TRUE, &capture_spec_requested, &capture_spec_obtained, 0);
    } else {
        fprintf(stderr, "%s: attempt to open default capture device ...\n", __func__);
        m_dev_id_in = SDL_OpenAudioDevice(nullptr, SDL_TRUE, &capture_spec_requested, &capture_spec_obtained, 0);
    }

    if (!m_dev_id_in) {
        fprintf(stderr, "%s: couldn't open an audio device for capture: %s!\n", __func__, SDL_GetError());
        m_dev_id_in = 0;

        return false;
    } else {
        fprintf(stderr, "%s: obtained spec for input device (SDL Id = %d):\n", __func__, m_dev_id_in);
        fprintf(stderr, "%s:     - sample rate:       %d\n",                   __func__, capture_spec_obtained.freq);
        fprintf(stderr, "%s:     - format:            %d (required: %d)\n",    __func__, capture_spec_obtained.format,
                capture_spec_requested.format);
        fprintf(stderr, "%s:     - channels:          %d (required: %d)\n",    __func__, capture_spec_obtained.channels,
                capture_spec_requested.channels);
        fprintf(stderr, "%s:     - samples per frame: %d\n",                   __func__, capture_spec_obtained.samples);
    }

    m_sample_rate = capture_spec_obtained.freq;
    //std::cout<<"AudioCapturer::init: m_sample_rate = "<<time2Bytes(m_len_ms,m_sample_rate)<<std::endl;
    m_audio = mv::Buf(time2Bytes(m_len_ms,m_sample_rate));
    //m_audio.resize((m_sample_rate*m_len_ms)/1000);

    return true;
}

bool AudioCapturer::resume() {
    if (!m_dev_id_in) {
        fprintf(stderr, "%s: no audio device to resume!\n", __func__);
        return false;
    }

    if (m_running) {
        fprintf(stderr, "%s: already running!\n", __func__);
        return false;
    }

    SDL_PauseAudioDevice(m_dev_id_in, 0);

    m_running = true;

    return true;
}

bool AudioCapturer::pause() {
    if (!m_dev_id_in) {
        fprintf(stderr, "%s: no audio device to pause!\n", __func__);
        return false;
    }

    if (!m_running) {
        fprintf(stderr, "%s: already paused!\n", __func__);
        return false;
    }

    SDL_PauseAudioDevice(m_dev_id_in, 1);

    m_running = false;

    return true;
}

bool AudioCapturer::clear() {
    if (!m_dev_id_in) {
        fprintf(stderr, "%s: no audio device to clear!\n", __func__);
        return false;
    }

    if (!m_running) {
        fprintf(stderr, "%s: not running!\n", __func__);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_audio.retrieveAll();
    }

    return true;
}

// callback to be called by SDL
void AudioCapturer::callback(uint8_t * stream, int len) {
    if (!m_running) {
        return;
    }
    //std::cout<<"AudioCapturer::callback: len = "<<len<<", "<< std::chrono::milliseconds(len*1000/sizeof(float)/m_sample_rate).count()<<std::endl;
    std::lock_guard<std::mutex> lock(m_mutex);

    if(len> static_cast<int32_t>(m_audio.writableBytes())){
        std::cerr<<"Skip "<<(len- m_audio.writableBytes())<<" bytes of audio data"<<std::endl;
    }
    //m_audio.hasWritten(std::min(len,static_cast<int32_t>(m_audio.writableBytes())));
    m_audio.append(stream, std::min(len,static_cast<int32_t>(m_audio.writableBytes())));

}


AudioCapturer::AudioTime AudioCapturer::bufferTime() const{
    return bytes2Time(m_audio.readableBytes(),m_sample_rate);
}

AudioCapturer::AudioTime AudioCapturer::availableBufferTime() const{
    return bytes2Time(m_audio.writableBytes(),m_sample_rate);
}

AudioCapturer::AudioTime AudioCapturer::get(const AudioCapturer::AudioTime& ms, mv::Buf& audio) {
    if (!m_dev_id_in) {
        fprintf(stderr, "%s: no audio device to get audio from!\n", __func__);
        return AudioTime(0);
    }

    if (!m_running) {
        fprintf(stderr, "%s: not running!\n", __func__);
        return AudioTime(0);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);


        size_t n_bytes = time2Bytes(ms,m_sample_rate);
        if( n_bytes>m_audio.readableBytes()){
            n_bytes = m_audio.readableBytes();
        }
        
        audio.ensureWritableBytes(n_bytes);
       // std::fill(result.begin(), result.end(), 0);
        //std::memcpy(result.data(), m_audio.peek(), n_bytes);
        std::copy(m_audio.peek(), m_audio.peek()+n_bytes, audio.beginWrite());
        m_audio.retrieve(n_bytes);
        audio.hasWritten(n_bytes);

        return bytes2Time(n_bytes,m_sample_rate);
    }
}