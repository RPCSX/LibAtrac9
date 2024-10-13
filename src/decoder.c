#include "decoder.h"
#include "band_extension.h"
#include "bit_reader.h"
#include "imdct.h"
#include "quantization.h"
#include "tables.h"
#include "unpack.h"
#include "utility.h"
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
	ERROR_CHECK(DecodeFrame(&handle->Frame, &br));

	PcmFloatToS16(&handle->Frame, (int16_t*)pcm);

	*bytesUsed = br.Position / 8;
	return ERR_SUCCESS;
}
At9Status DecodeS32(Atrac9Handle* handle, const void* audio, void* pcm, int* bytesUsed)
{
	BitReaderCxt br;
	InitBitReaderCxt(&br, audio);
	ERROR_CHECK(DecodeFrame(&handle->Frame, &br));

	PcmFloatToS32(&handle->Frame, (int32_t*)pcm);

	*bytesUsed = br.Position / 8;
	return ERR_SUCCESS;
}
At9Status DecodeF32(Atrac9Handle* handle, const void* audio, void* pcm, int* bytesUsed)
{
	BitReaderCxt br;
	InitBitReaderCxt(&br, audio);
	ERROR_CHECK(DecodeFrame(&handle->Frame, &br));

	PcmFloatToF32(&handle->Frame, (float*)pcm);

	*bytesUsed = br.Position / 8;
	return ERR_SUCCESS;
}
At9Status DecodeF64(Atrac9Handle* handle, const void* audio, void* pcm, int* bytesUsed)
{
	BitReaderCxt br;
	InitBitReaderCxt(&br, audio);
	ERROR_CHECK(DecodeFrame(&handle->Frame, &br));

	PcmFloatToF64(&handle->Frame, (double*)pcm);

	*bytesUsed = br.Position / 8;
	return ERR_SUCCESS;
}

static At9Status DecodeFrame(Frame* frame, BitReaderCxt* br)
{
	ERROR_CHECK(UnpackFrame(frame, br));

	for (int i = 0; i < frame->Config->ChannelConfig.BlockCount; i++)
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
	const int channelCount = frame->Config->ChannelCount;
	const int sampleCount = frame->Config->FrameSamples;
	Channel** channels = frame->Channels;
	int i = 0;

	for (int smpl = 0; smpl < sampleCount; smpl++)
	{
		for (int ch = 0; ch < channelCount; ch++, i++)
		{
			pcmOut[i] = ClampS16(RoundDouble(channels[ch]->Pcm[smpl]));
		}
	}
}

void PcmFloatToS32(Frame* frame, int32_t* pcmOut)
{
	const int channelCount = frame->Config->ChannelCount;
	const int sampleCount = frame->Config->FrameSamples;
	Channel** channels = frame->Channels;
	int i = 0;

	for (int smpl = 0; smpl < sampleCount; smpl++)
	{
		for (int ch = 0; ch < channelCount; ch++, i++)
		{
			pcmOut[i] = RoundDouble(channels[ch]->Pcm[smpl]);
		}
	}
}

void PcmFloatToF32(Frame* frame, float* pcmOut)
{
	const int channelCount = frame->Config->ChannelCount;
	const int sampleCount = frame->Config->FrameSamples;
	Channel** channels = frame->Channels;
	int i = 0;

	for (int smpl = 0; smpl < sampleCount; smpl++)
	{
		for (int ch = 0; ch < channelCount; ch++, i++)
		{
			pcmOut[i] = (float)channels[ch]->Pcm[smpl];
		}
	}
}

void PcmFloatToF64(Frame* frame, double* pcmOut)
{
	const int channelCount = frame->Config->ChannelCount;
	const int sampleCount = frame->Config->FrameSamples;
	Channel** channels = frame->Channels;
	int i = 0;

	for (int smpl = 0; smpl < sampleCount; smpl++)
	{
		for (int ch = 0; ch < channelCount; ch++, i++)
		{
			pcmOut[i] = channels[ch]->Pcm[smpl];
		}
	}
}

static void ImdctBlock(Block* block)
{
	for (int i = 0; i < block->ChannelCount; i++)
	{
		Channel* channel = &block->Channels[i];

		RunImdct(&channel->Mdct, channel->Spectra, channel->Pcm);
	}
}

static void ApplyIntensityStereo(Block* block)
{
	if (block->BlockType != Stereo) return;

	const int totalUnits = block->QuantizationUnitCount;
	const int stereoUnits = block->StereoQuantizationUnit;
	if (stereoUnits >= totalUnits) return;

	Channel* source = &block->Channels[block->PrimaryChannelIndex == 0 ? 0 : 1];
	Channel* dest = &block->Channels[block->PrimaryChannelIndex == 0 ? 1 : 0];

	for (int i = stereoUnits; i < totalUnits; i++)
	{
		const int sign = block->JointStereoSigns[i];
		for (int sb = QuantUnitToCoeffIndex[i]; sb < QuantUnitToCoeffIndex[i + 1]; sb++)
		{
			if (sign > 0)
			{
				dest->Spectra[sb] = -source->Spectra[sb];
			}
			else
			{
				dest->Spectra[sb] = source->Spectra[sb];
			}
		}
	}
}

int GetCodecInfo(Atrac9Handle* handle, CodecInfo * pCodecInfo)
{
	pCodecInfo->Channels = handle->Config.ChannelCount;
	pCodecInfo->ChannelConfigIndex = handle->Config.ChannelConfigIndex;
	pCodecInfo->SamplingRate = handle->Config.SampleRate;
	pCodecInfo->SuperframeSize = handle->Config.SuperframeBytes;
	pCodecInfo->FramesInSuperframe = handle->Config.FramesPerSuperframe;
	pCodecInfo->FrameSamples = handle->Config.FrameSamples;
	pCodecInfo->Wlength = handle->Wlength;
	memcpy(pCodecInfo->ConfigData, handle->Config.ConfigData, CONFIG_DATA_SIZE);
	return ERR_SUCCESS;
}
