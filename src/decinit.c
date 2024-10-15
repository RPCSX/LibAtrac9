#include "decinit.h"
#include "bit_allocation.h"
#include "bit_reader.h"
#include "error_codes.h"
#include "huffCodes.h"
#include "structures.h"
#include "tables.h"
#include "utility.h"
#include <math.h>
#include <string.h>

static At9Status InitConfigData(ConfigData* config, unsigned char * configData);
static At9Status ReadConfigData(ConfigData* config);
static At9Status InitFrame(Atrac9Handle* handle);
static At9Status InitBlock(Block* block, Frame* parentFrame, int blockIndex);
static At9Status InitChannel(Channel* channel, Block* parentBlock, int channelIndex);
static void InitHuffmanCodebooks();
static void InitHuffmanSet(const HuffmanCodebook* codebooks, int count);
static void GenerateTrigTables(int sizeBits);
static void GenerateShuffleTable(int sizeBits);
static void InitMdctTables(int frameSizePower);
static void GenerateMdctWindow(int frameSizePower);
static void GenerateImdctWindow(int frameSizePower);

static int BlockTypeToChannelCount(BlockType blockType);

At9Status InitDecoder(Atrac9Handle* handle, unsigned char* configData, int wlength)
{
	ERROR_CHECK(InitConfigData(&handle->config, configData));
	ERROR_CHECK(InitFrame(handle));
	InitMdctTables(handle->config.frameSamplesPower);
	InitHuffmanCodebooks();
	GenerateGradientCurves();
	handle->wlength = wlength;
	handle->initialized = 1;
	return ERR_SUCCESS;
}

static At9Status InitConfigData(ConfigData* config, unsigned char* configData)
{
	memcpy(config->configData, configData, CONFIG_DATA_SIZE);
	ERROR_CHECK(ReadConfigData(config));

	config->framesPerSuperframe = 1 << config->superframeIndex;
	config->superframeBytes = config->frameBytes << config->superframeIndex;

	config->channelConfig = ChannelConfigs[config->channelConfigIndex];
	config->channelCount = config->channelConfig.channelCount;
	config->sampleRate = SampleRates[config->sampleRateIndex];
	config->highSampleRate = config->sampleRateIndex > 7;
	config->frameSamplesPower = SamplingRateIndexToFrameSamplesPower[config->sampleRateIndex];
	config->frameSamples = 1 << config->frameSamplesPower;
	config->superframeSamples = config->frameSamples * config->framesPerSuperframe;

	return ERR_SUCCESS;
}

static At9Status ReadConfigData(ConfigData* config)
{
	BitReaderCxt br;
	InitBitReaderCxt(&br, &config->configData);

	const int header = ReadInt(&br, 8);
	config->sampleRateIndex = ReadInt(&br, 4);
	config->channelConfigIndex = ReadInt(&br, 3);
	const int validationBit = ReadInt(&br, 1);
	config->frameBytes = ReadInt(&br, 11) + 1;
	config->superframeIndex = ReadInt(&br, 2);

	if (header != 0xFE || validationBit != 0)
	{
		return ERR_BAD_CONFIG_DATA;
	}

	return ERR_SUCCESS;
}

static At9Status InitFrame(Atrac9Handle* handle)
{
	const int blockCount = handle->config.channelConfig.blockCount;
	handle->frame.Config = &handle->config;
	int channelNum = 0;

	for (int i = 0; i < blockCount; i++)
	{
		ERROR_CHECK(InitBlock(&handle->frame.Blocks[i], &handle->frame, i));

		for (int c = 0; c < handle->frame.Blocks[i].channelCount; c++)
		{
			handle->frame.Channels[channelNum++] = &handle->frame.Blocks[i].channels[c];
		}
	}

	return ERR_SUCCESS;
}

static At9Status InitBlock(Block* block, Frame* parentFrame, int blockIndex)
{
	block->frame = parentFrame;
	block->blockIndex = blockIndex;
	block->config = parentFrame->Config;
	block->blockType = block->config->channelConfig.types[blockIndex];
	block->channelCount = BlockTypeToChannelCount(block->blockType);

	for (int i = 0; i < block->channelCount; i++)
	{
		ERROR_CHECK(InitChannel(&block->channels[i], block, i));
	}

	return ERR_SUCCESS;
}

static At9Status InitChannel(Channel* channel, Block* parentBlock, int channelIndex)
{
	channel->block = parentBlock;
	channel->frame = parentBlock->frame;
	channel->config = parentBlock->config;
	channel->channelIndex = channelIndex;
	channel->mdct.bits = parentBlock->config->frameSamplesPower;
	return ERR_SUCCESS;
}

static void InitHuffmanCodebooks()
{
	InitHuffmanSet(HuffmanScaleFactorsUnsigned, sizeof(HuffmanScaleFactorsUnsigned) / sizeof(HuffmanCodebook));
	InitHuffmanSet(HuffmanScaleFactorsSigned, sizeof(HuffmanScaleFactorsSigned) / sizeof(HuffmanCodebook));
	InitHuffmanSet((HuffmanCodebook*)HuffmanSpectrum, sizeof(HuffmanSpectrum) / sizeof(HuffmanCodebook));
}

static void InitHuffmanSet(const HuffmanCodebook* codebooks, int count)
{
	for (int i = 0; i < count; i++)
	{
		InitHuffmanCodebook(&codebooks[i]);
	}
}

static void InitMdctTables(int frameSizePower)
{
	for (int i = 0; i < 9; i++)
	{
		GenerateTrigTables(i);
		GenerateShuffleTable(i);
	}
	GenerateMdctWindow(frameSizePower);
	GenerateImdctWindow(frameSizePower);
}

static void GenerateTrigTables(int sizeBits)
{
	const int size = 1 << sizeBits;
	double* sinTab = SinTables[sizeBits];
	double* cosTab = CosTables[sizeBits];

	for (int i = 0; i < size; i++)
	{
		const double value = M_PI * (4 * i + 1) / (4 * size);
		sinTab[i] = sin(value);
		cosTab[i] = cos(value);
	}
}

static void GenerateShuffleTable(int sizeBits)
{
	const int size = 1 << sizeBits;
	int* table = ShuffleTables[sizeBits];

	for (int i = 0; i < size; i++)
	{
		table[i] = BitReverse32(i ^ (i / 2), sizeBits);
	}
}

static void GenerateMdctWindow(int frameSizePower)
{
	const int frameSize = 1 << frameSizePower;
	double* mdct = MdctWindow[frameSizePower - 6];

	for (int i = 0; i < frameSize; i++)
	{
		mdct[i] = (sin(((i + 0.5) / frameSize - 0.5) * M_PI) + 1.0) * 0.5;
	}
}

static void GenerateImdctWindow(int frameSizePower)
{
	const int frameSize = 1 << frameSizePower;
	double* imdct = ImdctWindow[frameSizePower - 6];
	double* mdct = MdctWindow[frameSizePower - 6];

	for (int i = 0; i < frameSize; i++)
	{
		imdct[i] = mdct[i] / (mdct[frameSize - 1 - i] * mdct[frameSize - 1 - i] + mdct[i] * mdct[i]);
	}
}

static int BlockTypeToChannelCount(BlockType blockType)
{
	switch (blockType)
	{
	case Mono:
		return 1;
	case Stereo:
		return 2;
	case LFE:
		return 1;
	default:
		return 0;
	}
}
