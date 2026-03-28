#pragma once
#include <AL/al.h>
#include <AL/alc.h>
#include <unordered_map>
#include <string>
#include "block.h"

struct SoundSet
{
    std::vector<ALuint> breakSounds;
    std::vector<ALuint> placeSounds;
};

class Audio
{
public:
    static void Init();
    static void Shutdown();

    static ALuint LoadWav(const std::string& path);
    static void Play3D(ALuint buffer, float x, float y, float z);

    static void LoadBlockSounds();

    static void PlayBlockBreak(BlockType type, float x, float y, float z);

    static void SetListener(float x, float y, float z,
        float fx, float fy, float fz,
        float ux, float uy, float uz);

private:
    static ALCdevice* device;
    static ALCcontext* context;

    static const int MAX_SOURCES = 32;
    static ALuint sources[MAX_SOURCES];
    static int currentSource;

    static std::unordered_map<BlockType, SoundSet> blockSounds;
};