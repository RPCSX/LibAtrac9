#pragma once

#define CONFIG_DATA_SIZE 4
#define MAX_CHANNEL_COUNT 8
#define MAX_BLOCK_COUNT 5
#define MAX_BLOCK_CHANNEL_COUNT 2
#define MAX_FRAME_SAMPLES 256
#define MAX_BEX_VALUES 4

#define MAX_QUANT_UNITS 30

typedef struct Frame_s Frame;
typedef struct Block_s Block;

typedef enum BlockType_e {
	Mono = 0,
	Stereo = 1,
	LFE = 2
} BlockType;

typedef struct ChannelConfig_s {
	unsigned char blockCount;
	unsigned char channelCount;
	unsigned char types[MAX_BLOCK_COUNT];
} ChannelConfig;

typedef struct ConfigData_s {
	unsigned char configData[CONFIG_DATA_SIZE];
	int sampleRateIndex;
	int channelConfigIndex;
	int frameBytes;
	int superframeIndex;

	ChannelConfig channelConfig;
	int channelCount;
	int sampleRate;
	int highSampleRate;
	int framesPerSuperframe;
	int frameSamplesPower;
	int frameSamples;
	int superframeBytes;
	int superframeSamples;
} ConfigData;

typedef struct RngCxt_s {
	int initialized;
	unsigned short stateA;
	unsigned short stateB;
	unsigned short stateC;
	unsigned short stateD;
} RngCxt;

typedef struct Mdct_s {
	int bits;
	int size;
	double scale;
	double imdctPrevious[MAX_FRAME_SAMPLES];
	double* window;
	double* sinTable;
	double* cosTable;
} Mdct;

typedef struct Channel_s {
	Frame* frame;
	Block* block;
	ConfigData* config;
	int channelIndex;

	Mdct mdct;

	double pcm[MAX_FRAME_SAMPLES];
	double spectra[MAX_FRAME_SAMPLES];

	int codedQuantUnits;
	int scaleFactorCodingMode;

	int scaleFactors[31];
	int scaleFactorsPrev[31];

	int precisions[MAX_QUANT_UNITS];
	int precisionsFine[MAX_QUANT_UNITS];
	int precisionMask[MAX_QUANT_UNITS];

	int codebookSet[MAX_QUANT_UNITS];

	int quantizedSpectra[MAX_FRAME_SAMPLES];
	int quantizedSpectraFine[MAX_FRAME_SAMPLES];

	int bexMode;
	int bexValueCount;
	int bexValues[MAX_BEX_VALUES];

	RngCxt Rng;
} Channel;

struct Block_s {
	Frame* frame;
	ConfigData* config;
	BlockType blockType;
	int blockIndex;
	Channel channels[MAX_BLOCK_CHANNEL_COUNT];
	int channelCount;
	int firstInSuperframe;
	int reuseBandParams;

	int bandCount;
	int stereoBand;
	int extensionBand;
	int quantizationUnitCount;
	int stereoQuantizationUnit;
	int extensionUnit;
	int quantizationUnitsPrev;

	int gradient[31];
	int gradientMode;
	int gradientStartUnit;
	int gradientStartValue;
	int gradientEndUnit;
	int gradientEndValue;
	int gradientBoundary;

	int primaryChannelIndex;
	int hasJointStereoSigns;
	int jointStereoSigns[MAX_QUANT_UNITS];

	int bandExtensionEnabled;
	int hasExtensionData;
	int bexDataLength;
	int bexMode;
};

struct Frame_s {
	int IndexInSuperframe;
	ConfigData* Config;
	Channel* Channels[MAX_CHANNEL_COUNT];
	Block Blocks[MAX_BLOCK_COUNT];
};

typedef struct Atrac9Handle_s {
	int initialized;
	int wlength;
	ConfigData config;
	Frame frame;
} Atrac9Handle;

typedef struct BexGroup_s {
	char GroupBUnit;
	char GroupCUnit;
	char BandCount;
} BexGroup;

typedef struct CodecInfo_s {
	int channels;
	int channelConfigIndex;
	int samplingRate;
	int superframeSize;
	int framesInSuperframe;
	int frameSamples;
	int wlength;
	unsigned char configData[CONFIG_DATA_SIZE];
} CodecInfo;
