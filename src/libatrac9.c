#include "decinit.h"
#include "decoder.h"
#include "libatrac9.h"
#include "structures.h"
#include <errno.h>
#include <stdlib.h>

void* Atrac9GetHandle()
{
	return calloc(1, sizeof(Atrac9Handle));
}

void Atrac9ReleaseHandle(void* handle)
{
	free(handle);
}

int Atrac9InitDecoder(void* handle, unsigned char * pConfigData)
{
	return InitDecoder(handle, pConfigData, 16);
}

int Atrac9Decode(void* handle, const void *pAtrac9Buffer, void *pPcmBuffer, Atrac9Format format, int *pNBytesUsed)
{
	switch (format)
	{
	case kAtrac9FormatS16:
		return DecodeS16(handle, pAtrac9Buffer, pPcmBuffer, pNBytesUsed);
	case kAtrac9FormatS32:
		return DecodeS32(handle, pAtrac9Buffer, pPcmBuffer, pNBytesUsed);
	case kAtrac9FormatF32:
		return DecodeF32(handle, pAtrac9Buffer, pPcmBuffer, pNBytesUsed);
	case kAtrac9FormatF64:
		return DecodeF64(handle, pAtrac9Buffer, pPcmBuffer, pNBytesUsed);
	}

	return -EINVAL;
}

int Atrac9GetCodecInfo(void* handle, Atrac9CodecInfo * pCodecInfo)
{
	return GetCodecInfo(handle, (CodecInfo*)pCodecInfo);
}
