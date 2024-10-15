#include "bit_allocation.h"
#include "tables.h"
#include "utility.h"
#include <string.h>

static unsigned char GradientCurves[48][48];

At9Status CreateGradient(Block* block)
{
	int valueCount = block->gradientEndValue - block->gradientStartValue;
	int unitCount = block->gradientEndUnit - block->gradientStartUnit;

	for (int i = 0; i < block->gradientEndUnit; i++)
	{
		block->gradient[i] = block->gradientStartValue;
	}

	for (int i = block->gradientEndUnit; i <= block->quantizationUnitCount; i++)
	{
		block->gradient[i] = block->gradientEndValue;
	}
	if (unitCount <= 0) return ERR_SUCCESS;
	if (valueCount == 0) return ERR_SUCCESS;

	const unsigned char* curve = GradientCurves[unitCount - 1];
	if (valueCount <= 0)
	{
		double scale = (-valueCount - 1) / 31.0;
		int baseVal = block->gradientStartValue - 1;
		for (int i = block->gradientStartUnit; i < block->gradientEndUnit; i++)
		{
			block->gradient[i] = baseVal - (int)(curve[i - block->gradientStartUnit] * scale);
		}
	}
	else
	{
		double scale = (valueCount - 1) / 31.0;
		int baseVal = block->gradientStartValue + 1;
		for (int i = block->gradientStartUnit; i < block->gradientEndUnit; i++)
		{
			block->gradient[i] = baseVal + (int)(curve[i - block->gradientStartUnit] * scale);
		}
	}

	return ERR_SUCCESS;
}

void CalculateMask(Channel* channel)
{
	memset(channel->precisionMask, 0, sizeof(channel->precisionMask));
	for (int i = 1; i < channel->block->quantizationUnitCount; i++)
	{
		const int delta = channel->scaleFactors[i] - channel->scaleFactors[i - 1];
		if (delta > 1)
		{
			channel->precisionMask[i] += Min(delta - 1, 5);
		}
		else if (delta < -1)
		{
			channel->precisionMask[i - 1] += Min(delta * -1 - 1, 5);
		}
	}
}

void CalculatePrecisions(Channel* channel)
{
	Block* block = channel->block;

	if (block->gradientMode != 0)
	{
		for (int i = 0; i < block->quantizationUnitCount; i++)
		{
			channel->precisions[i] = channel->scaleFactors[i] + channel->precisionMask[i] - block->gradient[i];
			if (channel->precisions[i] > 0)
			{
				switch (block->gradientMode)
				{
				case 1:
					channel->precisions[i] /= 2;
					break;
				case 2:
					channel->precisions[i] = 3 * channel->precisions[i] / 8;
					break;
				case 3:
					channel->precisions[i] /= 4;
					break;
				}
			}
		}
	}
	else
	{
		for (int i = 0; i < block->quantizationUnitCount; i++)
		{
			channel->precisions[i] = channel->scaleFactors[i] - block->gradient[i];
		}
	}

	for (int i = 0; i < block->quantizationUnitCount; i++)
	{
		if (channel->precisions[i] < 1)
		{
			channel->precisions[i] = 1;
		}
	}

	for (int i = 0; i < block->gradientBoundary; i++)
	{
		channel->precisions[i]++;
	}

	for (int i = 0; i < block->quantizationUnitCount; i++)
	{
		channel->precisionsFine[i] = 0;
		if (channel->precisions[i] > 15)
		{
			channel->precisionsFine[i] = channel->precisions[i] - 15;
			channel->precisions[i] = 15;
		}
	}
}

static const unsigned char BaseCurve[48] =
{
	1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 11, 12, 13,
	15, 16, 18, 19, 20, 21, 22, 23, 24, 25, 26, 26, 27, 27, 28, 28, 28, 29, 29,
	29, 29, 30, 30, 30, 30
};

void GenerateGradientCurves()
{
	const int baseLength = sizeof(BaseCurve) / sizeof(BaseCurve[0]);	

	for (int length = 1; length <= baseLength; length++)
	{
		for (int i = 0; i < length; i++)
		{
			GradientCurves[length - 1][i] = BaseCurve[i * baseLength / length];
		}
	}
}
