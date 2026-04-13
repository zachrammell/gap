#pragma once

#include <memory>
#include <span>
#include <string_view>

#include "config.h"

namespace Feed
{
    class MessageFeed;
} // namespace Feed

namespace Audio
{
    enum class Sound
    {
        Keystroke,
        ReturnKey,
        Count
    };

    using DefaultSoundEffects = std::span<const std::string_view>;

    DefaultSoundEffects default_audio_keystroke();
    DefaultSoundEffects default_audio_return();

    class AudioEngine
    {
    public:
        struct Data;

        AudioEngine();
        ~AudioEngine();

        // Initialization.
        bool init(std::string_view asset_core_path, const Config::SystemEffects& system_effects, Feed::MessageFeed* feed);

        // Interaction.
        void queue(Sound sound);
        void mute(bool b);

    private:
        std::unique_ptr<Data> data;
    };
} // namespace Audio