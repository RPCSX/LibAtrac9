#include "quantization.h"
#include "tables.h"
#include <string.h>

static void DequantizeQuantUnit(Channel* channel, int band);
static void ScaleSpectrumChannel(Channel* channel);

void DequantizeSpectra(Block* block)
{
	for (int i = 0; i < block->channelCount; i++)
	{
		Channel* channel = &block->channels[i];
		memset(channel->spectra, 0, sizeof(channel->spectra));

		for (int j = 0; j < channel->codedQuantUnits; j++)
		{
			DequantizeQuantUnit(channel, j);
		}
	}
}

static void DequantizeQuantUnit(Channel* channel, int band)
{
	const int subBandIndex = QuantUnitToCoeffIndex[band];
	const int subBandCount = QuantUnitToCoeffCount[band];
	const double stepSize = QuantizerStepSize[channel->precisions[band]];
	const double stepSizeFine = QuantizerFineStepSize[channel->precisionsFine[band]];

	for (int sb = 0; sb < subBandCount; sb++)
	{
		const double coarse = channel->quantizedSpectra[subBandIndex + sb] * stepSize;
		const double fine = channel->quantizedSpectraFine[subBandIndex + sb] * stepSizeFine;
		channel->spectra[subBandIndex + sb] = coarse + fine;
	}
}

void ScaleSpectrumBlock(Block* block)
 {
	 for (int i = 0; i < block->channelCount; i++)
	 {
		 ScaleSpectrumChannel(&block->channels[i]);
	 }
 }

static void ScaleSpectrumChannel(Channel* channel)
{
	 const int quantUnitCount = channel->block->quantizationUnitCount;
	 double* spectra = channel->spectra;

	 for (int i = 0; i < quantUnitCount; i++)
	 {
		 for (int sb = QuantUnitToCoeffIndex[i]; sb < QuantUnitToCoeffIndex[i + 1]; sb++)
		 {
			 spectra[sb] *= SpectrumScale[channel->scaleFactors[i]];
		 }
	 }
 }