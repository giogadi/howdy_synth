#include <stdio.h>
#include <math.h>
#include <string.h>
#include "portaudio.h"

#define NUM_SECONDS   (5)
#define SAMPLE_RATE   (44100)
#define FRAMES_PER_BUFFER  (64)

#ifndef M_PI
#define M_PI  (3.14159265)
#endif

const float kSemitoneRatio = 1.05946309f;
const float kFifthRatio = 1.49830703f;

// TODO: use a lookup table!!!
float midiToFreq(int midi) {
    int const a4 = 69;  // 440Hz
    return 440.0f * pow(2.0f, (midi - a4) / 12.0f);
}

enum class EventType {
    None, NoteOn, NoteOff
};

struct Event {
    EventType type;
    int timeInTicks = 0;
    int midiNote = 0;
};

#define MIDI_SEQ_LENGTH (16)
#define EVENT_QUEUE_LENGTH (64)
struct paTestData {
    float f = 440.0f;
    float left_phase = 0.0f;
    float right_phase = 0.0f;
    float cutoffFreq = 0.0f;
    float cutoffK = 0.0f;  // [0,4] but 4 is unstable
    float lp0 = 0.0f;
    float lp1 = 0.0f;
    float lp2 = 0.0f;
    float lp3 = 0.0f;

    float pitchLFOGain = 0.0f;
    float pitchLFOFreq = 0.0f;
    float pitchLFOPhase = 0.0f;

    float cutoffLFOGain = 0.0f;
    float cutoffLFOFreq = 0.0f;
    float cutoffLFOPhase = 0.0f;

    float ampEnvAttackTime = 0.0f;
    float ampEnvDecayTime = 0.0f;
    float ampEnvSustainLevel = 1.0f;
    int ampEnvTicksSinceStart = -1;

    Event events[EVENT_QUEUE_LENGTH];
    int eventIx = 0;

    int tickTime = 0;

    char message[20];
};

float polyblep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t+t - t*t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t*t + t+t + 1.0f;
    }
    else {
        return 0.0f;
    }
}

inline float generateSquare(float const phase, float const phaseChange) {
    float v = 0.0f;
    if (phase < M_PI) {
        v = 1.0f;
    } else {
        v = -1.0f;
    }
    // polyblep
    float dt = phaseChange / (2*M_PI);
    float t = phase / (2*M_PI);
    v += polyblep(t, dt);
    v -= polyblep(fmod(t + 0.5f, 1.0f), dt);
    return v;
}

inline float generateSaw(float const phase, float const phaseChange) {
    float v = (phase / M_PI) - 1.0f;
    // polyblep
    float dt = phaseChange / (2*M_PI);
    float t = phase / (2*M_PI);
    v -= polyblep(t, dt);
    return v;
}

/* This routine will be called by the PortAudio engine when audio is needed.
** It may called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
// Let's do 100 samples up and 100 samples down.
static int patestCallback( const void *inputBuffer, void *outputBuffer,
                            unsigned long framesPerBuffer,
                            const PaStreamCallbackTimeInfo* timeInfo,
                            PaStreamCallbackFlags statusFlags,
                            void *userData )
{
    paTestData *data = (paTestData*)userData;
    float *out = (float*)outputBuffer;
    unsigned long i;

    (void) timeInfo; /* Prevent unused variable warnings. */
    (void) statusFlags;
    (void) inputBuffer;
    
    float const dt = 1.0f / SAMPLE_RATE;
    float const k = data->cutoffK;  // between [0,4], unstable at 4
    int const attackTimeInTicks = data->ampEnvAttackTime * SAMPLE_RATE;
    int const decayTimeInTicks = data->ampEnvDecayTime * SAMPLE_RATE;
    
    for( i=0; i<framesPerBuffer; i++ )
    {
        // TODO: Detect when we've wrapped around to the front of the ring
        // buffer (although that will only happen when adding items, not
        // consuming them)
        Event* e = &(data->events[data->eventIx]);
        while (e->type != EventType::None && data->tickTime >= e->timeInTicks) {
            if (e->timeInTicks == data->tickTime) {
                switch (e->type) {
                    case EventType::NoteOn: {
                        data->f = midiToFreq(e->midiNote);
                        data->ampEnvTicksSinceStart = 0;
                    }
                        break;
                    case EventType::NoteOff: {
                        // TODO
                    }
                        break;
                    default: {
                        // will never happen
                    }
                        break;
                }
            }
            data->eventIx = (data->eventIx + 1) % EVENT_QUEUE_LENGTH;
            e = &(data->events[data->eventIx]);
        }

        // Get pitch LFO value
        if (data->pitchLFOPhase >= 2*M_PI) {
            data->pitchLFOPhase -= 2*M_PI;
        }
        // Make LFO a sine wave for now.
        float const pitchLFOValue = data->pitchLFOGain * sinf(data->pitchLFOPhase);
        data->pitchLFOPhase += (data->pitchLFOFreq * 2*M_PI / SAMPLE_RATE);

        // Now use the LFO value to get a new frequency.
        float const modulatedF = data->f * powf(2.0f, pitchLFOValue);

        // use left phase for both channels for now.
        if (data->left_phase >= 2*M_PI) {
            data->left_phase -= 2*M_PI;
        }

        float phaseChange = 2*M_PI*modulatedF / SAMPLE_RATE;

        float v = 0.0f;
        {
            // v = generateSquare(data->left_phase, phaseChange);
            v = generateSaw(data->left_phase, phaseChange);
            data->left_phase += phaseChange;
        }

        // Get cutoff LFO value
        if (data->cutoffLFOPhase >= 2*M_PI) {
            data->cutoffLFOPhase -= 2*M_PI;
        }
        // Make LFO a sine wave for now.
        float const cutoffLFOValue = data->cutoffLFOGain * sinf(data->cutoffLFOPhase);
        data->cutoffLFOPhase += (data->cutoffLFOFreq * 2*M_PI / SAMPLE_RATE);

        float const modulatedCutoff = data->cutoffFreq * powf(2.0f, cutoffLFOValue);

        // ladder filter
        // TODO: should we put this in the oversampling?
        float const rc = 1 / modulatedCutoff;
        float const a = dt / (rc + dt);
        v -= k*data->lp3;

        data->lp0 = a*v + (1-a)*data->lp0;
        data->lp1 = a*(data->lp0) + (1-a)*data->lp1;
        data->lp2 = a*(data->lp1) + (1-a)*data->lp2;
        data->lp3 = a*(data->lp2) + (1-a)*data->lp3;
        v = data->lp3;

        // Amplitude envelope
        float ampEnvValue = 1.0f;
        if (data->ampEnvTicksSinceStart < attackTimeInTicks) {
            // attack phase
            float const ampEnvT = fmin(1.0f, (float) data->ampEnvTicksSinceStart / (float) attackTimeInTicks);
            float const startAmp = 0.0001f;
            float const factor = 1.0f / startAmp;
            ampEnvValue = startAmp*powf(factor, ampEnvT);
        } else {
            // decay phase
            int ticksSinceDecayStart = data->ampEnvTicksSinceStart - attackTimeInTicks;
            float const t = fmin(1.0f, (float) ticksSinceDecayStart / (float) decayTimeInTicks);
            float const sustain = fmax(0.0001f, data->ampEnvSustainLevel);
            ampEnvValue = 1.0f*powf(sustain, t);
        }
        ++data->ampEnvTicksSinceStart;

        v *= ampEnvValue;

        *out++ = v;
        *out++ = v;

        ++data->tickTime;
    }
    
    return paContinue;
}

/*
 * This routine is called by portaudio when playback is done.
 */
static void StreamFinished( void* userData )
{
   paTestData *data = (paTestData *) userData;
   printf( "Stream Completed: %s\n", data->message );
}

/*******************************************************************/
int main(void);
int main(void)
{
    PaStreamParameters outputParameters;
    PaStream *stream;
    PaError err;
    paTestData data;
    int i;

    
    printf("PortAudio Test: output sine wave. SR = %d, BufSize = %d\n", SAMPLE_RATE, FRAMES_PER_BUFFER);
    
    data.left_phase = data.right_phase = 0.0f;
    data.f = 440.0f;
    data.lp0 = 0.0f;
    data.lp1 = 0.0f;
    data.lp2 = 0.0f;
    data.lp3 = 0.0f;
    data.cutoffFreq = 44100.0f;
    data.cutoffK = 0.0f;
    data.pitchLFOFreq = 1.0f;
    data.pitchLFOGain = 0.0f;
    data.pitchLFOPhase = 0.0f;
    data.cutoffLFOFreq = 10.0f;
    data.cutoffLFOGain = 0.0f;
    data.cutoffLFOPhase = 0.0f;
    data.ampEnvAttackTime = 0.1f;
    data.ampEnvDecayTime = 0.5f;
    data.ampEnvSustainLevel = 0.0f;
    
    for (int i = 0; i < EVENT_QUEUE_LENGTH; ++i) {
        data.events[i].type = EventType::None;
    }

    // PROGRAM A SEQUENCER USING EVENTS!!!
    int const bpm = 200;
    int const kSamplesPerBeat = (SAMPLE_RATE * 60) / bpm;
    for (int i = 0; i < 16; ++i) {
        data.events[2*i].type = EventType::NoteOn;
        data.events[2*i].timeInTicks = kSamplesPerBeat*i;
        data.events[2*i].midiNote = 69 + i;

        data.events[2*i+1].type = EventType::NoteOff;
        data.events[2*i+1].timeInTicks = kSamplesPerBeat*i + (kSamplesPerBeat / 2);
        data.events[2*i+1].midiNote = 69 + i;
    }

    err = Pa_Initialize();
    if( err != paNoError ) goto error;

    outputParameters.device = Pa_GetDefaultOutputDevice(); /* default output device */
    if (outputParameters.device == paNoDevice) {
      fprintf(stderr,"Error: No default output device.\n");
      goto error;
    }
    outputParameters.channelCount = 2;       /* stereo output */
    outputParameters.sampleFormat = paFloat32; /* 32 bit floating point output */
    outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(
              &stream,
              NULL, /* no input */
              &outputParameters,
              SAMPLE_RATE,
              FRAMES_PER_BUFFER,
              paClipOff,      /* we won't output out of range samples so don't bother clipping them */
              patestCallback,
              &data );
    if( err != paNoError ) goto error;

    sprintf( data.message, "No Message" );
    err = Pa_SetStreamFinishedCallback( stream, &StreamFinished );
    if( err != paNoError ) goto error;

    err = Pa_StartStream( stream );
    if( err != paNoError ) goto error;

    printf("Play for %d seconds.\n", NUM_SECONDS );
    Pa_Sleep( NUM_SECONDS * 1000 );

    err = Pa_StopStream( stream );
    if( err != paNoError ) goto error;

    err = Pa_CloseStream( stream );
    if( err != paNoError ) goto error;

    Pa_Terminate();
    printf("Test finished.\n");
    
    return err;
error:
    Pa_Terminate();
    fprintf( stderr, "An error occured while using the portaudio stream\n" );
    fprintf( stderr, "Error number: %d\n", err );
    fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
    return err;
}