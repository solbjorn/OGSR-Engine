#include "stdafx.h"

#include "SoundRender_TargetA.h"

#include "SoundRender_Emitter.h"
#include "SoundRender_Source.h"

#include <alext.h>
#include <efx.h>

bool CSoundRender_TargetA::_initialize()
{
    // initialize buffer
    A_CHK(alGenBuffers(sdef_target_count_submit, pBuffers));
    alGenSources(1, &pSource);
    const ALenum error = alGetError();
    if (AL_NO_ERROR == error)
    {
        A_CHK(alSourcei(pSource, AL_LOOPING, AL_FALSE));
        A_CHK(alSourcef(pSource, AL_MIN_GAIN, 0.f));
        A_CHK(alSourcef(pSource, AL_MAX_GAIN, 1.f));
        A_CHK(alSourcef(pSource, AL_GAIN, cache_gain));
        A_CHK(alSourcef(pSource, AL_PITCH, cache_pitch));

        // master gain filter
        efx_api::alGenFilters(1, &sound_local_filter);

        efx_api::alFilteri(sound_local_filter, AL_FILTER_TYPE, AL_FILTER_BANDPASS);
        efx_api::alFilterf(sound_local_filter, AL_BANDPASS_GAIN, 1.f);
        efx_api::alFilterf(sound_local_filter, AL_BANDPASS_GAINLF, 1.f);
        efx_api::alFilterf(sound_local_filter, AL_BANDPASS_GAINHF, 1.f);

        return TRUE;
    }

    XR_LOG_ERROR("OpenAL: Can't create source. Error: {}", alGetString(error));

    return FALSE;
}

void CSoundRender_TargetA::alAuxInit(ALuint slot) { A_CHK(alSource3i(pSource, AL_AUXILIARY_SEND_FILTER, slot, 0, AL_FILTER_NULL)); }

void CSoundRender_TargetA::_destroy()
{
    if (efx_api::alIsFilter(sound_local_filter))
        efx_api::alDeleteFilters(1, &sound_local_filter);

    // clean up target
    if (alIsSource(pSource))
        alDeleteSources(1, &pSource);
    A_CHK(alDeleteBuffers(sdef_target_count_submit, pBuffers));
}

void CSoundRender_TargetA::_restart()
{
    _destroy();
    _initialize();
}

void CSoundRender_TargetA::start(CSoundRender_Emitter* E)
{
    inherited::start(E);

    const auto& wvf = m_pEmitter->source()->m_wformat;
    dataFormat = wvf.channels == 1 ? AL_FORMAT_MONO_FLOAT32 : AL_FORMAT_STEREO_FLOAT32;
    sampleRate = wvf.samplerate;
}

tmc::task<void> CSoundRender_TargetA::render()
{
    co_await inherited::render();
    co_await submit_all_buffers();

    A_CHK(alSourceQueueBuffers(pSource, sdef_target_count_submit, pBuffers));
    A_CHK(alSourcePlay(pSource));
}

void CSoundRender_TargetA::stop()
{
    if (rendering)
    {
        A_CHK(alSourceStop(pSource));
        A_CHK(alSourcei(pSource, AL_BUFFER, NULL));
        A_CHK(alSourcei(pSource, AL_SOURCE_RELATIVE, TRUE));
    }
    inherited::stop();
}

tmc::task<void> CSoundRender_TargetA::rewind()
{
    co_await inherited::rewind();

    A_CHK(alSourceStop(pSource));
    A_CHK(alSourcei(pSource, AL_BUFFER, NULL));

    co_await submit_all_buffers();

    A_CHK(alSourceQueueBuffers(pSource, sdef_target_count_submit, pBuffers));
    A_CHK(alSourcePlay(pSource));
}

tmc::task<void> CSoundRender_TargetA::update()
{
    co_await inherited::update();

    if (bAlSoft)
    {
        ALint processed, state;

        /* Get relevant source info */
        alGetSourcei(pSource, AL_SOURCE_STATE, &state);
        alGetSourcei(pSource, AL_BUFFERS_PROCESSED, &processed);

        ALenum error = alGetError();
        if (error != AL_NO_ERROR)
        {
            XR_LOG_ERROR("Error checking source state! OpenAL Error: [{}]", alGetString(error));
            co_return;
        }

        while (processed > 0)
        {
            ALuint BufferID;
            A_CHK(alSourceUnqueueBuffers(pSource, 1, &BufferID));

            co_await submit_buffer(BufferID);

            A_CHK(alSourceQueueBuffers(pSource, 1, &BufferID));
            processed--;

            ALenum error = alGetError();
            if (error != AL_NO_ERROR)
            {
                XR_LOG_ERROR("Error buffering data! OpenAL Error: [{}]", alGetString(error));
                co_return;
            }
        }

        /* Make sure the source hasn't underrun */
        if (state != AL_PLAYING && state != AL_PAUSED)
        {
            ALint queued;

            /* If no buffers are queued, playback is finished */
            alGetSourcei(pSource, AL_BUFFERS_QUEUED, &queued);
            if (queued == 0)
                co_return;

            alSourcePlay(pSource);
            ALenum error = alGetError();
            if (error != AL_NO_ERROR)
            {
                XR_LOG_ERROR("Error restarting playback! OpenAL Error: [{}]", alGetString(error));
                co_return;
            }
        }
    }
    else
    {
        ALint processed;
        // Get status
        A_CHK(alGetSourcei(pSource, AL_BUFFERS_PROCESSED, &processed));

        if (processed > 0)
        {
            while (processed)
            {
                ALuint BufferID;
                A_CHK(alSourceUnqueueBuffers(pSource, 1, &BufferID));

                co_await submit_buffer(BufferID);

                A_CHK(alSourceQueueBuffers(pSource, 1, &BufferID));
                processed--;
            }
        }
        else
        {
            // processed == 0
            // check play status -- if stopped then queue is not being filled fast enough
            ALint state;
            A_CHK(alGetSourcei(pSource, AL_SOURCE_STATE, &state));
            if (state != AL_PLAYING)
                A_CHK(alSourcePlay(pSource));
        }
    }
}

void CSoundRender_TargetA::fill_parameters(CSoundRender_Core* core)
{
    XR_ASSERT(m_pEmitter != nullptr);

    // 3D params
    A_CHK(alSourcef(pSource, AL_REFERENCE_DISTANCE, m_pEmitter->p_source.min_distance));
    A_CHK(alSourcef(pSource, AL_MAX_DISTANCE, m_pEmitter->p_source.max_distance));

    A_CHK(alSource3f(pSource, AL_POSITION, m_pEmitter->p_source.position.x, m_pEmitter->p_source.position.y, -m_pEmitter->p_source.position.z));
    A_CHK(alSource3f(pSource, AL_VELOCITY, m_pEmitter->p_source.velocity.x, m_pEmitter->p_source.velocity.y, -m_pEmitter->p_source.velocity.z));

    A_CHK(alSourcei(pSource, AL_SOURCE_RELATIVE, m_pEmitter->b2D));
    A_CHK(alSourcef(pSource, AL_ROLLOFF_FACTOR, psSoundRolloff));

    if (bEFX)
    {
        float smooth_hf_gain = m_pEmitter->smooth_hf_volume;
        clamp(smooth_hf_gain, EPS_S, 1.f);

        float high_gain = m_pEmitter->get_params()->high_gain * smooth_hf_gain; // учесть затухания звука из за дистанции
        float low_gain = m_pEmitter->get_params()->low_gain;

        high_gain = _min(high_gain, core->master_high_pass);
        low_gain = _min(low_gain, core->master_low_pass);

        if (!fsimilar(cache_low_gain, low_gain, 0.01f) || !fsimilar(cache_high_gain, high_gain, 0.01f))
        {
            efx_api::alFilterf(sound_local_filter, AL_BANDPASS_GAINLF, low_gain);
            efx_api::alFilterf(sound_local_filter, AL_BANDPASS_GAINHF, high_gain);

            A_CHK(alSourcei(pSource, AL_DIRECT_FILTER, sound_local_filter));

            cache_low_gain = low_gain;
            cache_high_gain = high_gain;
        }
    }
    else
    {
        A_CHK(alSourcei(pSource, AL_DIRECT_FILTER, AL_FILTER_NULL));
    }

    float _gain = m_pEmitter->smooth_volume;
    clamp(_gain, EPS_S, 1.f);
    if (!fsimilar(_gain, cache_gain, 0.01f))
    {
        cache_gain = _gain;
        A_CHK(alSourcef(pSource, AL_GAIN, _gain));
    }

    float _pitch = m_pEmitter->p_source.freq * psSoundTimeFactor; //--#SM+#-- Correct sound "speed" by time factor
    clamp(_pitch, EPS_L, 100.f); //--#SM+#-- Increase sound frequency (speed) limit
    if (!fsimilar(_pitch, cache_pitch))
    {
        cache_pitch = _pitch;
        A_CHK(alSourcef(pSource, AL_PITCH, _pitch));
    }
}

tmc::task<void> CSoundRender_TargetA::submit_buffer(ALuint BufferID) const
{
    XR_ASSERT(m_pEmitter != nullptr);

    const auto block = co_await m_pEmitter->obtain_block();
    A_CHK(alBufferData(BufferID, dataFormat, block.data(), block.size(), sampleRate));
}

tmc::task<void> CSoundRender_TargetA::submit_all_buffers() const
{
    for (const auto buffer : pBuffers)
        co_await submit_buffer(buffer);
}
