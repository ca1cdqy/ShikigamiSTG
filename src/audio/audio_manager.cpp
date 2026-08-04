#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <shiki/audio/audio_manager.h>
#include <spdlog/spdlog.h>

// OpenAL headers
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#ifndef AL_LOOP_POINTS_SOFT
#define AL_LOOP_POINTS_SOFT 0x2015
#endif

namespace shiki {

struct WavHeader {
  char riff[4];
  uint32_t size;
  char wave[4];
  char fmt[4];
  uint32_t fmtSize;
  uint16_t audioFormat;
  uint16_t numChannels;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
};

AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::initialize() {
  if (initialized_) {
    return true;
  }

  device_ = alcOpenDevice(nullptr);
  if (!device_) {
    spdlog::error("Failed to open OpenAL audio device");
    return false;
  }

  context_ = alcCreateContext(static_cast<ALCdevice *>(device_), nullptr);
  if (!context_) {
    spdlog::error("Failed to create OpenAL context");
    alcCloseDevice(static_cast<ALCdevice *>(device_));
    device_ = nullptr;
    return false;
  }

  alcMakeContextCurrent(static_cast<ALCcontext *>(context_));

  alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
  alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);

  initialized_ = true;
  spdlog::info("AudioManager initialized (OpenAL-soft)");
  return true;
}

void AudioManager::shutdown() {
  if (!initialized_) {
    return;
  }

  for (auto &[name, sound] : sounds_) {
    if (sound.source != 0) {
      alSourceStop(sound.source);
      alDeleteSources(1, &sound.source);
    }
    if (sound.buffer != 0) {
      alDeleteBuffers(1, &sound.buffer);
    }
  }
  sounds_.clear();

  if (musicSource_ != 0) {
    alSourceStop(musicSource_);
    alDeleteSources(1, &musicSource_);
    musicSource_ = 0;
  }

  for (auto &[name, music] : musics_) {
    if (music.buffer != 0) {
      alDeleteBuffers(1, &music.buffer);
    }
  }
  musics_.clear();

  if (context_) {
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(static_cast<ALCcontext *>(context_));
    context_ = nullptr;
  }

  if (device_) {
    alcCloseDevice(static_cast<ALCdevice *>(device_));
    device_ = nullptr;
  }

  initialized_ = false;
  spdlog::info("AudioManager shutdown");
}

bool AudioManager::loadWavFile(const std::string &path, ALuint &buffer) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    spdlog::error("Failed to open WAV file: {}", path);
    return false;
  }

  WavHeader header;
  file.read(reinterpret_cast<char *>(&header), sizeof(header));

  if (std::strncmp(header.riff, "RIFF", 4) != 0 ||
      std::strncmp(header.wave, "WAVE", 4) != 0) {
    spdlog::error("Invalid WAV file format: {}", path);
    return false;
  }

  char chunkId[4];
  uint32_t chunkSize;
  bool foundData = false;

  while (file.read(chunkId, 4)) {
    file.read(reinterpret_cast<char *>(&chunkSize), sizeof(chunkSize));

    if (std::strncmp(chunkId, "data", 4) == 0) {
      foundData = true;
      break;
    }

    file.seekg(chunkSize, std::ios::cur);
  }

  if (!foundData) {
    spdlog::error("No data chunk found in WAV file: {}", path);
    return false;
  }

  std::vector<char> data(chunkSize);
  file.read(data.data(), chunkSize);
  file.close();

  ALenum format;
  if (header.numChannels == 1) {
    format = (header.bitsPerSample == 8) ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
  } else {
    format =
        (header.bitsPerSample == 8) ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
  }

  alGenBuffers(1, &buffer);
  alBufferData(buffer, format, data.data(), static_cast<ALsizei>(chunkSize),
               static_cast<ALsizei>(header.sampleRate));

  ALenum error = alGetError();
  if (error != AL_NO_ERROR) {
    spdlog::error("OpenAL error loading WAV: {}", error);
    alDeleteBuffers(1, &buffer);
    return false;
  }

  return true;
}

bool AudioManager::loadSound(const std::string &name, const std::string &path) {
  if (!initialized_) {
    spdlog::warn("AudioManager not initialized, cannot load sound");
    return false;
  }

  ALuint buffer = 0;
  if (!loadWavFile(path, buffer)) {
    return false;
  }

  SoundData sound;
  sound.path = path;
  sound.buffer = buffer;
  sound.loaded = true;

  sounds_[name] = sound;
  spdlog::info("Sound loaded: {} from {}", name, path);
  return true;
}

void AudioManager::playSound(const std::string &name, float gain) {
  if (!initialized_) {
    return;
  }

  auto it = sounds_.find(name);
  if (it == sounds_.end()) {
    spdlog::warn("Sound not found: {}", name);
    return;
  }

  SoundData &sound = it->second;
  sound.sourceGain = std::clamp(gain, 0.0f, 1.0f);

  if (sound.source != 0) {
    ALint state;
    alGetSourcei(sound.source, AL_SOURCE_STATE, &state);
    if (state == AL_PLAYING) {
      alSourceStop(sound.source);
    }
    alDeleteSources(1, &sound.source);
  }

  alGenSources(1, &sound.source);
  alSourcei(sound.source, AL_BUFFER, static_cast<ALint>(sound.buffer));
  alSourcef(sound.source, AL_GAIN, soundVolume_ * sound.sourceGain);
  alSource3f(sound.source, AL_POSITION, 0.0f, 0.0f, 0.0f);

  alSourcePlay(sound.source);

  ALenum error = alGetError();
  if (error != AL_NO_ERROR) {
    spdlog::error("OpenAL error playing sound: {}", error);
  }
}

bool AudioManager::loadMusic(const std::string &name, const std::string &path) {
  if (!initialized_) {
    spdlog::warn("AudioManager not initialized, cannot load music");
    return false;
  }

  ALuint buffer = 0;
  if (!loadWavFile(path, buffer)) {
    return false;
  }

  SoundData music;
  music.path = path;
  music.buffer = buffer;
  music.loaded = true;

  musics_[name] = music;
  spdlog::info("Music loaded: {} from {}", name, path);
  return true;
}

bool AudioManager::setMusicLoopPoints(const std::string &name,
                                      int64_t startSample, int64_t endSample) {
  if (!initialized_ || !alIsExtensionPresent("AL_SOFT_loop_points")) {
    spdlog::warn("OpenAL loop-point extension is unavailable");
    return false;
  }

  auto it = musics_.find(name);
  if (it == musics_.end() || startSample < 0 || endSample <= startSample ||
      endSample > std::numeric_limits<ALint>::max()) {
    return false;
  }

  ALint size = 0;
  ALint channels = 0;
  ALint bits = 0;
  alGetBufferi(it->second.buffer, AL_SIZE, &size);
  alGetBufferi(it->second.buffer, AL_CHANNELS, &channels);
  alGetBufferi(it->second.buffer, AL_BITS, &bits);
  const int bytesPerFrame = channels * bits / 8;
  if (bytesPerFrame <= 0 || endSample > size / bytesPerFrame)
    return false;

  const ALint points[2] = {static_cast<ALint>(startSample),
                           static_cast<ALint>(endSample)};
  alBufferiv(it->second.buffer, AL_LOOP_POINTS_SOFT, points);
  const ALenum error = alGetError();
  if (error != AL_NO_ERROR) {
    spdlog::error("OpenAL error setting music loop points: {}", error);
    return false;
  }
  return true;
}

void AudioManager::playMusic(const std::string &name) {
  if (!initialized_) {
    return;
  }

  auto it = musics_.find(name);
  if (it == musics_.end()) {
    spdlog::warn("Music not found: {}", name);
    return;
  }

  if (musicSource_ != 0) {
    alSourceStop(musicSource_);
    alDeleteSources(1, &musicSource_);
    musicSource_ = 0;
  }

  alGenSources(1, &musicSource_);
  alSourcei(musicSource_, AL_BUFFER, static_cast<ALint>(it->second.buffer));
  alSourcef(musicSource_, AL_GAIN, musicVolume_);
  alSourcei(musicSource_, AL_LOOPING, AL_TRUE);

  alSourcePlay(musicSource_);
  currentMusic_ = name;

  ALenum error = alGetError();
  if (error != AL_NO_ERROR) {
    spdlog::error("OpenAL error playing music: {}", error);
  }
}

void AudioManager::stopMusic() {
  if (musicSource_ != 0) {
    alSourceStop(musicSource_);
  }
}

void AudioManager::pauseMusic() {
  if (musicSource_ != 0) {
    alSourcePause(musicSource_);
  }
}

void AudioManager::resumeMusic() {
  if (musicSource_ != 0) {
    alSourcePlay(musicSource_);
  }
}

void AudioManager::setSoundVolume(float volume) {
  soundVolume_ = std::clamp(volume, 0.0f, 1.0f);
  for (auto &[name, sound] : sounds_) {
    (void)name;
    if (sound.source != 0) {
      alSourcef(sound.source, AL_GAIN, soundVolume_ * sound.sourceGain);
    }
  }
}

void AudioManager::setMusicVolume(float volume) {
  musicVolume_ = std::clamp(volume, 0.0f, 1.0f);
  if (musicSource_ != 0) {
    alSourcef(musicSource_, AL_GAIN, musicVolume_);
  }
}

bool AudioManager::isMusicPlaying() const {
  if (musicSource_ == 0) {
    return false;
  }

  ALint state;
  alGetSourcei(musicSource_, AL_SOURCE_STATE, &state);
  return state == AL_PLAYING;
}

void AudioManager::checkALError(const std::string &operation) {
  ALenum error = alGetError();
  if (error != AL_NO_ERROR) {
    spdlog::error("OpenAL error during {}: {}", operation, error);
  }
}

} // namespace shiki
