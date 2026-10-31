/* M4：语音播报纯逻辑。契约：docs/contracts/C04-audio-output.md §3。 */
#include "pet_audio_policy.h"

bool pet_audio_should_play(pet_sound_t sound, bool muted)
{
    if (muted) {
        return false;
    }
    return sound == PET_SOUND_INPUT || sound == PET_SOUND_DONE || sound == PET_SOUND_FAILED;
}

pet_audio_asset_t pet_audio_asset_for(pet_sound_t sound)
{
    switch (sound) {
    case PET_SOUND_INPUT:
        return PET_AUDIO_ASSET_INPUT;
    case PET_SOUND_DONE:
        return PET_AUDIO_ASSET_DONE;
    case PET_SOUND_FAILED:
        return PET_AUDIO_ASSET_FAILED;
    default:
        return PET_AUDIO_ASSET_NONE;
    }
}

const char *pet_audio_asset_name(pet_audio_asset_t asset)
{
    switch (asset) {
    case PET_AUDIO_ASSET_INPUT:
        return "input";
    case PET_AUDIO_ASSET_DONE:
        return "done";
    case PET_AUDIO_ASSET_FAILED:
        return "failed";
    default:
        return "none";
    }
}
