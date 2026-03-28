#include "audio.h"
#include <fstream>
#include <vector>
#include <iostream>

ALCdevice* Audio::device = nullptr;
ALCcontext* Audio::context = nullptr;
ALuint Audio::sources[MAX_SOURCES];
int Audio::currentSource = 0;
std::unordered_map<BlockType, SoundSet> Audio::blockSounds;

void Audio::Init()
{
    device = alcOpenDevice(nullptr);
    context = alcCreateContext(device, nullptr);
    alcMakeContextCurrent(context);

    alGenSources(MAX_SOURCES, sources);
}

void Audio::Shutdown()
{
    alDeleteSources(MAX_SOURCES, sources);
    alcDestroyContext(context);
    alcCloseDevice(device);
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
    // GRASS
    blockSounds[GRASS].breakSounds = {
        LoadWav("Assets/sounds/grass/break1.wav"),
        LoadWav("Assets/sounds/grass/break2.wav")
    };

    // DIRT
    blockSounds[DIRT] = blockSounds[GRASS];

    // STONE
    blockSounds[STONE].breakSounds = {
        LoadWav("Assets/sounds/stone/break1.wav"),
        LoadWav("Assets/sounds/stone/break2.wav"),
        LoadWav("Assets/sounds/stone/break3.wav"),
        LoadWav("Assets/sounds/stone/break4.wav")
    };

    // WOOD (planks + log)
    blockSounds[OAK_PLANKS].breakSounds = {
        LoadWav("Assets/sounds/wood/break1.wav"),
        LoadWav("Assets/sounds/wood/break2.wav")
    };
    blockSounds[OAK_LOG] = blockSounds[OAK_PLANKS];

    // GLASS
    blockSounds[GLASS].breakSounds = {
        LoadWav("Assets/sounds/glass/break1.wav"),
        LoadWav("Assets/sounds/glass/break2.wav"),
        LoadWav("Assets/sounds/glass/break3.wav")
    };

    // SAND
    blockSounds[SAND].breakSounds = {
        LoadWav("Assets/sounds/sand/break1.wav"),
        LoadWav("Assets/sounds/sand/break2.wav"),
        LoadWav("Assets/sounds/sand/break3.wav"),
        LoadWav("Assets/sounds/sand/break4.wav")
    };

    // SNOW
    blockSounds[SNOW].breakSounds = {
        LoadWav("Assets/sounds/snow/break1.wav"),
        LoadWav("Assets/sounds/snow/break2.wav"),
        LoadWav("Assets/sounds/snow/break3.wav"),
        LoadWav("Assets/sounds/snow/break4.wav")
    };

    // COBBLESTONE = stone
    blockSounds[COBBLESTONE] = blockSounds[STONE];

    // OAK_LEAVES
    blockSounds[OAK_LEAVES].breakSounds = {
        LoadWav("Assets/sounds/plant/break1.wav")
    };

    // POOP
    blockSounds[POOP].breakSounds = {
        LoadWav("Assets/sounds/poop/break1.wav")
    };
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