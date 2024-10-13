#pragma once

#include "error_codes.h"
#include "structures.h"

At9Status DecodeS16(Atrac9Handle* handle, const void* audio, void* pcm, int* bytesUsed);
At9Status DecodeS32(Atrac9Handle* handle, const void* audio, void* pcm, int* bytesUsed);
At9Status DecodeF32(Atrac9Handle* handle, const void* audio, void* pcm, int* bytesUsed);
At9Status DecodeF64(Atrac9Handle* handle, const void* audio, void* pcm, int* bytesUsed);

int GetCodecInfo(Atrac9Handle* handle, CodecInfo* pCodecInfo);
