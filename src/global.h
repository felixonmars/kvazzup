#pragma once

// A place for program defines and constants. See common.h for common functions.

#include <QString>

const QString APPLICATIONNAME = "Kvazzup";

// how often registrations are sent in seconds
const int REGISTER_INTERVAL = 600;

// this affects latency of audio. We have to wait until this much audio
// has arrived before sending the packet. If packet is too small,
// we waste bandwidth.

// With a value of 100, one audio frame constitutes 10 ms of audio samples.
// It is recommended to keep the audio frame size relatively small to avoid 
// unnecessary latency.
const uint16_t AUDIO_FRAMES_PER_SECOND = 100;

const int STREAM_COMPONENTS = 4;

// this macro checks the condition and quits in debug mode and exits the current function in
#define CHECKERROR(condition, errorString, errorReturnValue) \
  Q_ASSERT(condition); \
  if(!condition) \
  { \
    qCritical().noSpace() << __FILE__ << ":" __LINE__ << "ERROR:" << errorstring;\
    return errorReturnValue; \
  }

#ifndef __cplusplus
#error A C++ compiler is required!
#endif

