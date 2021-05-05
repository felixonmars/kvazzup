#include "speexdsp.h"

// this is how many frames the audio capture seems to send

#include <QSettings>

#include "common.h"
#include "settingskeys.h"
#include "global.h"


bool PREPROCESSOR = true;

// I tested this to be the best or at least close enough
// this is also recommended by speex documentation
// if you are in a large room, optimal time may be larger.
const int REVERBERATION_TIME_MS = 100;

SpeexDSP::SpeexDSP(QAudioFormat format):
  format_(format),
  samplesPerFrame_(format.sampleRate()/AUDIO_FRAMES_PER_SECOND),
  preprocessor_(nullptr),
  echo_state_(nullptr),
  echoSize_(0),
  echoSample_(nullptr)
{
  init();
}


void SpeexDSP::updateSettings()
{
  if (PREPROCESSOR && preprocessor_ != nullptr)
  {
    QSettings settings(settingsFile, settingsFileFormat);

    echoMutex_.lock();

    if (settings.value(SettingsKey::audioAEC) == 1)
    {
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_ECHO_STATE, echo_state_);

      // these are the default values
      int* suppression = new int(-40);
      speex_preprocess_ctl(preprocessor_,
                           SPEEX_PREPROCESS_SET_ECHO_SUPPRESS,
                           suppression);

      *suppression = -15;
      speex_preprocess_ctl(preprocessor_,
                           SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE,
                           suppression);

      delete suppression;
    }
    else
    {
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_ECHO_STATE, nullptr);
    }

    int* activeState = new int(1);
    int* inactiveState = new int(0);

    if (settings.value(SettingsKey::audioDenoise) == 1)
    {
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_DENOISE, activeState);
    }
    else
    {
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_DENOISE, inactiveState);
    }

    if (settings.value(SettingsKey::audioDereverb) == 1)
    {
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_DEREVERB, activeState);
    }
    else
    {
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_AGC, inactiveState);
    }

    if (settings.value(SettingsKey::audioAGC) == 1)
    {
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_AGC, activeState);
    }
    else
    {
      speex_preprocess_ctl(preprocessor_, SPEEX_PREPROCESS_SET_AGC, inactiveState);
    }

    delete activeState;
    delete inactiveState;

    echoMutex_.unlock();
  }
}


void SpeexDSP::init()
{
  if (preprocessor_ != nullptr || echo_state_ != nullptr)
  {
    cleanup();
  }

  echoMutex_.lock();

  // should be around 1/3 of the room reverberation time
  uint16_t echoFilterLength = format_.sampleRate()*REVERBERATION_TIME_MS/1000;

  printNormal(this, "Initiating echo frame processing", {"Filter length"}, {
                QString::number(echoFilterLength)});

  if(format_.channelCount() > 1)
  {
    echo_state_ = speex_echo_state_init_mc(samplesPerFrame_,
                                           echoFilterLength,
                                           format_.channelCount(),
                                           format_.channelCount());
  }
  else
  {
    echo_state_ = speex_echo_state_init(samplesPerFrame_, echoFilterLength);
  }

  if (echoSample_ != nullptr)
  {
    delete echoSample_;
    echoSample_ = nullptr;
    echoSize_ = 0;
  }

  echoSize_ = samplesPerFrame_*format_.bytesPerFrame();
  echoSample_ = createEmptyFrame(echoSize_);

  if (PREPROCESSOR)
  {
    preprocessor_ = speex_preprocess_state_init(samplesPerFrame_,
                                                format_.sampleRate());
  }

  echoMutex_.unlock();

  updateSettings();
}


void SpeexDSP::cleanup()
{
  echoMutex_.lock();

  if (preprocessor_ != nullptr)
  {
    speex_preprocess_state_destroy(preprocessor_);
    preprocessor_ = nullptr;
  }

  if (echo_state_ != nullptr)
  {
    speex_echo_state_destroy(echo_state_);
    echo_state_ = nullptr;
  }

  echoMutex_.unlock();
}


std::unique_ptr<uchar[]> SpeexDSP::processInputFrame(std::unique_ptr<uchar[]> input,
                                                     uint32_t dataSize)
{
  // The audiocapturefilter makes sure the frames are the correct (samplesPerFrame_) size.
  if (dataSize != samplesPerFrame_*format_.bytesPerFrame())
  {
    printProgramError(this, "Wrong size of input frame for DSP input");
    return nullptr;
  }

  echoMutex_.lock();

  if (echoSample_ != nullptr)
  {
    // do not know if this is allowed, but it saves a copy
    int16_t* pcmOutput = (int16_t*)input.get();
    speex_echo_cancellation(echo_state_,
                            (int16_t*)input.get(),
                            (int16_t*)echoSample_, pcmOutput);
  }

  echoMutex_.unlock();

  // Do preprocess trickery defined in init for input.
  // In my understanding preprocessor is run after echo cancellation for some reason.
  if(preprocessor_ != nullptr)
  {
    speex_preprocess_run(preprocessor_, (int16_t*)input.get());
  }

  return input;
}


void SpeexDSP::processEchoFrame(uint8_t* echo,
                                uint32_t dataSize)
{
  // TODO: This should prepare for different size of frames in case since they
  // are not generated by us
  if (dataSize != samplesPerFrame_*format_.bytesPerFrame())
  {
    printPeerError(this, "Wrong size of echo frame for DSP. DSP will not operate");
    return;
  }

  echoMutex_.lock();

  if (echoSample_ == nullptr || echoSize_ != dataSize)
  {
    if (echoSample_ != nullptr)
    {
      delete echoSample_;
      echoSample_ = nullptr;
      echoSize_ = 0;
    }

    echoSize_ = dataSize;
    echoSample_ = createEmptyFrame(echoSize_);
  }
  echoSize_ = dataSize;

  // I don't like this memcpy, but visual studio had some problems with smart pointers.
  memcpy(echoSample_, echo, dataSize);
  echoMutex_.unlock();
}


uint8_t* SpeexDSP::createEmptyFrame(uint32_t size)
{
  uint8_t* emptyFrame  = new uint8_t[size];
  memset(emptyFrame, 0, size);
  return emptyFrame;
}
