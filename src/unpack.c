#include "unpack.h"
#include "band_extension.h"
#include "bit_allocation.h"
#include "huffCodes.h"
#include "scale_factors.h"
#include "tables.h"
#include "utility.h"
#include <string.h>

static At9Status UnpackBlock(Block* block, BitReaderCxt* br);
static At9Status ReadBlockHeader(Block* block, BitReaderCxt* br);
static At9Status UnpackStandardBlock(Block* block, BitReaderCxt* br);
static At9Status ReadBandParams(Block* block, BitReaderCxt* br);
static At9Status ReadGradientParams(Block* block, BitReaderCxt* br);
static At9Status ReadStereoParams(Block* block, BitReaderCxt* br);
static At9Status ReadExtensionParams(Block* block, BitReaderCxt* br);
static void UpdateCodedUnits(Channel* channel);
static void CalculateSpectrumCodebookIndex(Channel* channel);

static At9Status ReadSpectra(Channel* channel, BitReaderCxt* br);
static At9Status ReadSpectraFine(Channel* channel, BitReaderCxt* br);

static At9Status UnpackLfeBlock(Block* block, BitReaderCxt* br);
static void DecodeLfeScaleFactors(Channel* channel, BitReaderCxt* br);
static void CalculateLfePrecision(Channel* channel);
static void ReadLfeSpectra(Channel* channel, BitReaderCxt* br);

At9Status UnpackFrame(Frame* frame, BitReaderCxt* br)
{
	const int blockCount = frame->Config->channelConfig.blockCount;

	for (int i = 0; i < blockCount; i++)
	{
		ERROR_CHECK(UnpackBlock(&frame->Blocks[i], br));

		if (frame->Blocks[i].firstInSuperframe && frame->IndexInSuperframe)
		{
			return ERR_UNPACK_SUPERFRAME_FLAG_INVALID;
		}
	}

	frame->IndexInSuperframe++;

	if (frame->IndexInSuperframe == frame->Config->framesPerSuperframe)
	{
		frame->IndexInSuperframe = 0;
	}

	return ERR_SUCCESS;
}

static At9Status UnpackBlock(Block* block, BitReaderCxt* br)
{
	ERROR_CHECK(ReadBlockHeader(block, br));

	if (block->blockType == LFE)
	{
		ERROR_CHECK(UnpackLfeBlock(block, br));
	}
	else
	{
		ERROR_CHECK(UnpackStandardBlock(block, br));
	}

	AlignPosition(br, 8);
	return ERR_SUCCESS;
}

static At9Status ReadBlockHeader(Block* block, BitReaderCxt* br)
{
	block->firstInSuperframe = !ReadInt(br, 1);
	block->reuseBandParams = ReadInt(br, 1);

	if (block->firstInSuperframe && block->reuseBandParams && block->blockType != LFE)
	{
		return ERR_UNPACK_REUSE_BAND_PARAMS_INVALID;
	}

	return ERR_SUCCESS;
}

static At9Status UnpackStandardBlock(Block* block, BitReaderCxt* br)
{
	if (!block->reuseBandParams)
	{
		ERROR_CHECK(ReadBandParams(block, br));
	}

	ERROR_CHECK(ReadGradientParams(block, br));
	ERROR_CHECK(CreateGradient(block));
	ERROR_CHECK(ReadStereoParams(block, br));
	ERROR_CHECK(ReadExtensionParams(block, br));

	for (int i = 0; i < block->channelCount; i++)
	{
		Channel* channel = &block->channels[i];
		UpdateCodedUnits(channel);

		ERROR_CHECK(ReadScaleFactors(channel, br));
		CalculateMask(channel);
		CalculatePrecisions(channel);
		CalculateSpectrumCodebookIndex(channel);

		ERROR_CHECK(ReadSpectra(channel, br));
		ERROR_CHECK(ReadSpectraFine(channel, br));
	}

	block->quantizationUnitsPrev = block->bandExtensionEnabled ? block->extensionUnit : block->quantizationUnitCount;
	return ERR_SUCCESS;
}

static At9Status ReadBandParams(Block* block, BitReaderCxt* br)
{
	const int minBandCount = MinBandCount[block->config->highSampleRate];
	const int maxExtensionBand = MaxExtensionBand[block->config->highSampleRate];
	block->bandCount = ReadInt(br, 4) + minBandCount;
	block->quantizationUnitCount = BandToQuantUnitCount[block->bandCount];

	if (block->bandCount > MaxBandCount[block->config->sampleRateIndex])
	{
		return ERR_UNPACK_BAND_PARAMS_INVALID;
	}

	if (block->blockType == Stereo)
	{
		block->stereoBand = ReadInt(br, 4);
		block->stereoBand += minBandCount;
		block->stereoQuantizationUnit = BandToQuantUnitCount[block->stereoBand];
	}
	else
	{
		block->stereoBand = block->bandCount;
	}

	if (block->stereoBand > block->bandCount)
	{
		return ERR_UNPACK_BAND_PARAMS_INVALID;
	}

	block->bandExtensionEnabled = ReadInt(br, 1);
	if (block->bandExtensionEnabled)
	{
		block->extensionBand = ReadInt(br, 4);
		block->extensionBand += minBandCount;

		if (block->extensionBand < block->bandCount || block->extensionBand > maxExtensionBand)
		{
			return ERR_UNPACK_BAND_PARAMS_INVALID;
		}

		block->extensionUnit = BandToQuantUnitCount[block->extensionBand];
	}
	else
	{
		block->extensionBand = block->bandCount;
		block->extensionUnit = block->quantizationUnitCount;
	}

	return ERR_SUCCESS;
}

static At9Status ReadGradientParams(Block* block, BitReaderCxt* br)
{
	block->gradientMode = ReadInt(br, 2);
	if (block->gradientMode > 0)
	{
		block->gradientEndUnit = 31;
		block->gradientEndValue = 31;
		block->gradientStartUnit = ReadInt(br, 5);
		block->gradientStartValue = ReadInt(br, 5);
	}
	else
	{
		block->gradientStartUnit = ReadInt(br, 6);
		block->gradientEndUnit = ReadInt(br, 6) + 1;
		block->gradientStartValue = ReadInt(br, 5);
		block->gradientEndValue = ReadInt(br, 5);
	}
	block->gradientBoundary = ReadInt(br, 4);

	if (block->gradientBoundary > block->quantizationUnitCount)
	{
		return ERR_UNPACK_GRAD_BOUNDARY_INVALID;
	}
	if (block->gradientStartUnit < 0 || block->gradientStartUnit >= 48)
	{
		return ERR_UNPACK_GRAD_START_UNIT_OOB;
	}
	if (block->gradientEndUnit < 0 || block->gradientEndUnit >= 48)
	{
		return ERR_UNPACK_GRAD_END_UNIT_OOB;
	}
	if (block->gradientStartUnit > block->gradientEndUnit)
	{
		return ERR_UNPACK_GRAD_END_UNIT_INVALID;
	}
	if (block->gradientStartValue < 0 || block->gradientStartValue >= 32)
	{
		return ERR_UNPACK_GRAD_START_VALUE_OOB;
	}
	if (block->gradientEndValue < 0 || block->gradientEndValue >= 32)
	{
		return ERR_UNPACK_GRAD_END_VALUE_OOB;
	}

	return ERR_SUCCESS;
}

static At9Status ReadStereoParams(Block* block, BitReaderCxt* br)
{
	if (block->blockType != Stereo) return ERR_SUCCESS;

	block->primaryChannelIndex = ReadInt(br, 1);
	block->hasJointStereoSigns = ReadInt(br, 1);
	if (block->hasJointStereoSigns)
	{
		for (int i = block->stereoQuantizationUnit; i < block->quantizationUnitCount; i++)
		{
			block->jointStereoSigns[i] = ReadInt(br, 1);
		}
	}
	else
	{
		memset(block->jointStereoSigns, 0, sizeof(block->jointStereoSigns));
	}

	return ERR_SUCCESS;
}

static void BexReadHeader(Channel* channel, BitReaderCxt* br, int bexBand)
{
	const int bexMode = ReadInt(br, 2);
	channel->bexMode = bexBand > 2 ? bexMode : 4;
	channel->bexValueCount = BexEncodedValueCounts[channel->bexMode][bexBand];
}

static void BexReadData(Channel* channel, BitReaderCxt* br, int bexBand)
{
	for (int i = 0; i < channel->bexValueCount; i++)
	{
		const int dataLength = BexDataLengths[channel->bexMode][bexBand][i];
		channel->bexValues[i] = ReadInt(br, dataLength);
	}
}

static At9Status ReadExtensionParams(Block* block, BitReaderCxt* br)
{
	int bexBand = 0;
	if (block->bandExtensionEnabled)
	{
		bexBand = BexGroupInfo[block->quantizationUnitCount - 13].BandCount;
		if (block->blockType == Stereo)
		{
			BexReadHeader(&block->channels[1], br, bexBand);
		}
		else
		{
			br->Position += 1;
		}
	}
	block->hasExtensionData = ReadInt(br, 1);

	if (!block->hasExtensionData) return ERR_SUCCESS;
	if (!block->bandExtensionEnabled)
	{
		block->bexMode = ReadInt(br, 2);
		block->bexDataLength = ReadInt(br, 5);
		br->Position += block->bexDataLength;
		return ERR_SUCCESS;
	}

	BexReadHeader(&block->channels[0], br, bexBand);

	block->bexDataLength = ReadInt(br, 5);
	if (block->bexDataLength == 0) return ERR_SUCCESS;
	const int bexDataEnd = br->Position + block->bexDataLength;

	BexReadData(&block->channels[0], br, bexBand);

	if (block->blockType == Stereo)
	{
		BexReadData(&block->channels[1], br, bexBand);
	}

	// Make sure we didn't read too many bits
	if (br->Position > bexDataEnd)
	{
		return ERR_UNPACK_EXTENSION_DATA_INVALID;
	}

	return ERR_SUCCESS;
}

static void UpdateCodedUnits(Channel* channel)
{
	if (channel->block->primaryChannelIndex == channel->channelIndex)
	{
		channel->codedQuantUnits = channel->block->quantizationUnitCount;
	}
	else
	{
		channel->codedQuantUnits = channel->block->stereoQuantizationUnit;
	}
}

static void CalculateSpectrumCodebookIndex(Channel* channel)
{
	memset(channel->codebookSet, 0, sizeof(channel->codebookSet));
	const int quantUnits = channel->codedQuantUnits;
	int* sf = channel->scaleFactors;

	if (quantUnits <= 1) return;
	if (channel->config->highSampleRate) return;

	// Temporarily setting this value allows for simpler code by
	// making the last value a non-special case.
	const int originalScaleTmp = sf[quantUnits];
	sf[quantUnits] = sf[quantUnits - 1];

	int avg = 0;
	if (quantUnits > 12)
	{
		for (int i = 0; i < 12; i++)
		{
			avg += sf[i];
		}
		avg = (avg + 6) / 12;
	}

	for (int i = 8; i < quantUnits; i++)
	{
		const int prevSf = sf[i - 1];
		const int nextSf = sf[i + 1];
		const int minSf = Min(prevSf, nextSf);
		if (sf[i] - minSf >= 3 || sf[i] - prevSf + sf[i] - nextSf >= 3)
		{
			channel->codebookSet[i] = 1;
		}
	}

	for (int i = 12; i < quantUnits; i++)
	{
		if (channel->codebookSet[i] == 0)
		{
			const int minSf = Min(sf[i - 1], sf[i + 1]);
			if (sf[i] - minSf >= 2 && sf[i] >= avg - (QuantUnitToCoeffCount[i] == 16 ? 1 : 0))
			{
				channel->codebookSet[i] = 1;
			}
		}
	}

	sf[quantUnits] = originalScaleTmp;
}

static At9Status ReadSpectra(Channel* channel, BitReaderCxt* br)
{
	int values[16];
	memset(channel->quantizedSpectra, 0, sizeof(channel->quantizedSpectra));
	const int maxHuffPrecision = MaxHuffPrecision[channel->config->highSampleRate];

	for (int i = 0; i < channel->codedQuantUnits; i++)
	{
		const int subbandCount = QuantUnitToCoeffCount[i];
		const int precision = channel->precisions[i] + 1;
		if (precision <= maxHuffPrecision)
		{
			const HuffmanCodebook* huff = &HuffmanSpectrum[channel->codebookSet[i]][precision][QuantUnitToCodebookIndex[i]];
			const int groupCount = subbandCount >> huff->ValueCountPower;
			for (int j = 0; j < groupCount; j++)
			{
				values[j] = ReadHuffmanValue(huff, br, FALSE);
			}

			DecodeHuffmanValues(channel->quantizedSpectra, QuantUnitToCoeffIndex[i], subbandCount, huff, values);
		}
		else
		{
			const int subbandIndex = QuantUnitToCoeffIndex[i];
			for (int j = subbandIndex; j < QuantUnitToCoeffIndex[i + 1]; j++)
			{
				channel->quantizedSpectra[j] = ReadSignedInt(br, precision);
			}
		}
	}

	return ERR_SUCCESS;
}

static At9Status ReadSpectraFine(Channel* channel, BitReaderCxt* br)
{
	memset(channel->quantizedSpectraFine, 0, sizeof(channel->quantizedSpectraFine));

	for (int i = 0; i < channel->codedQuantUnits; i++)
	{
		if (channel->precisionsFine[i] > 0)
		{
			const int overflowBits = channel->precisionsFine[i] + 1;
			const int startSubband = QuantUnitToCoeffIndex[i];
			const int endSubband = QuantUnitToCoeffIndex[i + 1];

			for (int j = startSubband; j < endSubband; j++)
			{
				channel->quantizedSpectraFine[j] = ReadSignedInt(br, overflowBits);
			}
		}
	}
	return ERR_SUCCESS;
}

static At9Status UnpackLfeBlock(Block* block, BitReaderCxt* br)
{
	Channel* channel = &block->channels[0];
	block->quantizationUnitCount = 2;

	DecodeLfeScaleFactors(channel, br);
	CalculateLfePrecision(channel);
	channel->codedQuantUnits = block->quantizationUnitCount;
	ReadLfeSpectra(channel, br);

	return ERR_SUCCESS;
}

static void DecodeLfeScaleFactors(Channel* channel, BitReaderCxt* br)
{
	memset(channel->scaleFactors, 0, sizeof(channel->scaleFactors));
	for (int i = 0; i < channel->block->quantizationUnitCount; i++)
	{
		channel->scaleFactors[i] = ReadInt(br, 5);
	}
}

static void CalculateLfePrecision(Channel* channel)
{
	Block* block = channel->block;
	const int precision = block->reuseBandParams ? 8 : 4;
	for (int i = 0; i < block->quantizationUnitCount; i++)
	{
		channel->precisions[i] = precision;
		channel->precisionsFine[i] = 0;
	}
}

static void ReadLfeSpectra(Channel* channel, BitReaderCxt* br)
{
	memset(channel->quantizedSpectra, 0, sizeof(channel->quantizedSpectra));

	for (int i = 0; i < channel->codedQuantUnits; i++)
	{
		if (channel->precisions[i] <= 0) continue;

		const int precision = channel->precisions[i] + 1;
		for (int j = QuantUnitToCoeffIndex[i]; j < QuantUnitToCoeffIndex[i + 1]; j++)
		{
			channel->quantizedSpectra[j] = ReadSignedInt(br, precision);
		}
	}
}
