#include "decoder.h"
#include "band_extension.h"
#include "bit_reader.h"
#include "imdct.h"
#include "quantization.h"
#include "tables.h"
#include "unpack.h"
#include <stdint.h>
#include <string.h>
#include <limits.h>


static At9Status DecodeFrame(Frame* frame, BitReaderCxt* br);
static void ImdctBlock(Block* block);
static void ApplyIntensityStereo(Block* block);
static void PcmFloatToS16(Frame* frame, int16_t* pcmOut);
static void PcmFloatToS32(Frame* frame, int32_t* pcmOut);
static void PcmFloatToF32(Frame* frame, float* pcmOut);
static void PcmFloatToF64(Frame* frame, double* pcmOut);

static int16_t ClampS16(int32_t value)
{
	if (value > INT16_MAX)
		return INT16_MAX;
	if (value < INT16_MIN)
		return INT16_MIN;
	return (int16_t)value;
}
static int RoundDouble(double x)
{
	x += 0.5;
	return (int)x - (x < (int)x);
}


At9Status DecodeS16(Atrac9Handle* handle, const void* audio, void* pcm, int* bytesUsed)
{
	BitReaderCxt br;
	InitBitReaderCxt(&br, audio);
	ERROR_CHECK(DecodeFrame(&handle->frame, &br));

	PcmFloatToS16(&handle->frame, (int16_t*)pcm);

	*bytesUsed = br.Position / 8;
	return ERR_SUCCESS;
}
At9Status DecodeS32(Atrac9Handle* handle, const void* audio, void* pcm, int* bytesUsed)
{
	BitReaderCxt br;
	InitBitReaderCxt(&br, audio);
	ERROR_CHECK(DecodeFrame(&handle->frame, &br));

	PcmFloatToS32(&handle->frame, (int32_t*)pcm);

	*bytesUsed = br.Position / 8;
	return ERR_SUCCESS;
}
At9Status DecodeF32(Atrac9Handle* handle, const void* audio, void* pcm, int* bytesUsed)
{
	BitReaderCxt br;
	InitBitReaderCxt(&br, audio);
	ERROR_CHECK(DecodeFrame(&handle->frame, &br));

	PcmFloatToF32(&handle->frame, (float*)pcm);

	*bytesUsed = br.Position / 8;
	return ERR_SUCCESS;
}
At9Status DecodeF64(Atrac9Handle* handle, const void* audio, void* pcm, int* bytesUsed)
{
	BitReaderCxt br;
	InitBitReaderCxt(&br, audio);
	ERROR_CHECK(DecodeFrame(&handle->frame, &br));

	PcmFloatToF64(&handle->frame, (double*)pcm);

	*bytesUsed = br.Position / 8;
	return ERR_SUCCESS;
}

static At9Status DecodeFrame(Frame* frame, BitReaderCxt* br)
{
	ERROR_CHECK(UnpackFrame(frame, br));

	for (int i = 0; i < frame->Config->channelConfig.blockCount; i++)
	{
		Block* block = &frame->Blocks[i];

		DequantizeSpectra(block);
		ApplyIntensityStereo(block);
		ScaleSpectrumBlock(block);
		ApplyBandExtension(block);
		ImdctBlock(block);
	}

	return ERR_SUCCESS;
}

void PcmFloatToS16(Frame* frame, int16_t* pcmOut)
{
	const int channelCount = frame->Config->channelCount;
	const int sampleCount = frame->Config->frameSamples;
	Channel** channels = frame->Channels;
	int i = 0;

	for (int smpl = 0; smpl < sampleCount; smpl++)
	{
		for (int ch = 0; ch < channelCount; ch++, i++)
		{
			pcmOut[i] = ClampS16(RoundDouble(channels[ch]->pcm[smpl]));
		}
	}
}

void PcmFloatToS32(Frame* frame, int32_t* pcmOut)
{
	const int channelCount = frame->Config->channelCount;
	const int sampleCount = frame->Config->frameSamples;
	Channel** channels = frame->Channels;
	int i = 0;

	for (int smpl = 0; smpl < sampleCount; smpl++)
	{
		for (int ch = 0; ch < channelCount; ch++, i++)
		{
			pcmOut[i] = RoundDouble(channels[ch]->pcm[smpl]);
		}
	}
}

void PcmFloatToF32(Frame* frame, float* pcmOut)
{
	const int channelCount = frame->Config->channelCount;
	const int sampleCount = frame->Config->frameSamples;
	Channel** channels = frame->Channels;
	int i = 0;

	for (int smpl = 0; smpl < sampleCount; smpl++)
	{
		for (int ch = 0; ch < channelCount; ch++, i++)
		{
			pcmOut[i] = (float)channels[ch]->pcm[smpl];
		}
	}
}

void PcmFloatToF64(Frame* frame, double* pcmOut)
{
	const int channelCount = frame->Config->channelCount;
	const int sampleCount = frame->Config->frameSamples;
	Channel** channels = frame->Channels;
	int i = 0;

	for (int smpl = 0; smpl < sampleCount; smpl++)
	{
		for (int ch = 0; ch < channelCount; ch++, i++)
		{
			pcmOut[i] = channels[ch]->pcm[smpl];
		}
	}
}

static void ImdctBlock(Block* block)
{
	for (int i = 0; i < block->channelCount; i++)
	{
		Channel* channel = &block->channels[i];

		RunImdct(&channel->mdct, channel->spectra, channel->pcm);
	}
}

static void ApplyIntensityStereo(Block* block)
{
	if (block->blockType != Stereo) return;

	const int totalUnits = block->quantizationUnitCount;
	const int stereoUnits = block->stereoQuantizationUnit;
	if (stereoUnits >= totalUnits) return;

	Channel* source = &block->channels[block->primaryChannelIndex == 0 ? 0 : 1];
	Channel* dest = &block->channels[block->primaryChannelIndex == 0 ? 1 : 0];

	for (int i = stereoUnits; i < totalUnits; i++)
	{
		const int sign = block->jointStereoSigns[i];
		for (int sb = QuantUnitToCoeffIndex[i]; sb < QuantUnitToCoeffIndex[i + 1]; sb++)
		{
			if (sign > 0)
			{
				dest->spectra[sb] = -source->spectra[sb];
			}
			else
			{
				dest->spectra[sb] = source->spectra[sb];
			}
		}
	}
}

int GetCodecInfo(Atrac9Handle* handle, CodecInfo * pCodecInfo)
{
	pCodecInfo->channels = handle->config.channelCount;
	pCodecInfo->channelConfigIndex = handle->config.channelConfigIndex;
	pCodecInfo->samplingRate = handle->config.sampleRate;
	pCodecInfo->superframeSize = handle->config.superframeBytes;
	pCodecInfo->framesInSuperframe = handle->config.framesPerSuperframe;
	pCodecInfo->frameSamples = handle->config.frameSamples;
	pCodecInfo->wlength = handle->wlength;
	memcpy(pCodecInfo->configData, handle->config.configData, CONFIG_DATA_SIZE);
	return ERR_SUCCESS;
}
