#pragma once
#include <AL/al.h>
#include <AL/alc.h>
#include <unordered_map>
#include <string>
#include <vector>
#include "block.h"

struct SoundSet
{
    std::vector<ALuint> breakSounds;
    std::vector<ALuint> placeSounds;
};

struct PlayerSoundSet
{
    std::vector<ALuint> hurtSounds;
};

class Audio
{
public:
    static void Init();
    static void Shutdown();

    static ALuint LoadOgg(const std::string& path);
    static void Play3D(ALuint buffer, float x, float y, float z);
    static void Play2D(ALuint buffer);

    static void LoadSounds();

    static void PlayBlockBreak(BlockType type, float x, float y, float z);

    static void SetListener(float x, float y, float z,
        float fx, float fy, float fz,
        float ux, float uy, float uz);

    static void PlayPlayerHurt();

private:
    static ALCdevice* device;
    static ALCcontext* context;

    static const int MAX_SOURCES = 32;
    static ALuint sources[MAX_SOURCES];
    static int currentSource;

    static std::unordered_map<BlockType, SoundSet> blockSounds;

    static std::vector<ALuint> LoadOggsFromFolder(const std::string& folder);

    static PlayerSoundSet playerSounds;
};