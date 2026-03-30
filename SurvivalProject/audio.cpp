#include "audio.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

ALCdevice* Audio::device = nullptr;
ALCcontext* Audio::context = nullptr;
ALuint Audio::sources[MAX_SOURCES];
int Audio::currentSource = 0;
std::unordered_map<BlockType, SoundSet> Audio::blockSounds;

std::vector<ALuint> Audio::LoadWavsFromFolder(const std::string& folder)
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
            [](unsigned char c) { return (unsigned char)std::tolower(c); });

        if (ext == ".wav")
            files.push_back(entry.path());
    }

    std::sort(files.begin(), files.end());

    for (const auto& file : files)
    {
        ALuint buffer = LoadWav(file.string());
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
    }
    blockSounds.clear();

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

// ПРОСТОЙ WAV LOADER (PCM only)
ALuint Audio::LoadWav(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return 0;

    file.seekg(22);
    short channels;
    file.read((char*)&channels, 2);

    int sampleRate;
    file.read((char*)&sampleRate, 4);

    file.seekg(34);
    short bitsPerSample;
    file.read((char*)&bitsPerSample, 2);

    file.seekg(40);
    int dataSize;
    file.read((char*)&dataSize, 4);

    std::vector<char> data(dataSize);
    file.read(data.data(), dataSize);

    ALenum format = 0;
    if (channels == 1 && bitsPerSample == 16) format = AL_FORMAT_MONO16;
    if (channels == 2 && bitsPerSample == 16) format = AL_FORMAT_STEREO16;

    ALuint buffer;
    alGenBuffers(1, &buffer);
    alBufferData(buffer, format, data.data(), dataSize, sampleRate);

    return buffer;
}

void Audio::Play3D(ALuint buffer, float x, float y, float z)
{
    ALuint src = sources[currentSource];
    currentSource = (currentSource + 1) % MAX_SOURCES;

    alSourcei(src, AL_BUFFER, buffer);
    alSource3f(src, AL_POSITION, x, y, z);

    //float pitch = 0.9f + (rand() % 20) / 100.0f;
    float pitch = 1.0f;
    alSourcef(src, AL_PITCH, pitch);

    alSourcef(src, AL_REFERENCE_DISTANCE, 2.0f);
    alSourcef(src, AL_MAX_DISTANCE, 25.0f);

    alSourcef(src, AL_GAIN, 1.0f);

    alSourcePlay(src);
}

void Audio::LoadBlockSounds()
{
    blockSounds[GRASS].breakSounds       = LoadWavsFromFolder("Assets/sounds/grass");
    blockSounds[DIRT].breakSounds        = LoadWavsFromFolder("Assets/sounds/dirt");
    blockSounds[STONE].breakSounds       = LoadWavsFromFolder("Assets/sounds/stone");
    blockSounds[OAK_PLANKS].breakSounds  = LoadWavsFromFolder("Assets/sounds/plank");
    blockSounds[OAK_LOG].breakSounds     = LoadWavsFromFolder("Assets/sounds/log");
    blockSounds[GLASS].breakSounds       = LoadWavsFromFolder("Assets/sounds/glass");
    blockSounds[SAND].breakSounds        = LoadWavsFromFolder("Assets/sounds/sand");
    blockSounds[SNOW].breakSounds        = LoadWavsFromFolder("Assets/sounds/snow");
    blockSounds[COBBLESTONE].breakSounds = LoadWavsFromFolder("Assets/sounds/cobblestone");
    blockSounds[BASALT].breakSounds      = LoadWavsFromFolder("Assets/sounds/basalt");
    blockSounds[BEDROCK].breakSounds     = LoadWavsFromFolder("Assets/sounds/bedrock");
    blockSounds[OAK_LEAVES].breakSounds  = LoadWavsFromFolder("Assets/sounds/plant");
    blockSounds[POOP].breakSounds        = LoadWavsFromFolder("Assets/sounds/poop");
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