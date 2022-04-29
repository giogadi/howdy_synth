#include "AudioPluginUtil.h"

namespace Howdy
{
    float gNewFreq = -1.0f;

    enum Param
    {
        P_FREQ,
        P_INPUTMIX,
        P_NUM
    };

    struct EffectData
    {
        struct Data
        {
            float pattern[64];
            float phase;
            float lfophase;
            float freq;
            float lpf;
            float bpf;
            float env;
            float lfoenv;
            float cutrnd;
            float wetmix;
            float p[P_NUM];
            int pattern_index;
            AudioPluginUtil::Random random;
        };
        union
        {
            Data data;
            unsigned char pad[(sizeof(Data) + 15) & ~15]; // This entire structure must be a multiple of 16 bytes (and and instance 16 byte aligned) for PS3 SPU DMA requirements
        };
    };

    int InternalRegisterEffectDefinition(UnityAudioEffectDefinition& definition)
    {
        int numparams = P_NUM;
        definition.paramdefs = new UnityAudioParameterDefinition[numparams];
        AudioPluginUtil::RegisterParameter(definition, "Frequency", "", 0.0f, 1000.0f, 440.0f, 1.0f, 1.0f, P_FREQ, "frequency");
        AudioPluginUtil::RegisterParameter(definition, "InputMix", "%", 0.0f, 100.0f, 100.0f, 1.0f, 1.0f, P_INPUTMIX, "Amount of input signal mixed to the output of the synthesizer.");
        
        return numparams;
    }


    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK CreateCallback(UnityAudioEffectState* state)
    {
        EffectData* effectdata = new EffectData;
        memset(effectdata, 0, sizeof(EffectData));
        effectdata->data.phase = 0.0f;
        effectdata->data.freq = 440.0f;
        state->effectdata = effectdata;     
        AudioPluginUtil::InitParametersFromDefinitions(InternalRegisterEffectDefinition, effectdata->data.p);
        // CalcPattern(&effectdata->data);
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ReleaseCallback(UnityAudioEffectState* state)
    {
        EffectData* effectdata = state->GetEffectData<EffectData>();
        delete effectdata;
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK SetFloatParameterCallback(UnityAudioEffectState* state, int index, float value)
    {
        EffectData::Data* data = &state->GetEffectData<EffectData>()->data;
        if (index >= P_NUM)
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        data->p[index] = value;
        // if (index == P_SEED || index == P_MINNOTE || index == P_MAXNOTE)
        //     CalcPattern(data);
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK GetFloatParameterCallback(UnityAudioEffectState* state, int index, float* value, char *valuestr)
    {
        EffectData::Data* data = &state->GetEffectData<EffectData>()->data;
        if (index >= P_NUM)
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        if (value != NULL)
            *value = data->p[index];
        if (valuestr != NULL)
            valuestr[0] = 0;
        return UNITY_AUDIODSP_OK;
    }

    int UNITY_AUDIODSP_CALLBACK GetFloatBufferCallback(UnityAudioEffectState* state, const char* name, float* buffer, int numsamples)
    {
        return UNITY_AUDIODSP_OK;
    }

    inline void GenerateSawtooth(
        UnityAudioEffectState* state, float* outBuffer, unsigned int bufferLength, int outChannels) {

        EffectData::Data* data = &state->GetEffectData<EffectData>()->data;

        float const sampleRate = (float)state->samplerate;
        // float const freq = data->p[P_FREQ];
        if (gNewFreq >= 0.0f) {
            data->freq = gNewFreq;
            gNewFreq = -1.0f;
        }
        float phaseIncrement = data->freq*2*AudioPluginUtil::kPI / sampleRate;

        // phase=0: 1.0
        // phase=pi: 0
        // phase=2pi: -1.0
        //
        // phase / 2pi: [0,2pi] -> [0,1]
        // (phase / 2pi) * 2: [0,2pi] -> [0,2]
        // (phase / 2pi) * 2 - 1 -> [-1,1]
        // -((phase / 2pi) * 2 - 1) -> [1,-1]
        // 1 - phase/pi
        for (unsigned int sampleIx = 0; sampleIx < bufferLength; ++sampleIx) {
            if (data->phase >= 2*AudioPluginUtil::kPI) {
                data->phase -= 2*AudioPluginUtil::kPI;
            }
            float v = 1.0f - (data->phase / AudioPluginUtil::kPI);
            for (unsigned int channelIx = 0; channelIx < outChannels; ++channelIx) {
                outBuffer[sampleIx * outChannels + channelIx] = v;
            }
            data->phase += phaseIncrement;
        }
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ProcessCallback(UnityAudioEffectState* state, float* inBuffer, float* outBuffer, unsigned int bufferLength, int inChannels, int outChannels)
    {
        const bool shouldPlay = (state->flags & UnityAudioEffectStateFlags_IsPlaying) && !(state->flags & (UnityAudioEffectStateFlags_IsMuted | UnityAudioEffectStateFlags_IsPaused));
        if (!shouldPlay) {
            memset(outBuffer, 0.0f, bufferLength*outChannels*sizeof(float));
            return UNITY_AUDIODSP_OK;
        }

        GenerateSawtooth(state, outBuffer, bufferLength, outChannels);

        // EffectData::Data* data = &state->GetEffectData<EffectData>()->data;
        // float const sampleRate = (float)state->samplerate;
        // // float const freq = data->p[P_FREQ];
        // if (gNewFreq >= 0.0f) {
        //     data->freq = gNewFreq;
        //     gNewFreq = -1.0f;
        // }
        // float phaseIncrement = data->freq*2*AudioPluginUtil::kPI / sampleRate;
        // for (unsigned int sampleIx = 0; sampleIx < bufferLength; ++sampleIx) {
        //     float p = sinf(data->phase);
        //     for (unsigned int channelIx = 0; channelIx < outChannels; ++channelIx) {
        //         outBuffer[sampleIx * outChannels + channelIx] = p;
        //     }
        //     data->phase += phaseIncrement;
        //     if (data->phase >= 2*AudioPluginUtil::kPI) {
        //         data->phase -= 2*AudioPluginUtil::kPI;
        //     }
        // }

        return UNITY_AUDIODSP_OK;
    }
}
