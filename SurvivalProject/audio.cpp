#include "audio.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include "stb_vorbis.c"

namespace fs = std::filesystem;

ALCdevice* Audio::device = nullptr;
ALCcontext* Audio::context = nullptr;
ALuint Audio::sources[MAX_SOURCES];
int Audio::currentSource = 0;
std::unordered_map<BlockType, SoundSet> Audio::blockSounds;

PlayerSoundSet Audio::playerSounds;

std::vector<ALuint> Audio::LoadOggsFromFolder(const std::string& folder)
{
    std::vector<ALuint> result;

    if (!fs::exists(folder) || !fs::is_directory(folder))
        return result;

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(folder))
    {
        if (!entry.is_regular_file())
            continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });

        if (ext == ".ogg")
            files.push_back(entry.path());
    }

    std::sort(files.begin(), files.end());

    for (const auto& file : files)
    {
        ALuint buffer = LoadOgg(file.string());
        if (buffer != 0)
            result.push_back(buffer);
    }

    return result;
}

void Audio::Init()
{
    device = alcOpenDevice(nullptr);
    context = alcCreateContext(device, nullptr);
    alcMakeContextCurrent(context);

    alGenSources(MAX_SOURCES, sources);
}

void Audio::Shutdown()
{
    // удалить все буферы звуков
    for (auto& [type, set] : blockSounds)
    {
        if (!set.breakSounds.empty())
            alDeleteBuffers((ALsizei)set.breakSounds.size(), set.breakSounds.data());

        if (!set.placeSounds.empty())
            alDeleteBuffers((ALsizei)set.placeSounds.size(), set.placeSounds.data());
    }

    blockSounds.clear();

    if (!playerSounds.hurtSounds.empty())
        alDeleteBuffers((ALsizei)playerSounds.hurtSounds.size(),
            playerSounds.hurtSounds.data());

    alDeleteSources(MAX_SOURCES, sources);

    alcMakeContextCurrent(nullptr);
    if (context) alcDestroyContext(context);
    if (device) alcCloseDevice(device);

    context = nullptr;
    device = nullptr;
    currentSource = 0;
}

void Audio::SetListener(float x, float y, float z,
    float fx, float fy, float fz,
    float ux, float uy, float uz)
{
    alListener3f(AL_POSITION, x, y, z);

    float ori[] = { fx, fy, fz, ux, uy, uz };
    alListenerfv(AL_ORIENTATION, ori);
}

ALuint Audio::LoadOgg(const std::string& path)
{
    int channels = 0;
    int sampleRate = 0;
    short* output = nullptr;

    int samples = stb_vorbis_decode_filename(path.c_str(), &channels, &sampleRate, &output);
    if (samples < 0 || !output)
        return 0;

    ALenum format = 0;
    if (channels == 1) format = AL_FORMAT_MONO16;
    else if (channels == 2) format = AL_FORMAT_STEREO16;
    else
    {
        std::free(output);
        return 0;
    }

    ALuint buffer;
    alGenBuffers(1, &buffer);
    alBufferData(buffer, format, output, samples * channels * (ALsizei)sizeof(short), sampleRate);

    std::free(output);
    return buffer;
}

void Audio::Play3D(ALuint buffer, float x, float y, float z)
{
    ALuint src = sources[currentSource];
    currentSource = (currentSource + 1) % MAX_SOURCES;

    alSourcei(src, AL_BUFFER, buffer);
    alSourcei(src, AL_SOURCE_RELATIVE, AL_FALSE);
    alSourcef(src, AL_ROLLOFF_FACTOR, 1.0f);
    alSource3f(src, AL_POSITION, x, y, z);

    float pitch = 0.9f + (rand() % 20) / 100.0f;
    alSourcef(src, AL_PITCH, pitch);

    alSourcef(src, AL_REFERENCE_DISTANCE, 2.0f);
    alSourcef(src, AL_MAX_DISTANCE, 25.0f);

    alSourcef(src, AL_GAIN, 1.0f);

    alSourcePlay(src);
}

void Audio::Play2D(ALuint buffer)
{
    ALuint src = sources[currentSource];
    currentSource = (currentSource + 1) % MAX_SOURCES;

    alSourcei(src, AL_BUFFER, buffer);
    alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);  // координаты относительно слушателя
    alSource3f(src, AL_POSITION, 0.0f, 0.0f, 0.0f); // прямо в центре головы

    float pitch = 0.9f + (rand() % 20) / 100.0f;
    alSourcef(src, AL_PITCH, pitch);
    alSourcef(src, AL_ROLLOFF_FACTOR, 0.0f); // отключаем затухание по расстоянию
    alSourcef(src, AL_GAIN, 1.0f);

    alSourcePlay(src);
}

void Audio::LoadSounds()
{
    // Blocks
    blockSounds[GRASS].breakSounds       = LoadOggsFromFolder("Assets/sounds/grass");
    blockSounds[DIRT].breakSounds        = LoadOggsFromFolder("Assets/sounds/dirt");
    blockSounds[STONE].breakSounds       = LoadOggsFromFolder("Assets/sounds/stone");
    blockSounds[OAK_PLANKS].breakSounds  = LoadOggsFromFolder("Assets/sounds/plank");
    blockSounds[OAK_LOG].breakSounds     = LoadOggsFromFolder("Assets/sounds/log");
    blockSounds[GLASS].breakSounds       = LoadOggsFromFolder("Assets/sounds/glass");
    blockSounds[SAND].breakSounds        = LoadOggsFromFolder("Assets/sounds/sand");
    blockSounds[SNOW].breakSounds        = LoadOggsFromFolder("Assets/sounds/snow");
    blockSounds[COBBLESTONE].breakSounds = LoadOggsFromFolder("Assets/sounds/cobblestone");
    blockSounds[BASALT].breakSounds      = LoadOggsFromFolder("Assets/sounds/basalt");
    blockSounds[BEDROCK].breakSounds     = LoadOggsFromFolder("Assets/sounds/bedrock");
    blockSounds[OAK_LEAVES].breakSounds  = LoadOggsFromFolder("Assets/sounds/plant");
    blockSounds[POOP].breakSounds        = LoadOggsFromFolder("Assets/sounds/poop");
    blockSounds[TALL_GRASS].breakSounds  = LoadOggsFromFolder("Assets/sounds/plant");

    // Player
    playerSounds.hurtSounds = LoadOggsFromFolder("Assets/sounds/player/hurt");
}

void Audio::PlayBlockBreak(BlockType type, float x, float y, float z)
{
    auto it = blockSounds.find(type);
    if (it == blockSounds.end()) return;

    auto& sounds = it->second.breakSounds;
    if (sounds.empty()) return;

    int index = rand() % sounds.size();
    Play3D(sounds[index], x, y, z);
}

void Audio::PlayPlayerHurt()
{
    auto& sounds = playerSounds.hurtSounds;
    if (sounds.empty()) return;
    int index = rand() % sounds.size();
    Play2D(sounds[index]);
}