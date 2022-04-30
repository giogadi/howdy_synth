#include "AudioPluginUtil.h"
#include "synth_common.h"

namespace
{
    common::EventQueue gEventQueue(common::kEventQueueLength);
    // TODO: THIS IS PROBABLY HORRIBLY UNSAFE
    int* gSynthTicks = nullptr;
}

extern "C" bool NoteOn(int midiNum, int ticksUntilEvent) {
    common::Event e;
    e.type = common::EventType::NoteOn;
    e.midiNote = midiNum;
    e.timeInTicks = ticksUntilEvent;
    return gEventQueue.try_push(e);
}

extern "C" bool NoteOff(int midiNum, int ticksUntilEvent) {
    common::Event e;
    e.type = common::EventType::NoteOff;
    e.midiNote = midiNum;
    e.timeInTicks = ticksUntilEvent;
    return gEventQueue.try_push(e);
}

extern "C" int GetSynthTicks() {
    return *gSynthTicks;
}

namespace Howdy {

    enum Param
    {
        P_FREQ,
        P_INPUTMIX,
        P_NUM
    };

    struct Data {
        common::StateData state;
        float p[P_NUM];
    };
    union PaddedData {
        // NOTE: clang for some reason needs these braces here or else it
        // complains about this variant having a non-trivial constructor.
        Data data {};

        // This entire structure must be a multiple of 16 bytes (and and
        // instance 16 byte aligned) for PS3 SPU DMA requirements
        unsigned char pad[(sizeof(Data) + 15) & ~15]; 
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
        PaddedData* paddedData = new PaddedData;
        common::InitStateData(paddedData->data.state, &gEventQueue, state->samplerate);
        gSynthTicks = &(paddedData->data.state.tickTime);
        state->effectdata = paddedData;     
        AudioPluginUtil::InitParametersFromDefinitions(InternalRegisterEffectDefinition, paddedData->data.p);
        // CalcPattern(&effectdata->data);
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ReleaseCallback(UnityAudioEffectState* state)
    {
        PaddedData* paddedData = state->GetEffectData<PaddedData>();
        delete paddedData;
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK SetFloatParameterCallback(UnityAudioEffectState* state, int index, float value)
    {
        Data* data = &state->GetEffectData<PaddedData>()->data;
        if (index >= P_NUM)
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        data->p[index] = value;
        // if (index == P_SEED || index == P_MINNOTE || index == P_MAXNOTE)
        //     CalcPattern(data);
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK GetFloatParameterCallback(UnityAudioEffectState* state, int index, float* value, char *valuestr)
    {
        Data* data = &state->GetEffectData<PaddedData>()->data;
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

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ProcessCallback(UnityAudioEffectState* state, float* inBuffer, float* outBuffer, unsigned int bufferLength, int inChannels, int outChannels)
    {
        const bool shouldPlay = (state->flags & UnityAudioEffectStateFlags_IsPlaying) && !(state->flags & (UnityAudioEffectStateFlags_IsMuted | UnityAudioEffectStateFlags_IsPaused));
        if (!shouldPlay) {
            memset(outBuffer, 0.0f, bufferLength*outChannels*sizeof(float));
            return UNITY_AUDIODSP_OK;
        }

        Data* data = &state->GetEffectData<PaddedData>()->data;
        common::Process(&data->state, outBuffer, outChannels, bufferLength, state->samplerate);

        return UNITY_AUDIODSP_OK;
    }
}
