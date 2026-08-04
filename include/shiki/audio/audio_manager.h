#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declaration for OpenAL
using ALuint = unsigned int;

namespace shiki {

/**
 * OpenAL Soft-backed audio manager for sound effects and music.
 *
 * Call initialize() after construction and shutdown() before destruction.
 * Sound effects are stored by name and played one-shot; music supports
 * sample-accurate loop points and pause/resume control.
 */
class AudioManager {
  public:
	/** Creates an audio manager with no initialized OpenAL context. */
	AudioManager() = default;
	/** Calls shutdown() if the manager was initialized. */
	~AudioManager();

	/** Audio managers cannot be copied because they own an OpenAL context. */
	AudioManager(const AudioManager &) = delete;
	/** Audio managers cannot be copy-assigned. */
	AudioManager &operator=(const AudioManager &) = delete;

	/** Creates the OpenAL device and context. Returns true on success. */
	bool initialize();
	/** Stops playback and releases all OpenAL resources. */
	void shutdown();

	/** Decodes a WAV file and registers it under name. Returns true on success. */
	bool loadSound(const std::string &name, const std::string &path);

	/** Plays the named sound effect once at the given linear gain. */
	void playSound(const std::string &name, float gain = 1.0f);

	/** Decodes and registers a looping OGG, MP3, or WAV track. Returns true on success. */
	bool loadMusic(const std::string &name, const std::string &path);
	/** Sets sample-accurate loop start and end points for the named music track. */
	bool setMusicLoopPoints(const std::string &name, int64_t startSample,
	                        int64_t endSample);

	/** Starts or restarts playback of the named music track. */
	void playMusic(const std::string &name);
	/** Stops music playback and resets the playback position. */
	void stopMusic();
	/** Pauses music playback without changing the current position. */
	void pauseMusic();
	/** Resumes music playback from the current position. */
	void resumeMusic();

	/** Sets the linear gain applied to all sound effects. */
	void setSoundVolume(float volume);
	/** Sets the linear gain applied to music. */
	void setMusicVolume(float volume);

	/** Returns whether a music track is currently playing. */
	bool isMusicPlaying() const;

  private:
	struct SoundData {
		std::string path;
		ALuint buffer = 0;
		ALuint source = 0;
		float sourceGain = 1.0f;
		bool loaded = false;
	};

	/// OpenAL device state
	void *device_ = nullptr;  // ALCdevice*
	void *context_ = nullptr; // ALCcontext*

	std::unordered_map<std::string, SoundData> sounds_;
	std::unordered_map<std::string, SoundData> musics_;

	/// Currently playing music
	std::string currentMusic_;
	ALuint musicSource_ = 0;

	float soundVolume_ = 1.0f;
	float musicVolume_ = 1.0f;
	bool initialized_ = false;

	/// Internal helpers
	bool loadWavFile(const std::string &path, ALuint &buffer);
	void checkALError(const std::string &operation);
};

} // namespace shiki
