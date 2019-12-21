/*
 * Version modified for use of Fluidsynth with iOS
 * Copyright (C) {2019} {Digital Logic LTD}
 *
 * This library is free software; you can
 * redistribute it and/or
 * modify it under the terms of the GNU Lesser
 * General Public
 * License as published by the Free Software
 * Foundation; either
 * version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that
 * it will be useful, but WITHOUT ANY WARRANTY; 
 * without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE.  See the GNU Lesser General Public
 * License for more details.

 * You should have received a copy of the GNU
 * Lesser General Public License along with
 * this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin
 * Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * You can contact representatives of
 * Digital Logic by e-mail 
 * g2ames.developer@gmail.com
 * or you can write to us to address:
 * Digital Logic LTD, Nemanjina 57A, Pozarevac,
 * Serbia.
 */
 
/* FluidSynth - A Software Synthesizer
 *
 * Copyright (C) 2003  Peter Hanappe and others.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

/* fluid_coreaudio.c
 *
 * Driver for the Apple's CoreAudio on IOS
 *
 */

#include "fluid_synth.h"
#include "fluid_midi.h"
#include "fluid_adriver.h"
#include "fluid_mdriver.h"
#include "fluid_settings.h"

#include "config.h"
#define COREAUDIO_SUPPORT 1
#if COREAUDIO_SUPPORT
#include <CoreAudio/CoreAudioTypes.h>
#include <AudioToolbox/AudioToolbox.h>

#define my_min(x,y) (x < y) ? x : y

#define kOutputBus 0
#define kInputBus 1

/*
 * fluid_core_audio_driver_t
 *
 */
typedef struct {
  fluid_audio_driver_t driver;
  AudioComponentInstance id;
  AudioStreamBasicDescription format;
  fluid_audio_func_t callback;
  void* data;
  unsigned int buffer_size;
  float* buffers[2];
  double phase;
} fluid_core_audio_driver_t;

fluid_audio_driver_t* new_fluid_core_audio_driver(fluid_settings_t* settings, fluid_synth_t* synth);

fluid_audio_driver_t* new_fluid_core_audio_driver2(fluid_settings_t* settings,
                              fluid_audio_func_t func,
                              void* data);

OSStatus playbackCallback(void *inRefCon,
                                 AudioUnitRenderActionFlags *ioActionFlags,
                                 const AudioTimeStamp *inTimeStamp,
                                 UInt32 inBusNumber,
                                 UInt32 inNumberFrames,
                                 AudioBufferList *ioData);

int delete_fluid_core_audio_driver(fluid_audio_driver_t* p);


/**************************************************************
 *
 *        CoreAudio audio driver
 *
 */

void
fluid_core_audio_driver_settings(fluid_settings_t* settings)
{
/*   fluid_settings_register_str(settings, "audio.coreaudio.device", "default", 0, NULL, NULL); */
}

/*
 * new_fluid_core_audio_driver
 */
fluid_audio_driver_t*
new_fluid_core_audio_driver(fluid_settings_t* settings, fluid_synth_t* synth)
{
    return new_fluid_core_audio_driver2(settings,
                                        (fluid_audio_func_t) NULL,
                                        (void*) synth);
}

/*
 * new_fluid_core_audio_driver2
 */
fluid_audio_driver_t*
new_fluid_core_audio_driver2(fluid_settings_t* settings, fluid_audio_func_t func, void* data)
{
    FLUID_LOG(FLUID_ERR, "STARTED FUNC");
    fluid_core_audio_driver_t* dev = NULL;
    UInt32 size;
    OSStatus status;
    
    dev = FLUID_NEW(fluid_core_audio_driver_t);
    if (dev == NULL) {
        FLUID_LOG(FLUID_ERR, "Out of memory");
        return NULL;
    }
    FLUID_MEMSET(dev, 0, sizeof(fluid_core_audio_driver_t));
    
    dev->callback = func;
    dev->data = data;
    
    // Describe audio component
    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    
    // Get component
    AudioComponent inputComponent = AudioComponentFindNext(NULL, &desc);
    
    // Get audio units
    status = AudioComponentInstanceNew(inputComponent, &dev->id);
    if (status != noErr) {
        FLUID_LOG(FLUID_ERR, "Failed to get audio unit");
        goto error_recovery;
    }
    
    // Enable IO for playback
    UInt32 flag = 1;
    status = AudioUnitSetProperty(dev->id,
                                  kAudioOutputUnitProperty_EnableIO,
                                  kAudioUnitScope_Output,
                                  kOutputBus,
                                  &flag,
                                  sizeof(flag));
    if (status != noErr) {
        FLUID_LOG(FLUID_ERR, "Failed to enable io for playback");
        goto error_recovery;
    }
    
    // Describe format
    dev->format.mSampleRate            = 44100.00;
    dev->format.mFormatID            = kAudioFormatLinearPCM;
    dev->format.mFormatFlags        = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    dev->format.mFramesPerPacket    = 1;
    dev->format.mChannelsPerFrame    = 2;
    dev->format.mBitsPerChannel        = 16;
    dev->format.mBytesPerPacket        = 4;
    dev->format.mBytesPerFrame        = 4;
    
    status = AudioUnitSetProperty(dev->id,
                                  kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input,
                                  kOutputBus,
                                  &dev->format,
                                  sizeof(dev->format));
    if (status != noErr) {
        FLUID_LOG(FLUID_ERR, "Failed to set stream format");
        goto error_recovery;
    }
    
    dev->buffer_size = 1024;
    dev->buffers[0] = FLUID_ARRAY(float, dev->buffer_size);
    dev->buffers[1] = FLUID_ARRAY(float, dev->buffer_size);
    
    // Set output callback
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = playbackCallback;
    callbackStruct.inputProcRefCon = (void*) dev;
    status = AudioUnitSetProperty(dev->id,
                                  kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Global,
                                  kOutputBus,
                                  &callbackStruct,
                                  sizeof(callbackStruct));
    if (status != noErr) {
        FLUID_LOG(FLUID_ERR, "Failed to set output callback");
        goto error_recovery;
    }
    
    // Initialise
    status = AudioUnitInitialize(dev->id);
    if (status != noErr) {
        FLUID_LOG(FLUID_ERR, "Failed to init audio unit");
        goto error_recovery;
    }
    
    status = AudioOutputUnitStart(dev->id);
    if (status != noErr) {
        FLUID_LOG(FLUID_ERR, "Failed to start audio unit");
        goto error_recovery;
    }
    
    return (fluid_audio_driver_t*) dev;
    
error_recovery:
    
    delete_fluid_core_audio_driver((fluid_audio_driver_t*) dev);
    return NULL;
}

/*
 * delete_fluid_core_audio_driver
 */
int
delete_fluid_core_audio_driver(fluid_audio_driver_t* p)
{
    fluid_core_audio_driver_t* dev = (fluid_core_audio_driver_t*) p;
    
    if (dev == NULL) {
        return FLUID_OK;
    }
    
    if (AudioOutputUnitStop(dev->id) != noErr) {
        FLUID_LOG(FLUID_ERR, "Failed to stop audio unit");
    }
    
    AudioUnitUninitialize(dev->id);
    
    if (dev->buffers[0]) {
        FLUID_FREE(dev->buffers[0]);
    }
    if (dev->buffers[1]) {
        FLUID_FREE(dev->buffers[1]);
    }
    
    FLUID_FREE(dev);
    
    return FLUID_OK;
}

OSStatus playbackCallback(void *inRefCon,
                                 AudioUnitRenderActionFlags *ioActionFlags,
                                 const AudioTimeStamp *inTimeStamp,
                                 UInt32 inBusNumber,
                                 UInt32 inNumberFrames,
                                 AudioBufferList *ioData) {
    // Notes: ioData contains buffers (may be more than one!)
    // Fill them up as much as you can. Remember to set the size value in each buffer to match how
    // much data is in the buffer.
    int i, k;
    fluid_core_audio_driver_t* dev = (fluid_core_audio_driver_t*) inRefCon;
    int len = my_min(dev->buffer_size, ioData->mBuffers[0].mDataByteSize / dev->format.mBytesPerFrame);
    short* buffer = (short *)ioData->mBuffers[0].mData;
    
    if (dev->callback)
    {
        float* left = dev->buffers[0];
        float* right = dev->buffers[1];
        
        (*dev->callback)(dev->data, len, 0, NULL, 2, dev->buffers);
        
        for (i = 0, k = 0; i < len; i++) {
            buffer[k++] = (short)left[i];
            buffer[k++] = (short)right[i];
        }
    }
    else fluid_synth_write_s16((fluid_synth_t*) dev->data, len, buffer, 0, 2,
                                 buffer, 1, 2);

    // set ioData values
    ioData->mNumberBuffers = 1;
    ioData->mBuffers[0].mNumberChannels = 2;
    ioData->mBuffers[0].mDataByteSize = len * dev->format.mBytesPerFrame;

    return noErr;
}


#endif /* #if COREAUDIO_SUPPORT */
