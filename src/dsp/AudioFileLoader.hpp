#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Fraudize {

struct LoadedAudioBuffer {
    std::vector<float> samplesL;
    std::vector<float> samplesR;
    float sampleRate = 44100.0f;
    size_t loopStart = 0;
    size_t loopEnd = 0;
    bool valid = false;

    void clear() {
        samplesL.clear();
        samplesR.clear();
        sampleRate = 44100.0f;
        loopStart = 0;
        loopEnd = 0;
        valid = false;
    }

    size_t numFrames() const {
        return std::min(samplesL.size(), samplesR.size());
    }
};

class AudioFileLoader {
public:
    static std::wstring utf8ToWide(const std::string& s) {
        if (s.empty()) return L"";
#if defined(_WIN32)
        int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
        if (needed > 0) {
            std::wstring out(static_cast<size_t>(needed), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], needed);
            return out;
        }
#endif
        return std::wstring(s.begin(), s.end());
    }

    static bool readAllBytes(const std::string& path, std::vector<uint8_t>& outBytes) {
        outBytes.clear();
#if defined(_WIN32)
        std::wstring wPath = utf8ToWide(path);
        FILE* f = _wfopen(wPath.c_str(), L"rb");
#else
        FILE* f = std::fopen(path.c_str(), "rb");
#endif
        if (!f) return false;
        if (std::fseek(f, 0, SEEK_END) != 0) {
            std::fclose(f);
            return false;
        }
        long sz = std::ftell(f);
        if (sz <= 0 || sz > 256L * 1024L * 1024L) {
            std::fclose(f);
            return false;
        }
        std::fseek(f, 0, SEEK_SET);
        outBytes.resize(static_cast<size_t>(sz));
        size_t nRead = std::fread(outBytes.data(), 1, static_cast<size_t>(sz), f);
        std::fclose(f);
        outBytes.resize(nRead);
        return !outBytes.empty();
    }

    static bool load(const std::string& path, LoadedAudioBuffer& out) {
        out.clear();
        if (path.empty()) return false;

        std::string ext = getLowerExtension(path);
        bool ok = false;
        if (ext == ".wav" || ext == ".wave") {
            ok = loadWav(path, out);
        }
        if (!ok) {
            ok = loadViaMediaFoundation(path, out);
        }
        if (!ok || out.numFrames() == 0) {
            out.clear();
            return false;
        }

        finalizeBuffer(out);
        return out.valid;
    }

private:
    static std::string getLowerExtension(const std::string& path) {
        size_t dot = path.find_last_of('.');
        if (dot == std::string::npos) return "";
        std::string ext = path.substr(dot);
        for (char& c : ext) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        return ext;
    }

    static uint16_t readU16(const uint8_t* p) {
        return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
    }

    static uint32_t readU32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    }

    static bool loadWav(const std::string& path, LoadedAudioBuffer& out) {
        std::vector<uint8_t> bytes;
        if (!readAllBytes(path, bytes) || bytes.size() < 44) return false;

        if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
            return false;
        }

        uint16_t audioFormat = 0;
        uint16_t numChannels = 0;
        uint32_t sampleRate = 44100;
        uint16_t bitsPerSample = 0;
        bool hasFmt = false;
        size_t dataOffset = 0;
        size_t dataSize = 0;
        bool hasData = false;

        size_t pos = 12;
        while (pos + 8 <= bytes.size()) {
            const uint8_t* hdr = bytes.data() + pos;
            uint32_t chunkSize = readU32(hdr + 4);
            size_t payloadPos = pos + 8;
            size_t avail = (payloadPos <= bytes.size()) ? (bytes.size() - payloadPos) : 0;
            size_t safeSize = std::min<size_t>(chunkSize, avail);

            if (std::memcmp(hdr, "fmt ", 4) == 0 && safeSize >= 16) {
                const uint8_t* fmt = bytes.data() + payloadPos;
                audioFormat = readU16(fmt + 0);
                numChannels = readU16(fmt + 2);
                sampleRate = readU32(fmt + 4);
                bitsPerSample = readU16(fmt + 14);
                if (audioFormat == 0xFFFE && safeSize >= 40) {
                    // WAVE_FORMAT_EXTENSIBLE: first 2 bytes of SubFormat GUID hold the format tag
                    audioFormat = readU16(fmt + 24);
                }
                hasFmt = true;
            } else if (std::memcmp(hdr, "data", 4) == 0 && safeSize > 0) {
                dataOffset = payloadPos;
                dataSize = safeSize;
                hasData = true;
            }

            size_t padded = static_cast<size_t>(chunkSize) + (chunkSize & 1u);
            if (payloadPos + padded <= pos) break;
            pos = payloadPos + padded;
        }

        if (!hasFmt || !hasData || numChannels == 0 || sampleRate < 4000 || sampleRate > 384000) {
            return false;
        }
        if (audioFormat != 1 && audioFormat != 3) return false; // PCM or IEEE float

        size_t bytesPerSample = bitsPerSample / 8;
        if (bytesPerSample == 0) return false;
        size_t frameSize = bytesPerSample * numChannels;
        if (frameSize == 0) return false;

        size_t totalFrames = dataSize / frameSize;
        if (totalFrames == 0) return false;

        out.sampleRate = static_cast<float>(sampleRate);
        out.samplesL.resize(totalFrames);
        out.samplesR.resize(totalFrames);

        const uint8_t* src = bytes.data() + dataOffset;
        auto decodeSample = [&](const uint8_t* p) -> float {
            if (audioFormat == 3) {
                if (bitsPerSample == 32) {
                    float v = 0.0f;
                    std::memcpy(&v, p, 4);
                    return std::isfinite(v) ? v : 0.0f;
                }
                if (bitsPerSample == 64) {
                    double v = 0.0;
                    std::memcpy(&v, p, 8);
                    return std::isfinite(v) ? static_cast<float>(v) : 0.0f;
                }
            } else if (audioFormat == 1) {
                if (bitsPerSample == 8) {
                    return (static_cast<int>(p[0]) - 128) / 128.0f;
                }
                if (bitsPerSample == 16) {
                    int16_t v = static_cast<int16_t>(readU16(p));
                    return static_cast<float>(v) / 32768.0f;
                }
                if (bitsPerSample == 24) {
                    int32_t v = static_cast<int32_t>(p[0]) |
                                (static_cast<int32_t>(p[1]) << 8) |
                                (static_cast<int32_t>(static_cast<int8_t>(p[2])) << 16);
                    return static_cast<float>(v) / 8388608.0f;
                }
                if (bitsPerSample == 32) {
                    int32_t v = static_cast<int32_t>(readU32(p));
                    return static_cast<float>(static_cast<double>(v) / 2147483648.0);
                }
            }
            return 0.0f;
        };

        for (size_t i = 0; i < totalFrames; ++i) {
            const uint8_t* frame = src + i * frameSize;
            float l = decodeSample(frame);
            float r = (numChannels >= 2) ? decodeSample(frame + bytesPerSample) : l;
            out.samplesL[i] = l;
            out.samplesR[i] = r;
        }
        return true;
    }

    static bool loadViaMediaFoundation(const std::string& path, LoadedAudioBuffer& out) {
#if defined(_WIN32)
        HMODULE hMfPlat = LoadLibraryA("mfplat.dll");
        HMODULE hMfReadWrite = LoadLibraryA("mfreadwrite.dll");
        if (!hMfPlat || !hMfReadWrite) {
            if (hMfReadWrite) FreeLibrary(hMfReadWrite);
            if (hMfPlat) FreeLibrary(hMfPlat);
            return false;
        }

        using FnMFStartup = HRESULT(WINAPI*)(ULONG, DWORD);
        using FnMFShutdown = HRESULT(WINAPI*)();
        using FnMFCreateMediaType = HRESULT(WINAPI*)(IMFMediaType**);
        using FnMFCreateSourceReaderFromURL = HRESULT(WINAPI*)(LPCWSTR, IMFAttributes*, IMFSourceReader**);

        auto pMFStartup = reinterpret_cast<FnMFStartup>(GetProcAddress(hMfPlat, "MFStartup"));
        auto pMFShutdown = reinterpret_cast<FnMFShutdown>(GetProcAddress(hMfPlat, "MFShutdown"));
        auto pMFCreateMediaType = reinterpret_cast<FnMFCreateMediaType>(GetProcAddress(hMfPlat, "MFCreateMediaType"));
        auto pMFCreateSourceReaderFromURL = reinterpret_cast<FnMFCreateSourceReaderFromURL>(
            GetProcAddress(hMfReadWrite, "MFCreateSourceReaderFromURL")
        );

        if (!pMFStartup || !pMFShutdown || !pMFCreateMediaType || !pMFCreateSourceReaderFromURL) {
            FreeLibrary(hMfReadWrite);
            FreeLibrary(hMfPlat);
            return false;
        }

        HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool didCoInit = (hrCo == S_OK || hrCo == S_FALSE);

        bool success = false;
        if (SUCCEEDED(pMFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) {
            // Inline GUIDs so no extra -lmfuuid linkage is needed
            static const GUID kMF_MT_MAJOR_TYPE = { 0x48eba18e, 0xf8c9, 0x4687, { 0xbf, 0x11, 0x0a, 0x74, 0xc9, 0xf9, 0x6a, 0x8f } };
            static const GUID kMF_MT_SUBTYPE = { 0xf7e34c9a, 0x42e8, 0x4714, { 0xb7, 0x4b, 0xcb, 0x29, 0xd7, 0x2c, 0x35, 0xe5 } };
            static const GUID kMFMediaType_Audio = { 0x73647561, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
            static const GUID kMFAudioFormat_Float = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
            static const GUID kMF_MT_AUDIO_SAMPLES_PER_SECOND = { 0x5faeeae7, 0x0290, 0x4c31, { 0x9e, 0x8a, 0xc5, 0x34, 0xf6, 0x8d, 0x9d, 0xba } };
            static const GUID kMF_MT_AUDIO_NUM_CHANNELS = { 0x37e48bf5, 0x645e, 0x4c5b, { 0x89, 0xde, 0xad, 0xa9, 0xe2, 0x9b, 0x69, 0x6a } };
            static const GUID kMF_MT_AUDIO_BITS_PER_SAMPLE = { 0xf2deb57f, 0x40fa, 0x4764, { 0xaa, 0x33, 0xed, 0x4f, 0x2d, 0x1f, 0xf6, 0x69 } };

            std::wstring wPath = utf8ToWide(path);
            IMFSourceReader* reader = nullptr;
            if (SUCCEEDED(pMFCreateSourceReaderFromURL(wPath.c_str(), nullptr, &reader)) && reader) {
                IMFMediaType* partialType = nullptr;
                if (SUCCEEDED(pMFCreateMediaType(&partialType)) && partialType) {
                    partialType->SetGUID(kMF_MT_MAJOR_TYPE, kMFMediaType_Audio);
                    partialType->SetGUID(kMF_MT_SUBTYPE, kMFAudioFormat_Float);

                    if (SUCCEEDED(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, partialType))) {
                        IMFMediaType* actualType = nullptr;
                        if (SUCCEEDED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &actualType)) && actualType) {
                            UINT32 sr = 44100;
                            UINT32 ch = 2;
                            UINT32 bps = 32;
                            actualType->GetUINT32(kMF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
                            actualType->GetUINT32(kMF_MT_AUDIO_NUM_CHANNELS, &ch);
                            actualType->GetUINT32(kMF_MT_AUDIO_BITS_PER_SAMPLE, &bps);
                            actualType->Release();

                            if (sr >= 4000 && sr <= 384000 && ch >= 1 && bps == 32) {
                                out.sampleRate = static_cast<float>(sr);
                                constexpr size_t kMaxFrames = 48000 * 120; // cap at 2 minutes of audio

                                while (out.samplesL.size() < kMaxFrames) {
                                    DWORD streamFlags = 0;
                                    IMFSample* sample = nullptr;
                                    HRESULT hrRead = reader->ReadSample(
                                        static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
                                        0, nullptr, &streamFlags, nullptr, &sample
                                    );
                                    if (FAILED(hrRead) || (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                                        if (sample) sample->Release();
                                        break;
                                    }
                                    if (sample) {
                                        IMFMediaBuffer* buf = nullptr;
                                        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && buf) {
                                            BYTE* raw = nullptr;
                                            DWORD cbLen = 0;
                                            if (SUCCEEDED(buf->Lock(&raw, nullptr, &cbLen)) && raw && cbLen > 0) {
                                                size_t frameBytes = static_cast<size_t>(ch) * sizeof(float);
                                                size_t frames = cbLen / frameBytes;
                                                const float* fdata = reinterpret_cast<const float*>(raw);
                                                for (size_t i = 0; i < frames; ++i) {
                                                    float l = fdata[i * ch];
                                                    float r = (ch >= 2) ? fdata[i * ch + 1] : l;
                                                    out.samplesL.push_back(std::isfinite(l) ? l : 0.0f);
                                                    out.samplesR.push_back(std::isfinite(r) ? r : 0.0f);
                                                }
                                                buf->Unlock();
                                            }
                                            buf->Release();
                                        }
                                        sample->Release();
                                    }
                                }
                                success = !out.samplesL.empty();
                            }
                        }
                    }
                    partialType->Release();
                }
                reader->Release();
            }
            pMFShutdown();
        }

        if (didCoInit) CoUninitialize();
        FreeLibrary(hMfReadWrite);
        FreeLibrary(hMfPlat);
        return success;
#else
        (void)path;
        (void)out;
        return false;
#endif
    }

    static void finalizeBuffer(LoadedAudioBuffer& out) {
        size_t n = out.numFrames();
        if (n == 0) return;

        // Trim leading and trailing digital silence
        size_t first = 0;
        while (first < n && std::fabs(out.samplesL[first]) < 1e-4f && std::fabs(out.samplesR[first]) < 1e-4f) {
            ++first;
        }
        if (first >= n) {
            out.clear();
            return;
        }
        size_t last = n;
        while (last > first + 1 && std::fabs(out.samplesL[last - 1]) < 1e-4f && std::fabs(out.samplesR[last - 1]) < 1e-4f) {
            --last;
        }

        if (first > 0 || last < n) {
            out.samplesL.erase(out.samplesL.begin() + static_cast<ptrdiff_t>(last), out.samplesL.end());
            out.samplesR.erase(out.samplesR.begin() + static_cast<ptrdiff_t>(last), out.samplesR.end());
            out.samplesL.erase(out.samplesL.begin(), out.samplesL.begin() + static_cast<ptrdiff_t>(first));
            out.samplesR.erase(out.samplesR.begin(), out.samplesR.begin() + static_cast<ptrdiff_t>(first));
        }

        n = out.numFrames();
        float peak = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            peak = std::max(peak, std::max(std::fabs(out.samplesL[i]), std::fabs(out.samplesR[i])));
        }
        if (peak > 1e-5f) {
            float gain = 0.85f / peak;
            for (size_t i = 0; i < n; ++i) {
                out.samplesL[i] *= gain;
                out.samplesR[i] *= gain;
            }
        }

        // Locate sustain loop region [15% .. 85%] anchored near positive-going zero crossings
        if (n <= 512) {
            out.loopStart = 0;
            out.loopEnd = n;
        } else {
            size_t rawStart = (n * 15) / 100;
            size_t rawEnd = (n * 85) / 100;
            if (rawEnd <= rawStart + 256) {
                rawStart = 0;
                rawEnd = n;
            } else {
                auto snapZero = [&](size_t target) -> size_t {
                    size_t lo = (target > 256) ? (target - 256) : 1;
                    size_t hi = std::min(n - 1, target + 256);
                    for (size_t i = lo; i < hi; ++i) {
                        if (out.samplesL[i - 1] <= 0.0f && out.samplesL[i] >= 0.0f) return i;
                    }
                    return target;
                };
                rawStart = snapZero(rawStart);
                rawEnd = snapZero(rawEnd);
                if (rawEnd <= rawStart + 256) {
                    rawStart = 0;
                    rawEnd = n;
                }
            }
            out.loopStart = rawStart;
            out.loopEnd = rawEnd;
        }
        out.valid = true;
    }
};

} // namespace Fraudize
