/*******************************************************************************
 *
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
*******************************************************************************/
/**
 *
 * @file xhdmic.c
 * @{
 *
 * Contains common utility functions that are typically used by hdmi-related
 * drivers and applications.
 *
 * @note	None.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 * 1.0   EB  21/12/17 Initial release.
 * 1.1   EB  10/04/18 Fixed a bug in XV_HdmiC_ParseAudioInfoFrame
 * 1.2   EB  18/06/19 Added FrlRateTable
 * </pre>
 *
*******************************************************************************/

/******************************* Include Files ********************************/
#include "xv_hdmic.h"

/************************** Constant Definitions ******************************/

/*****************************************************************************/
/**
* This table contains the attributes for various standard resolutions.
* Each entry is of the format:
* 1) Resolution ID
* 2) Video Identification Code.
* 3) Width
* 4) Height
* 5) Frame rate
* 6) Aspect Ratio
*/
const XHdmiC_VicTable VicTable[VICTABLE_SIZE] = {
    {XVIDC_VM_640x480_60_P, 1, 640, 480, 60, 0},      /* Vic 1 */
    {XVIDC_VM_720x480_60_P, 2, 720, 480, 60, 0},      /* Vic 2 */
    {XVIDC_VM_720x480_60_P, 3, 720, 480, 60, 1},      /* Vic 3 */
    {XVIDC_VM_1280x720_60_P, 4, 1280, 720, 60, 1},     /* Vic 4 */
    {XVIDC_VM_1920x1080_60_I, 5, 1920, 540, 120, 1},    /* Vic 5 */
    {XVIDC_VM_1440x480_60_I, 6, 1440, 240, 120, 0},     /* Vic 6 */
    {XVIDC_VM_1440x480_60_I, 7, 1440, 240, 120, 1},     /* Vic 7 */
    {XVIDC_VM_1440x240_60_P, 8, 1440, 240, 60, 0},     /* Vic 8 */
    {XVIDC_VM_1440x240_60_P, 9, 1440, 240, 60, 1},     /* Vic 9 */
    {XVIDC_VM_2880x480_60_I, 10, 2880, 240, 120, 0},    /* Vic 10 */
    {XVIDC_VM_2880x480_60_I, 11, 2880, 240, 120, 1},    /* Vic 11 */
    {XVIDC_VM_2880x240_60_P, 12, 2880, 240, 60, 0},    /* Vic 12 */
    {XVIDC_VM_2880x240_60_P, 13, 2880, 240, 60, 1},    /* Vic 13 */
    {XVIDC_VM_1440x480_60_P, 14, 1440, 480, 60, 0},    /* Vic 14 */
    {XVIDC_VM_1440x480_60_P, 15, 1440, 480, 60, 1},    /* Vic 15 */
    {XVIDC_VM_1920x1080_60_P, 16, 1920, 1080, 60, 1},   /* Vic 16 */
    {XVIDC_VM_720x576_50_P, 17, 720, 576, 50, 0},     /* Vic 17 */
    {XVIDC_VM_720x576_50_P, 18, 720, 576, 50, 1},     /* Vic 18 */
    {XVIDC_VM_1280x720_50_P, 19, 1280, 720, 50, 1},    /* Vic 19 */
    {XVIDC_VM_1920x1080_50_I, 20, 1920, 540, 100, 1},   /* Vic 20 */
    {XVIDC_VM_1440x576_50_I, 21, 1440, 288, 100, 0},    /* Vic 21 */
    {XVIDC_VM_1440x576_50_I, 22, 1440, 288, 100, 1},    /* Vic 22 */
    {XVIDC_VM_1440x288_50_P, 23, 1440, 288, 50, 0},    /* Vic 23 */
    {XVIDC_VM_1440x288_50_P, 24, 1440, 288, 50, 1},    /* Vic 24 */
    {XVIDC_VM_2880x576_50_I, 25, 2880, 288, 100, 0},    /* Vic 25 */
    {XVIDC_VM_2880x576_50_I, 26, 2880, 288, 100, 1},    /* Vic 26 */
    {XVIDC_VM_2880x288_50_P, 27, 2880, 288, 50, 0},    /* Vic 27 */
    {XVIDC_VM_2880x288_50_P, 28, 2880, 288, 50, 1},    /* Vic 28 */
    {XVIDC_VM_1440x576_50_P, 29, 1440, 576, 50, 0},    /* Vic 29 */
    {XVIDC_VM_1440x576_50_P, 30, 1440, 576, 50, 1},    /* Vic 30 */
    {XVIDC_VM_1920x1080_50_P, 31, 1920, 1080, 50, 1},   /* Vic 31 */
    {XVIDC_VM_1920x1080_24_P, 32, 1920, 1080, 24, 1},   /* Vic 32 */
    {XVIDC_VM_1920x1080_25_P, 33, 1920, 1080, 25, 1},   /* Vic 33 */
    {XVIDC_VM_1920x1080_30_P, 34, 1920, 1080, 30, 1},   /* Vic 34 */
    {XVIDC_VM_2880x480_60_P, 35, 2880, 480, 60, 0},    /* Vic 35 */
    {XVIDC_VM_2880x480_60_P, 36, 2880, 480, 60, 1},    /* Vic 36 */
    {XVIDC_VM_2880x576_50_P, 37, 2880, 576, 50, 0},    /* Vic 37 */
    {XVIDC_VM_2880x576_50_P, 38, 2880, 576, 50, 1},    /* Vic 38 */
    {XVIDC_VM_1920x1080_100_I, 40, 1920, 540, 200, 1},  /* Vic 40 */
    {XVIDC_VM_1280x720_100_P, 41, 1280, 720, 100, 1},   /* Vic 41 */
    {XVIDC_VM_720x576_100_P, 42, 720, 576, 100, 0},    /* Vic 42 */
    {XVIDC_VM_720x576_100_P, 43, 720, 576, 100, 1},    /* Vic 43 */
    {XVIDC_VM_1440x576_100_I, 44, 1440, 288, 200, 0},   /* Vic 44 */
    {XVIDC_VM_1440x576_100_I, 45, 1440, 288, 200, 1},   /* Vic 45 */
    {XVIDC_VM_1920x1080_120_I, 46, 1920, 540, 240, 1},  /* Vic 46 */
    {XVIDC_VM_1280x720_120_P, 47, 1280, 720, 120, 1},   /* Vic 47 */
    {XVIDC_VM_720x480_120_P, 48, 720, 480, 120, 0},    /* Vic 48 */
    {XVIDC_VM_720x480_120_P, 49, 720, 480, 120, 1},    /* Vic 49 */
    {XVIDC_VM_1440x480_120_I, 50, 1440, 240, 240, 0},   /* Vic 50 */
    {XVIDC_VM_1440x480_120_I, 51, 1440, 240, 240, 1},   /* Vic 51 */
    {XVIDC_VM_720x576_200_P, 52, 720, 576, 200, 0},    /* Vic 52 */
    {XVIDC_VM_720x576_200_P, 53, 720, 576, 200, 1},    /* Vic 53 */
    {XVIDC_VM_1440x576_200_I, 54, 1440, 288, 400, 0},   /* Vic 54 */
    {XVIDC_VM_1440x576_200_I, 55, 1440, 288, 400, 1},   /* Vic 55 */
    {XVIDC_VM_720x480_240_P, 56, 720, 480, 240, 0},    /* Vic 56 */
    {XVIDC_VM_720x480_240_P, 57, 720, 480, 240, 1},    /* Vic 57 */
    {XVIDC_VM_1440x480_240_I, 58, 1440, 240, 480, 0},   /* Vic 58 */
    {XVIDC_VM_1440x480_240_I, 59, 1440, 240, 480, 1},   /* Vic 59 */
    {XVIDC_VM_1280x720_24_P, 60, 1280, 720, 24, 1},    /* Vic 60 */
    {XVIDC_VM_1280x720_25_P, 61, 1280, 720, 25, 1},    /* Vic 61 */
    {XVIDC_VM_1280x720_30_P, 62, 1280, 720, 30, 1},    /* Vic 62 */
    {XVIDC_VM_1920x1080_120_P, 63, 1920, 1080, 120, 1},  /* Vic 63 */
    {XVIDC_VM_1920x1080_100_P, 64, 1920, 1080, 100, 1},  /* Vic 64 */

    /* 1280 x 720 */
    {XVIDC_VM_1280x720_24_P, 65, 1280, 720, 24, 2},    /* Vic 65 */
    {XVIDC_VM_1280x720_25_P, 66, 1280, 720, 25, 2},    /* Vic 66 */
    {XVIDC_VM_1280x720_30_P, 67, 1280, 720, 30, 2},    /* Vic 67 */
    {XVIDC_VM_1280x720_50_P, 68, 1280, 720, 50, 2},    /* Vic 68 */
    {XVIDC_VM_1280x720_60_P, 69, 1280, 720, 60, 2},    /* Vic 69 */
    {XVIDC_VM_1280x720_100_P, 70, 1280, 720, 100, 2},   /* Vic 70 */
    {XVIDC_VM_1280x720_120_P, 71, 1280, 720, 120, 2},   /* Vic 71 */

    /* 1680 x 720 */
    {XVIDC_VM_1680x720_24_P, 79, 1680, 720, 24, 2},    /* Vic 79 */
    {XVIDC_VM_1680x720_25_P, 80, 1680, 720, 25, 2},    /* Vic 80 */
    {XVIDC_VM_1680x720_30_P, 81, 1680, 720, 30, 2},    /* Vic 81 */
    {XVIDC_VM_1680x720_50_P, 82, 1680, 720, 50, 2},    /* Vic 82 */
    {XVIDC_VM_1680x720_60_P, 83, 1680, 720, 60, 2},    /* Vic 83 */
    {XVIDC_VM_1680x720_100_P, 84, 1680, 720, 100, 2},   /* Vic 84 */
    {XVIDC_VM_1680x720_120_P, 85, 1680, 720, 120, 2},   /* Vic 85 */

    /* 1920 x 1080 */
    {XVIDC_VM_1920x1080_24_P, 72, 1920, 1080, 24, 2},   /* Vic 72 */
    {XVIDC_VM_1920x1080_25_P, 73, 1920, 1080, 25, 2},   /* Vic 73 */
    {XVIDC_VM_1920x1080_30_P, 74, 1920, 1080, 30, 2},   /* Vic 74 */
    {XVIDC_VM_1920x1080_50_P, 75, 1920, 1080, 50, 2},   /* Vic 75 */
    {XVIDC_VM_1920x1080_60_P, 76, 1920, 1080, 60, 2},   /* Vic 76 */
    {XVIDC_VM_1920x1080_100_P, 77, 1920, 1080, 100, 2},  /* Vic 77 */
    {XVIDC_VM_1920x1080_120_P, 78, 1920, 1080, 120, 2},  /* Vic 78 */

    /* 2560 x 1080 */
    {XVIDC_VM_2560x1080_24_P, 86, 2560, 1080, 24, 2},   /* Vic 86 */
    {XVIDC_VM_2560x1080_25_P, 87, 2560, 1080, 25, 2},   /* Vic 87 */
    {XVIDC_VM_2560x1080_30_P, 88, 2560, 1080, 30, 2},   /* Vic 88 */
    {XVIDC_VM_2560x1080_50_P, 89, 2560, 1080, 50, 2},   /* Vic 89 */
    {XVIDC_VM_2560x1080_60_P, 90, 2560, 1080, 60, 2},   /* Vic 90 */
    {XVIDC_VM_2560x1080_100_P, 91, 2560, 1080, 100, 2},  /* Vic 91 */
    {XVIDC_VM_2560x1080_120_P, 92, 2560, 1080, 120, 2},  /* Vic 92 */

    /* 3840 x 2160 */
    {XVIDC_VM_3840x2160_24_P, 93, 3840, 2160, 24, 1},   /* Vic 93  */
    {XVIDC_VM_3840x2160_25_P, 94, 3840, 2160, 25, 1},   /* Vic 94  */
    {XVIDC_VM_3840x2160_30_P, 95, 3840, 2160, 30, 1},   /* Vic 95  */
    {XVIDC_VM_3840x2160_50_P, 96, 3840, 2160, 50, 1},   /* Vic 96  */
    {XVIDC_VM_3840x2160_60_P, 97, 3840, 2160, 60, 1},   /* Vic 97  */
    {XVIDC_VM_3840x2160_100_P, 117, 3840, 2160, 100, 1}, /* Vic 117 */
    {XVIDC_VM_3840x2160_120_P, 118, 3840, 2160, 120, 1}, /* Vic 118 */

    /* 4096 x 2160 */
    {XVIDC_VM_4096x2160_24_P, 98, 4096, 2160, 24, 3},   /* Vic 98  */
    {XVIDC_VM_4096x2160_25_P, 99, 4096, 2160, 25, 3},   /* Vic 99  */
    {XVIDC_VM_4096x2160_30_P, 100, 4096, 2160, 30, 3},  /* Vic 100 */
    {XVIDC_VM_4096x2160_50_P, 101, 4096, 2160, 50, 3},  /* Vic 101 */
    {XVIDC_VM_4096x2160_60_P, 102, 4096, 2160, 60, 3},  /* Vic 102 */
    {XVIDC_VM_4096x2160_100_P, 218, 4096, 2160, 100, 3}, /* Vic 218 */
    {XVIDC_VM_4096x2160_120_P, 219, 4096, 2160, 120, 3}, /* Vic 219 */

    /* 5120 x 2160 */
    {XVIDC_VM_5120x2160_24_P, 121, 5120, 2160, 24, 2},  /* Vic 121 */
    {XVIDC_VM_5120x2160_25_P, 122, 5120, 2160, 25, 2},  /* Vic 122 */
    {XVIDC_VM_5120x2160_30_P, 123, 5120, 2160, 30, 2},  /* Vic 123 */
    {XVIDC_VM_5120x2160_50_P, 125, 5120, 2160, 50, 2},  /* Vic 125 */
    {XVIDC_VM_5120x2160_60_P, 126, 5120, 2160, 60, 2},  /* Vic 126 */
    {XVIDC_VM_5120x2160_100_P, 127, 5120, 2160, 100, 2}, /* Vic 127 */
    {XVIDC_VM_5120x2160_120_P, 193, 5120, 2160, 120, 2}, /* Vic 193 */

    /* 7680 x 4320 */
    {XVIDC_VM_7680x4320_24_P, 194, 7680, 4320, 24, 1},  /* Vic 194 */
    {XVIDC_VM_7680x4320_25_P, 195, 7680, 4320, 25, 1},  /* Vic 195 */
    {XVIDC_VM_7680x4320_30_P, 196, 7680, 4320, 30, 1},  /* Vic 196 */
    {XVIDC_VM_7680x4320_50_P, 198, 7680, 4320, 50, 1},  /* Vic 198 */
    {XVIDC_VM_7680x4320_60_P, 199, 7680, 4320, 60, 1},  /* Vic 199 */
    {XVIDC_VM_7680x4320_100_P, 200, 7680, 4320, 100, 1}, /* Vic 200 */
    {XVIDC_VM_7680x4320_120_P, 201, 7680, 4320, 120, 1}, /* Vic 201 */

    /* 10240 x 4320 */
    {XVIDC_VM_10240x4320_24_P, 210, 10240, 4320, 24, 2},  /* Vic 210 */
    {XVIDC_VM_10240x4320_25_P, 211, 10240, 4320, 25, 2},  /* Vic 211 */
    {XVIDC_VM_10240x4320_30_P, 212, 10240, 4320, 30, 2},  /* Vic 212 */
    {XVIDC_VM_10240x4320_50_P, 214, 10240, 4320, 50, 2},  /* Vic 214 */
    {XVIDC_VM_10240x4320_60_P, 215, 10240, 4320, 60, 2},  /* Vic 215 */
    {XVIDC_VM_10240x4320_100_P, 216, 10240, 4320, 100, 2}, /* Vic 216 */
    {XVIDC_VM_10240x4320_120_P, 217, 10240, 4320, 120, 2}, /* Vic 217 */
};

/**
* This table contains the attributes for various FRL Rate.
* Each entry is of the format:
* 1) Number of lanes
* 2) Line Rate.
*/
const XHdmiC_FrlRate FrlRateTable[] = {
    {3, 0},	/* XHDMIC_MAXFRLRATE_NOT_SUPPORTED */
    {3, 3},	/* XHDMIC_MAXFRLRATE_3X3GBITSPS */
    {3, 6},	/* XHDMIC_MAXFRLRATE_3X6GBITSPS */
    {4, 6},	/* XHDMIC_MAXFRLRATE_4X6GBITSPS */
    {4, 8},	/* XHDMIC_MAXFRLRATE_4X8GBITSPS */
    {4, 10},	/* XHDMIC_MAXFRLRATE_4X10GBITSPS */
    {4, 12},	/* XHDMIC_MAXFRLRATE_4X12GBITSPS */
};

/**
* This table contains the CTS and N value of each TMDS character rate.
* Each entry is of the format:
* 1) Sample Frequency TMDS CLk, (32kHz, 44.1kHz, and 48kHz)
*/
const XHdmiC_TMDS_N_Table TMDSChar_N_Table[] =
{
	/* TMDSClk    32k   44k1   48k   88k2    96k  176k4   192k */
	{        0, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{ 25200000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{ 27000000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{ 31500000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{ 33750000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{ 37800000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{ 40500000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{ 50400000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{ 54000000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{ 67500000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{ 74250000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{ 81000000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{ 92812500, { 8192, 6272, 12288, 12544, 24576, 25088, 49152}},
	{108000000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{111375000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{148500000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{185625000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{222750000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{297000000, { 3072, 4704,  5120,  9408, 10240, 18816, 20480}},
	{371250000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{445500000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576}},
	{594000000, { 3072, 9408,  6144, 18816, 12288, 37632, 24576}}
};

/**
* This returns the the N Value based Audio Sampling Rate and TMDS
* Character Rate
*/
u32 XHdmiC_TMDS_GetNVal(u32 TMDSCharRate,
	XHdmiC_SamplingFrequency AudSampleFreq)
{
  XHdmiC_TMDS_N_Table const *item;
  u32 i = 0;

  /* Proceed only with supported Audio Sampling Frequency */
  if (AudSampleFreq == XHDMIC_SAMPLING_FREQUENCY ||
                  AudSampleFreq > XHDMIC_SAMPLING_FREQUENCY_192K) {
          /* Since AudSampFreq not valid, return N Value = 0 */
          return 0;
  }

  for(i = 0; i < sizeof(TMDSChar_N_Table)/sizeof(XHdmiC_TMDS_N_Table); i++) {
    item = &TMDSChar_N_Table[i];
    /* 10kHz  Tolerance */
	if(TMDSCharRate <= (item->TMDSCharRate + 10000) &&
			(TMDSCharRate >= (item->TMDSCharRate - 10000))) {
      return item->ACR_NVal[AudSampleFreq-1];
    }
  }

  /* If TMDS character rate could not be found return default values */
  item = &TMDSChar_N_Table[0];

  return item->ACR_NVal[AudSampleFreq-1];
}

/**
* This returns the Audio Sampling Rate and TMDS
* Character Rate.
*
* This function is not expected to be called for CTS/N as 0.
*/
XHdmiC_SamplingFrequency XHdmiC_TMDS_GetAudSampFreq(u32 TMDSCharRate,
		u32 N, u32 CTSVal)
{
  XHdmiC_TMDS_N_Table const *item;
  u8 i = 0;
  u8 j = 0;
  u64 fs;
  u32 FsTol = 1000; /* Max Fs tolerance limit */

  /* Look for standard TMDS rates */
  for(i = 0; i < sizeof(TMDSChar_N_Table)/sizeof(XHdmiC_TMDS_N_Table); i++) {
		item = &TMDSChar_N_Table[i];
		if(TMDSCharRate <= (item->TMDSCharRate + 10000) &&
			(TMDSCharRate >= (item->TMDSCharRate - 10000))) {
			for (j = 0; j < 7; j++) {
				if(N == item->ACR_NVal[j]) {
					/*
					 * starts from
					 * XHDMIC_SAMPLING_FREQUENCY_32K
					 */
					return (j + 1);
					break;
				}
			}
		}
  }

  if (!CTSVal)
          return XHDMIC_SAMPLING_FREQUENCY;

 /* compute and approximate */
  fs = ((u64)TMDSCharRate * (u64)N) ;
  fs = fs / (128 * CTSVal);

  if (((XHDMIC_SAMPLING_FREQ_32K - FsTol) <= fs) &&
		  (fs <= (XHDMIC_SAMPLING_FREQ_32K + FsTol)))
	  return XHDMIC_SAMPLING_FREQUENCY_32K;
  else if (((XHDMIC_SAMPLING_FREQ_44_1K - FsTol) <= fs) &&
		  (fs <= (XHDMIC_SAMPLING_FREQ_44_1K + FsTol)))
	  return XHDMIC_SAMPLING_FREQUENCY_44_1K;
  else if (((XHDMIC_SAMPLING_FREQ_48K - FsTol) <= fs) &&
		  (fs <= (XHDMIC_SAMPLING_FREQ_48K + FsTol)))
	  return XHDMIC_SAMPLING_FREQUENCY_48K;
  else if (((XHDMIC_SAMPLING_FREQ_88_2K - FsTol) <= fs) &&
		  (fs <= (XHDMIC_SAMPLING_FREQ_88_2K + FsTol)))
	  return XHDMIC_SAMPLING_FREQUENCY_88_2K;
  else if (((XHDMIC_SAMPLING_FREQ_96K - FsTol) <= fs) &&
		  (fs <= (XHDMIC_SAMPLING_FREQ_96K + FsTol)))
	  return XHDMIC_SAMPLING_FREQUENCY_96K;
  else if (((XHDMIC_SAMPLING_FREQ_176_4K - FsTol) <= fs) &&
		  (fs <= (XHDMIC_SAMPLING_FREQ_176_4K + FsTol)))
	  return XHDMIC_SAMPLING_FREQUENCY_176_4K;
  else if (((XHDMIC_SAMPLING_FREQ_192K - FsTol) <= fs) &&
		  (fs <= (XHDMIC_SAMPLING_FREQ_192K + FsTol)))
	  return XHDMIC_SAMPLING_FREQUENCY_192K;
  else
	  return  XHDMIC_SAMPLING_FREQUENCY; /* invalid */
}

/**
* Top level data structure for XHdmiC_FRL_CTS_N_Table_t
*/
typedef struct {
	/* 5 = FRL Rate 12,10,8,6,3 */
	XHdmiC_FRL_CTS_N_Val CTS_NVal[5];
} XHdmiC_FRL_CTS_N_Table_t;


/**
* This table contains the CTS and N value of each FRL character rate.
* Each entry is of the format:
* 1) Sample Frequency (32kHz, 44.1kHz, and 48kHz)
*/
const XHdmiC_FRL_CTS_N_Table_t FRL_CTS_N_Table[] =
{
	{/* 32kHz */
		{	/* 3 Gbps */
			{171875, {4224, 8448, 16896, 33792, 67584, 135168}},
			/* 6 Gbps */
			{328125, {4032, 8064, 16128, 32256, 64512, 129024}},
			/* 8 Gbps */
			{437500, {4032, 8064, 16128, 32256, 64512, 129024}},
			/* 10 Gbps */
			{468750, {3456, 6912, 13824, 27648, 55296, 110592}},
			/* 12 Gbps */
			{500000, {3072, 6144, 12288, 24576, 49152, 98304 }},
		},
	},
	{/* 44.1kHz */
		{
			{156250 ,{5292 ,10584 ,21168 ,42336 ,84672 ,169344}},
			{312500 ,{5292 ,10584 ,21168 ,42336 ,84672 ,169344}},
			{312500 ,{3969 ,7938  ,15876 ,31752 ,63504 ,127008}},
			{390625 ,{3969 ,7938  ,15876 ,31752 ,63504 ,127008}},
			{468750 ,{3969 ,7938  ,15876 ,31752 ,63504 ,127008}},
		},
	},
	{/* 48kHz */
		{
			{156250 ,{5760 ,11520 ,23040 ,46080 ,92160 ,184320}},
			{328125 ,{6048 ,12096 ,24192 ,48384 ,96768 ,193536}},
			{437500 ,{6048 ,12096 ,24192 ,48384 ,96768 ,193536}},
			{468750 ,{5184 ,10368 ,20736 ,41472 ,82944 ,165888}},
			{515625 ,{4752 ,9504  ,19008 ,38016 ,76032 ,152064}},
		},
	},
};

/**
* This returns the N value in FRL mode based on its Audio Sample Frequency
*/
u32 XHdmiC_FRL_GetNVal(XHdmiC_FRLCharRate FRLCharRate,
		XHdmiC_SamplingFrequencyVal AudSampleFreqVal)
{
	XHdmiC_FRL_CTS_N_Table_t const *item;
	u8 SampleFreq;
	u8 MultSampleFreq;
	u8 FrlAudioCharRate = FRLCharRate;

	/* Check for valid FRL Character Rate */
	if (FrlAudioCharRate > R_666_667 ) {
		/* Since AudSampFreq not valid, return N Value = 0 */
		FrlAudioCharRate = R_166_667;
	}

	if (AudSampleFreqVal%XHDMIC_SAMPLING_FREQ_48K == 0) {
		SampleFreq = 2;
		MultSampleFreq = AudSampleFreqVal/XHDMIC_SAMPLING_FREQ_48K;
	} else if (AudSampleFreqVal%XHDMIC_SAMPLING_FREQ_44_1K == 0) {
		SampleFreq = 1;
		MultSampleFreq = AudSampleFreqVal/XHDMIC_SAMPLING_FREQ_44_1K;
	} else if (AudSampleFreqVal%XHDMIC_SAMPLING_FREQ_32K == 0) {
		SampleFreq = 0;
		MultSampleFreq = AudSampleFreqVal/XHDMIC_SAMPLING_FREQ_32K;
	} else {
		/* Not Valid Scenario */
		SampleFreq = 0;
		MultSampleFreq = 0;
	}

	/* MultSampleFreq is the divisible value from the 3 base
	 * Sample Frequency.
	 * However the N value array for FRL are based on the power of 2.
	 * {base_freq*(2^0),base_freq*(2^1),base_freq*(2^2),base_freq*(2^3),..}
	 */
	switch (MultSampleFreq) {
		case 1:
		case 2:
			MultSampleFreq = MultSampleFreq - 1;
			break;

		case 4:
			MultSampleFreq = 2;
			break;

		case 8:
			MultSampleFreq = 3;
			break;

		case 16:
			MultSampleFreq = 4;
			break;

		case 32:
			MultSampleFreq = 5;
			break;

		default:
			MultSampleFreq = 0;
			break;
	}

	item = &FRL_CTS_N_Table[SampleFreq];
	return item->CTS_NVal[FrlAudioCharRate].ACR_NVal[MultSampleFreq];

}

/**
* This returns the Audio Sample Frequency for FRL
*/
XHdmiC_SamplingFrequencyVal
	XHdmiC_FRL_GetAudSampFreq(XHdmiC_FRLCharRate FRLCharRate,
					u32 CTS, u32 N)
{
    XHdmiC_FRL_CTS_N_Table_t const *item;
    u8 Index_0 = 0;
    u8 Index_1 = 0;
    XHdmiC_SamplingFrequencyVal SampleFreq = XHDMIC_SAMPLING_FREQ;

    item = &FRL_CTS_N_Table[SampleFreq];
    for (Index_0 = 0; Index_0 < 3; Index_0++) {
        item = &FRL_CTS_N_Table[Index_0];
        /* Adding tolerance of 10k CTS */
        if ((CTS <  (item->CTS_NVal[FRLCharRate].ACR_CTSVal + 10000)) &&
        (CTS >  (item->CTS_NVal[FRLCharRate].ACR_CTSVal - 10000))) {
        	for (Index_1 = 0; Index_1 < 6; Index_1++) {
        		if (N ==
        			item->CTS_NVal[FRLCharRate].ACR_NVal[Index_1]){
        			if (Index_0 == 0) {
        				SampleFreq =
        				XHDMIC_SAMPLING_FREQ_32K << Index_1;
        			} else if (Index_0 == 1) {
        				SampleFreq =
        				XHDMIC_SAMPLING_FREQ_44_1K << Index_1;
        			} else {
        				SampleFreq =
        				XHDMIC_SAMPLING_FREQ_48K << Index_1;
        			}
        			break;
        		}
        	}
        }
    }
    return SampleFreq;
}

/**
* This converts the Aud. Sampling Freq. Value to
* Audio Info-frame Sampling Freq. Format.
*/
XHdmiC_SamplingFrequency
	XHdmiC_GetAudIFSampFreq (XHdmiC_SamplingFrequencyVal AudSampFreqVal)
{
	XHdmiC_SamplingFrequency ExtACRSampFreq;

	switch (AudSampFreqVal) {
		case XHDMIC_SAMPLING_FREQ_32K:
			ExtACRSampFreq =
				XHDMIC_SAMPLING_FREQUENCY_32K;
		break;

		case XHDMIC_SAMPLING_FREQ_44_1K:
			ExtACRSampFreq =
				XHDMIC_SAMPLING_FREQUENCY_44_1K;
		break;

		case XHDMIC_SAMPLING_FREQ_48K:
			ExtACRSampFreq =
				XHDMIC_SAMPLING_FREQUENCY_48K;
		break;

		case XHDMIC_SAMPLING_FREQ_88_2K:
			ExtACRSampFreq =
				XHDMIC_SAMPLING_FREQUENCY_88_2K;
		break;

		case XHDMIC_SAMPLING_FREQ_96K:
			ExtACRSampFreq =
				XHDMIC_SAMPLING_FREQUENCY_96K;
		break;

		case XHDMIC_SAMPLING_FREQ_176_4K:
			ExtACRSampFreq =
				XHDMIC_SAMPLING_FREQUENCY_176_4K;
		break;

		case XHDMIC_SAMPLING_FREQ_192K:
			ExtACRSampFreq =
				XHDMIC_SAMPLING_FREQUENCY_192K;
		break;

		default:
			ExtACRSampFreq =
					XHDMIC_SAMPLING_FREQ;
		break;
	}

	return ExtACRSampFreq;
}

/**
* This converts the Audio Info-frame Sampling Freq. Format. to
* Audio Sampling Freq. Value.
*/
XHdmiC_SamplingFrequencyVal
	XHdmiC_GetAudSampFreqVal(XHdmiC_SamplingFrequency AudSampFreqVal)
{
	XHdmiC_SamplingFrequencyVal ExtACRSampFreq;

	switch (AudSampFreqVal) {
		case XHDMIC_SAMPLING_FREQUENCY_32K:
			ExtACRSampFreq =
				XHDMIC_SAMPLING_FREQ_32K;
		break;

		case XHDMIC_SAMPLING_FREQUENCY_44_1K:
			ExtACRSampFreq =
				XHDMIC_SAMPLING_FREQ_44_1K;
		break;

		case XHDMIC_SAMPLING_FREQUENCY_48K:
			ExtACRSampFreq =
				XHDMIC_SAMPLING_FREQ_48K;
		break;

		case XHDMIC_SAMPLING_FREQUENCY_88_2K:
			ExtACRSampFreq =
				XHDMIC_SAMPLING_FREQ_88_2K;
		break;

		case XHDMIC_SAMPLING_FREQUENCY_96K:
			ExtACRSampFreq =
				XHDMIC_SAMPLING_FREQ_96K;
		break;

		case XHDMIC_SAMPLING_FREQUENCY_176_4K:
			ExtACRSampFreq =
				XHDMIC_SAMPLING_FREQ_176_4K;
		break;

		case XHDMIC_SAMPLING_FREQUENCY_192K:
			ExtACRSampFreq =
				XHDMIC_SAMPLING_FREQ_192K;
		break;

		default:
			ExtACRSampFreq =
				XHDMIC_SAMPLING_FREQ_32K;
		break;
	}

	return ExtACRSampFreq;
}

/*************************** Function Definitions *****************************/
/**
*
* This function retrieves the Auxiliary Video Information Info Frame.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiC_ParseAVIInfoFrame(XHdmiC_Aux *AuxPtr, XHdmiC_AVI_InfoFrame *infoFramePtr)
{
	if (AuxPtr->Header.Byte[0] == AUX_AVI_INFOFRAME_TYPE) {

		/* Header, Version */
		infoFramePtr->Version = AuxPtr->Header.Byte[1];

		/* PB1 */
		infoFramePtr->ColorSpace = (AuxPtr->Data.Byte[1] >> 5) & 0x7;
		infoFramePtr->ActiveFormatDataPresent = (AuxPtr->Data.Byte[1] >> 4) & 0x1;
		infoFramePtr->BarInfo = (AuxPtr->Data.Byte[1] >> 2) & 0x3;
		infoFramePtr->ScanInfo = AuxPtr->Data.Byte[1] & 0x3;

		/* PB2 */
		infoFramePtr->Colorimetry = (AuxPtr->Data.Byte[2] >> 6) & 0x3;
		infoFramePtr->PicAspectRatio = (AuxPtr->Data.Byte[2] >> 4) & 0x3;
		infoFramePtr->ActiveAspectRatio = AuxPtr->Data.Byte[2] & 0xf;

		/* PB3 */
		infoFramePtr->Itc = (AuxPtr->Data.Byte[3] >> 7) & 0x1;
		infoFramePtr->ExtendedColorimetry = (AuxPtr->Data.Byte[3] >> 4) & 0x7;
		infoFramePtr->QuantizationRange = (AuxPtr->Data.Byte[3] >> 2) & 0x3;
		infoFramePtr->NonUniformPictureScaling = AuxPtr->Data.Byte[3] & 0x3;

		/* PB4 */
		infoFramePtr->VIC = AuxPtr->Data.Byte[4] & 0x7f;

		/* PB5 */
		infoFramePtr->YccQuantizationRange = (AuxPtr->Data.Byte[5] >> 6) & 0x3;
		infoFramePtr->ContentType = (AuxPtr->Data.Byte[5] >> 4) & 0x3;
		infoFramePtr->PixelRepetition = AuxPtr->Data.Byte[5] & 0xf;

		/* PB6/7 */
		infoFramePtr->TopBar = (AuxPtr->Data.Byte[8] << 8) | AuxPtr->Data.Byte[6];

		/* PB8/9 */
		infoFramePtr->BottomBar = (AuxPtr->Data.Byte[10] << 8) | AuxPtr->Data.Byte[9];

		/* PB10/11 */
		infoFramePtr->LeftBar = (AuxPtr->Data.Byte[12] << 8) | AuxPtr->Data.Byte[11];

		/* PB12/13 */
		infoFramePtr->RightBar = (AuxPtr->Data.Byte[14] << 8) | AuxPtr->Data.Byte[13];

	}
}

/*****************************************************************************/
/**
*
* This function retrieves the General Control Packet.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiC_ParseGCP(XHdmiC_Aux *AuxPtr, XHdmiC_GeneralControlPacket *GcpPtr)
{
	if (AuxPtr->Header.Byte[0] == AUX_GENERAL_CONTROL_PACKET_TYPE) {

		/* SB0 */
		GcpPtr->Clear_AVMUTE = (AuxPtr->Data.Byte[0] >> 4) & 0x1;
		GcpPtr->Set_AVMUTE = AuxPtr->Data.Byte[0] & 0x1;

		/* SB1 */
		GcpPtr->PixelPackingPhase = (AuxPtr->Data.Byte[1] >> 4) & 0xf;
		GcpPtr->ColorDepth = AuxPtr->Data.Byte[1] & 0xf;

		/* SB2 */
		GcpPtr->Default_Phase = AuxPtr->Data.Byte[2] & 0x1;
	}
}

/*****************************************************************************/
/**
*
* This function retrieves the Audio Info Frame.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiC_ParseAudioInfoFrame(XHdmiC_Aux *AuxPtr, XHdmiC_AudioInfoFrame *AudIFPtr)
{
	if (AuxPtr->Header.Byte[0] == AUX_AUDIO_INFOFRAME_TYPE) {

		/* HB1, Version */
		AudIFPtr->Version = AuxPtr->Header.Byte[1];

		/* PB1 */
		AudIFPtr->CodingType = (AuxPtr->Data.Byte[1] >> 4) & 0xf;
		AudIFPtr->ChannelCount = (AuxPtr->Data.Byte[1]) & 0x7;

		/* PB2 */
		AudIFPtr->SampleFrequency = (AuxPtr->Data.Byte[2] >> 2) & 0x7;
		AudIFPtr->SampleSize = AuxPtr->Data.Byte[2] & 0x3;

		/* PB4 */
		AudIFPtr->ChannelAllocation = AuxPtr->Data.Byte[4];

		/* PB5 */
		AudIFPtr->Downmix_Inhibit = (AuxPtr->Data.Byte[5] >> 7) & 0x1;
		AudIFPtr->LevelShiftVal = (AuxPtr->Data.Byte[5] >> 4) & 0xf;
		AudIFPtr->LFE_Playback_Level = AuxPtr->Data.Byte[5] & 0x3;
	}
}

/*****************************************************************************/
/**
*
* This function retrieves the Audio Metadata.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiC_ParseAudioMetadata(XHdmiC_Aux *AuxPtr,
		XHdmiC_AudioMetadata *AudMetadata)
{
	if (AuxPtr->Header.Byte[0] == AUX_AUDIO_METADATA_PACKET_TYPE) {
		/* 3D Audio */
		AudMetadata->Audio3D = AuxPtr->Header.Byte[1] & 0x1;

		/* HB2 */
		AudMetadata->Num_Audio_Str =
				(AuxPtr->Header.Byte[2] >> 2) & 0x3;
		AudMetadata->Num_Views = AuxPtr->Header.Byte[2] & 0x3;

		/* PB0 */
		AudMetadata->Audio3D_ChannelCount =
				AuxPtr->Data.Byte[0] & 0x1F;

		/* PB1 */
		AudMetadata->ACAT = AuxPtr->Data.Byte[1] & 0x0F;

		/* PB2 */
		AudMetadata->Audio3D_ChannelAllocation =
				AuxPtr->Data.Byte[2] & 0xFF;
	}
}

/*****************************************************************************/
/**
 *
 * This function retrieves the SPD Infoframes.
 *
 * @param  None.
 *
 * @return None.
 *
 * @note   None.
 *
******************************************************************************/
void XV_HdmiC_ParseSPDIF(XHdmiC_Aux *AuxPtr, XHdmiC_SPDInfoFrame *SPDInfoFrame)
{
	if (AuxPtr->Header.Byte[0] == AUX_SPD_INFOFRAME_TYPE) {
		/* 3D Audio */
		SPDInfoFrame->Version = AuxPtr->Header.Byte[1];

		/* Vendor Name Characters */
		SPDInfoFrame->VN1 = AuxPtr->Data.Byte[1];
		SPDInfoFrame->VN2 = AuxPtr->Data.Byte[2];
		SPDInfoFrame->VN3 = AuxPtr->Data.Byte[3];
		SPDInfoFrame->VN4 = AuxPtr->Data.Byte[4];
		SPDInfoFrame->VN5 = AuxPtr->Data.Byte[5];
		SPDInfoFrame->VN6 = AuxPtr->Data.Byte[6];
		SPDInfoFrame->VN7 = AuxPtr->Data.Byte[8];
		SPDInfoFrame->VN8 = AuxPtr->Data.Byte[9];

		/* Product Description Character */
		SPDInfoFrame->PD1 = AuxPtr->Data.Byte[10];
		SPDInfoFrame->PD2 = AuxPtr->Data.Byte[11];
		SPDInfoFrame->PD3 = AuxPtr->Data.Byte[12];
		SPDInfoFrame->PD4 = AuxPtr->Data.Byte[13];
		SPDInfoFrame->PD5 = AuxPtr->Data.Byte[14];
		SPDInfoFrame->PD6 = AuxPtr->Data.Byte[16];
		SPDInfoFrame->PD7 = AuxPtr->Data.Byte[17];
		SPDInfoFrame->PD8 = AuxPtr->Data.Byte[18];
		SPDInfoFrame->PD9 = AuxPtr->Data.Byte[19];
		SPDInfoFrame->PD10 = AuxPtr->Data.Byte[20];
		SPDInfoFrame->PD11 = AuxPtr->Data.Byte[21];
		SPDInfoFrame->PD12 = AuxPtr->Data.Byte[22];
		SPDInfoFrame->PD13 = AuxPtr->Data.Byte[24];
		SPDInfoFrame->PD14 = AuxPtr->Data.Byte[25];
		SPDInfoFrame->PD15 = AuxPtr->Data.Byte[26];
		SPDInfoFrame->PD16 = AuxPtr->Data.Byte[27];

		/* Source Information */
		SPDInfoFrame->SourceInfo = AuxPtr->Data.Byte[28];
	}
}

/*****************************************************************************/
/**
*
* This function retrieves the DRM Infoframes.
*
* @param  None.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XV_HdmiC_ParseDRMIF(XHdmiC_Aux *AuxPtr, struct v4l2_hdr10_payload *DRMInfoFrame)
{
	if (AuxPtr->Header.Byte[0] == AUX_DRM_INFOFRAME_TYPE) {

		/* Vendor Name Characters */
		DRMInfoFrame->eotf = AuxPtr->Data.Byte[1] & 0x7;

		DRMInfoFrame->metadata_type =
				AuxPtr->Data.Byte[2] & 0x7;

		DRMInfoFrame->display_primaries[0].x =
				(AuxPtr->Data.Byte[3] & 0xFF) |
				(AuxPtr->Data.Byte[4] << 8);

		DRMInfoFrame->display_primaries[0].y =
				(AuxPtr->Data.Byte[5] & 0xFF) |
				(AuxPtr->Data.Byte[6] << 8);

		DRMInfoFrame->display_primaries[1].x =
				(AuxPtr->Data.Byte[8] & 0xFF) |
				(AuxPtr->Data.Byte[9] << 8);

		DRMInfoFrame->display_primaries[1].y =
				(AuxPtr->Data.Byte[10] & 0xFF) |
				(AuxPtr->Data.Byte[11] << 8);

		DRMInfoFrame->display_primaries[2].x =
				(AuxPtr->Data.Byte[12] & 0xFF) |
				(AuxPtr->Data.Byte[13] << 8);

		DRMInfoFrame->display_primaries[2].y =
				(AuxPtr->Data.Byte[14] & 0xFF) |
				(AuxPtr->Data.Byte[16] << 8);

		DRMInfoFrame->white_point.x =
				(AuxPtr->Data.Byte[17] & 0xFF) |
				(AuxPtr->Data.Byte[18] << 8);

		DRMInfoFrame->white_point.y =
				(AuxPtr->Data.Byte[19] & 0xFF) |
				(AuxPtr->Data.Byte[20] << 8);

		DRMInfoFrame->max_mdl =
				(AuxPtr->Data.Byte[21] & 0xFF) |
				(AuxPtr->Data.Byte[22] << 8);

		DRMInfoFrame->min_mdl =
				(AuxPtr->Data.Byte[24] & 0xFF) |
				(AuxPtr->Data.Byte[25] << 8);

		DRMInfoFrame->max_cll =
				(AuxPtr->Data.Byte[26] & 0xFF) |
				(AuxPtr->Data.Byte[27] << 8);

		DRMInfoFrame->max_fall =
				(AuxPtr->Data.Byte[28] & 0xFF) |
				(AuxPtr->Data.Byte[29] << 8);
	}
}

/*****************************************************************************/
/**
*
* This function generates and sends Auxilliary Video Infoframes
*
* @param  InstancePtr is a pointer to the HDMI TX Subsystem instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
XHdmiC_Aux XV_HdmiC_AVIIF_GeneratePacket(XHdmiC_AVI_InfoFrame *infoFramePtr)
{
//	Xil_AssertNonvoid(infoFramePtr != NULL);

	u8 Index;
	u8 Crc;
	XHdmiC_Aux aux;

	(void)memset((void *)&aux, 0, sizeof(XHdmiC_Aux));

	/* Header, Packet type*/
	aux.Header.Byte[0] = AUX_AVI_INFOFRAME_TYPE;

	/* Version */
	aux.Header.Byte[1] = infoFramePtr->Version;

	/* Length */
	aux.Header.Byte[2] = 13;

	/* Checksum (this will be calculated by the HDMI TX IP) */
	aux.Header.Byte[3] = 0;

	/* PB1 */
   aux.Data.Byte[1] = (infoFramePtr->ColorSpace & 0x7) << 5 |
		   ((infoFramePtr->ActiveFormatDataPresent << 4) & 0x10) |
		   ((infoFramePtr->BarInfo << 2) & 0xc) |
		   (infoFramePtr->ScanInfo & 0x3);

   /* PB2 */
   aux.Data.Byte[2] = ((infoFramePtr->Colorimetry & 0x3) << 6  |
		   ((infoFramePtr->PicAspectRatio << 4) & 0x30) |
		   (infoFramePtr->ActiveAspectRatio & 0xf));

   /* PB3 */
   aux.Data.Byte[3] = (infoFramePtr->Itc & 0x1) << 7 |
		   ((infoFramePtr->ExtendedColorimetry << 4) & 0x70) |
		   ((infoFramePtr->QuantizationRange << 2) & 0xc) |
		   (infoFramePtr->NonUniformPictureScaling & 0x3);

   /* PB4 */
   aux.Data.Byte[4] = infoFramePtr->VIC;

   /* PB5 */
   aux.Data.Byte[5] = (infoFramePtr->YccQuantizationRange & 0x3) << 6 |
		   ((infoFramePtr->ContentType << 4) & 0x30) |
		   (infoFramePtr->PixelRepetition & 0xf);

   /* PB6 */
   aux.Data.Byte[6] = infoFramePtr->TopBar & 0xff;

   aux.Data.Byte[7] = 0;

   /* PB8 */
   aux.Data.Byte[8] = (infoFramePtr->TopBar & 0xff00) >> 8;

   /* PB9 */
   aux.Data.Byte[9] = infoFramePtr->BottomBar & 0xff;

   /* PB10 */
   aux.Data.Byte[10] = (infoFramePtr->BottomBar & 0xff00) >> 8;

   /* PB11 */
   aux.Data.Byte[11] = infoFramePtr->LeftBar & 0xff;

   /* PB12 */
   aux.Data.Byte[12] = (infoFramePtr->LeftBar & 0xff00) >> 8;

   /* PB13 */
   aux.Data.Byte[13] = infoFramePtr->RightBar & 0xff;

   /* PB14 */
   aux.Data.Byte[14] = (infoFramePtr->RightBar & 0xff00) >> 8;

   /* Index references the length to calculate start of loop from where values are reserved */
   for (Index = aux.Header.Byte[2] + 2; Index < 32; Index++) {
	   aux.Data.Byte[Index] = 0;
   }

   /* Calculate AVI infoframe checksum */
   Crc = 0;

   /* Header */
   for (Index = 0; Index < 3; Index++) {
     Crc += aux.Header.Byte[Index];
   }

   /* Data */
   for (Index = 1; Index < (aux.Header.Byte[2] + 2); Index++) {
     Crc += aux.Data.Byte[Index];
   }

   Crc = 256 - Crc;

   /* PB0 */
   aux.Data.Byte[0] = Crc;

   return aux;
}

/*****************************************************************************/
/**
*
* This function generates and sends Audio Infoframes
*
* @param  InstancePtr is a pointer to the HDMI TX Subsystem instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
XHdmiC_Aux XV_HdmiC_AudioIF_GeneratePacket(XHdmiC_AudioInfoFrame *AudioInfoFrame)
{
//	Xil_AssertNonvoid(AudioInfoFrame != NULL);

	u8 Index;
	u8 Crc;
	XHdmiC_Aux aux;

	(void)memset((void *)&aux, 0, sizeof(XHdmiC_Aux));

	/* Header, Packet Type */
	aux.Header.Byte[0] = AUX_AUDIO_INFOFRAME_TYPE;

	/* Version */
	aux.Header.Byte[1] = 0x01;

	/* Length */
	aux.Header.Byte[2] = 0x0A;

	aux.Header.Byte[3] = 0;

	/* PB1 */
	aux.Data.Byte[1] = AudioInfoFrame->CodingType << 4 |
			(AudioInfoFrame->ChannelCount & 0x7);

	/* PB2 */
	aux.Data.Byte[2] = ((AudioInfoFrame->SampleFrequency << 2) & 0x1c) |
			(AudioInfoFrame->SampleSize & 0x3);

	/* PB3 */
	aux.Data.Byte[3] = 0;


	/* PB4 */
	aux.Data.Byte[4] = AudioInfoFrame->ChannelAllocation;

	/* PB5 */
	aux.Data.Byte[5] = (AudioInfoFrame->Downmix_Inhibit << 7) |
			((AudioInfoFrame->LevelShiftVal << 3) & 0x78) |
			(AudioInfoFrame->LFE_Playback_Level & 0x3);

	for (Index = 6; Index < 32; Index++)
	{
		aux.Data.Byte[Index] = 0;
	}

	/* Calculate Audio infoframe checksum */
	  Crc = 0;

	  /* Header */
	  for (Index = 0; Index < 3; Index++) {
	    Crc += aux.Header.Byte[Index];
	  }

	  /* Data */
	  for (Index = 1; Index < aux.Header.Byte[2] + 1; Index++) {
		  Crc += aux.Data.Byte[Index];
	  }

	  Crc = 256 - Crc;

	  aux.Data.Byte[0] = Crc;

	  return aux;
}

/*****************************************************************************/
/**
*
* This function generates and sends Audio Metadata Packet
*
* @param  InstancePtr is a pointer to the HDMI TX Subsystem instance.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
XHdmiC_Aux XV_HdmiC_AudioMetadata_GeneratePacket(XHdmiC_AudioMetadata
		*AudMetadata)
{
	u8 Index;
	XHdmiC_Aux aux;

	(void)memset((void *)&aux, 0, sizeof(XHdmiC_Aux));

	/* Header, Packet Type */
	aux.Header.Byte[0] = AUX_AUDIO_METADATA_PACKET_TYPE;

	/* 3D Audio */
	aux.Header.Byte[1] = AudMetadata->Audio3D & 0x1;

	/* HB2 */
	aux.Header.Byte[2] = ((AudMetadata->Num_Audio_Str & 0x3) << 2) |
			(AudMetadata->Num_Views & 0x3);

	/* HB3 */
	aux.Header.Byte[3] = 0;

	/* PB0 */
	aux.Data.Byte[0] = AudMetadata->Audio3D_ChannelCount & 0x1F;

	/* PB1 */
	aux.Data.Byte[1] = AudMetadata->ACAT & 0x0F;

	/* PB2 */
	aux.Data.Byte[2] = AudMetadata->Audio3D_ChannelAllocation & 0xFF;

	for (Index = 3; Index < 32; Index++)
		aux.Data.Byte[Index] = 0;

	return aux;
}

/*****************************************************************************/
/**
 *
 * This function generates and sends SPD Infoframes
 *
 * @param  InstancePtr is a pointer to the HDMI TX Subsystem instance.
 *
 * @return None.
 *
 * @note   None.
 *
******************************************************************************/
XHdmiC_Aux XV_HdmiC_SPDIF_GeneratePacket(XHdmiC_SPDInfoFrame *SPDInfoFrame)
{
	u8 Index;
	u8 Crc;
	XHdmiC_Aux aux;

	(void)memset((void *)&aux, 0, sizeof(XHdmiC_Aux));

	/* Header, Packet Type */
	aux.Header.Byte[0] = AUX_SPD_INFOFRAME_TYPE;

	/* 3D Audio */
	aux.Header.Byte[1] = SPDInfoFrame->Version;

	/* Length of SPD InfoFrame */
	aux.Header.Byte[2] = 25;

	/* HB3 */
	aux.Header.Byte[3] = 0; /* CRC */

	/* Vendor Name Characters */
	aux.Data.Byte[0] = 0; /* CRC */
	aux.Data.Byte[1] = SPDInfoFrame->VN1;
	aux.Data.Byte[2] = SPDInfoFrame->VN2;
	aux.Data.Byte[3] = SPDInfoFrame->VN3;
	aux.Data.Byte[4] = SPDInfoFrame->VN4;
	aux.Data.Byte[5] = SPDInfoFrame->VN5;
	aux.Data.Byte[6] = SPDInfoFrame->VN6;
	aux.Data.Byte[7] = 0; /* ECC */
	aux.Data.Byte[8] = SPDInfoFrame->VN7;
	aux.Data.Byte[9] = SPDInfoFrame->VN8;

	/* Product Description Character */
	aux.Data.Byte[10] = SPDInfoFrame->PD1;
	aux.Data.Byte[11] = SPDInfoFrame->PD2;
	aux.Data.Byte[12] = SPDInfoFrame->PD3;
	aux.Data.Byte[13] = SPDInfoFrame->PD4;
	aux.Data.Byte[14] = SPDInfoFrame->PD5;
	aux.Data.Byte[15] = 0; /* ECC */
	aux.Data.Byte[16] = SPDInfoFrame->PD6;
	aux.Data.Byte[17] = SPDInfoFrame->PD7;
	aux.Data.Byte[18] = SPDInfoFrame->PD8;
	aux.Data.Byte[19] = SPDInfoFrame->PD9;
	aux.Data.Byte[20] = SPDInfoFrame->PD10;
	aux.Data.Byte[21] = SPDInfoFrame->PD11;
	aux.Data.Byte[22] = SPDInfoFrame->PD12;
	aux.Data.Byte[23] = 0; /* ECC */
	aux.Data.Byte[24] = SPDInfoFrame->PD13;
	aux.Data.Byte[25] = SPDInfoFrame->PD14;
	aux.Data.Byte[26] = SPDInfoFrame->PD15;
	aux.Data.Byte[27] = SPDInfoFrame->PD16;

	/* Source Information */
	aux.Data.Byte[28] = SPDInfoFrame->SourceInfo;
	aux.Data.Byte[29] = 0;
	aux.Data.Byte[30] = 0;
	aux.Data.Byte[31] = 0; /* ECC */

	/* Calculate SPD infoframe checksum */
	Crc = 0;

	/* Header */
	for (Index = 0; Index < 3; Index++)
		Crc += aux.Header.Byte[Index];

	/* Data */
	for (Index = 1; Index < aux.Header.Byte[2] + 4; Index++)
		Crc += aux.Data.Byte[Index];

	Crc = 256 - Crc;

	aux.Data.Byte[0] = Crc;

	return aux;
}

/*****************************************************************************/
/**
 *
 * This function generates and sends DRM Infoframes
 *
 * @param  InstancePtr is a pointer to the HDMI TX Subsystem instance.
 *
 * @return None.
 *
 * @note   None.
 *
******************************************************************************/
void XV_HdmiC_DRMIF_GeneratePacket(struct v4l2_hdr10_payload *DRMInfoFrame, XHdmiC_Aux *aux)
{
	u8 Index;
	u8 Crc;

	memset(aux, 0, sizeof(XHdmiC_Aux));

	/* Header, Packet Type */
	aux->Header.Byte[0] = AUX_DRM_INFOFRAME_TYPE;

	/* Version Refer CEA-861-G */
	aux->Header.Byte[1] = 0x1;

	/* Length of DRM InfoFrame */
	aux->Header.Byte[2] = 26;

	/* HB3 */
	aux->Header.Byte[3] = 0; /* CRC */

	/* Vendor Name Characters */
	aux->Data.Byte[0] = 0; /* CRC */
	aux->Data.Byte[1] = DRMInfoFrame->eotf & 0x7;

	aux->Data.Byte[2] = DRMInfoFrame->metadata_type & 0x7;

	aux->Data.Byte[3] = DRMInfoFrame->display_primaries[0].x & 0xFF;
	aux->Data.Byte[4] = DRMInfoFrame->display_primaries[0].x >> 8;

	aux->Data.Byte[5] = DRMInfoFrame->display_primaries[0].y & 0xFF;
	aux->Data.Byte[6] = DRMInfoFrame->display_primaries[0].y >> 8;
	aux->Data.Byte[7] = 0; /* ECC */

	aux->Data.Byte[8] = DRMInfoFrame->display_primaries[1].x & 0xFF;
	aux->Data.Byte[9] = DRMInfoFrame->display_primaries[1].x >> 8;

	aux->Data.Byte[10] = DRMInfoFrame->display_primaries[1].y & 0xFF;
	aux->Data.Byte[11] = DRMInfoFrame->display_primaries[1].y >> 8;

	aux->Data.Byte[12] = DRMInfoFrame->display_primaries[2].x & 0xFF;
	aux->Data.Byte[13] = DRMInfoFrame->display_primaries[2].x >> 8;

	aux->Data.Byte[14] = DRMInfoFrame->display_primaries[2].y & 0xFF;
	aux->Data.Byte[15] = 0; /* ECC */
	aux->Data.Byte[16] = DRMInfoFrame->display_primaries[2].y >> 8;

	aux->Data.Byte[17] = DRMInfoFrame->white_point.x & 0xFF;
	aux->Data.Byte[18] = DRMInfoFrame->white_point.x >> 8;

	aux->Data.Byte[19] = DRMInfoFrame->white_point.y & 0xFF;
	aux->Data.Byte[20] = DRMInfoFrame->white_point.y >> 8;

	aux->Data.Byte[21] = DRMInfoFrame->max_mdl & 0xFF;
	aux->Data.Byte[22] = DRMInfoFrame->max_mdl >> 8;
	aux->Data.Byte[23] = 0; /* ECC */

	aux->Data.Byte[24] = DRMInfoFrame->min_mdl & 0xFF;
	aux->Data.Byte[25] = DRMInfoFrame->min_mdl >> 8;

	aux->Data.Byte[26] = DRMInfoFrame->max_cll & 0xFF;
	aux->Data.Byte[27] = DRMInfoFrame->max_cll >> 8;

	aux->Data.Byte[28] = DRMInfoFrame->max_fall & 0xFF;
	aux->Data.Byte[29] = DRMInfoFrame->max_fall >> 8;

	aux->Data.Byte[30] = 0;
	aux->Data.Byte[31] = 0; /* ECC */

	/* Calculate DRM infoframe checksum */
	Crc = 0;

	/* Header */
	for (Index = 0; Index < 3; Index++)
		Crc += aux->Header.Byte[Index];

	/* Data */
	for (Index = 1; Index < aux->Header.Byte[2] + 4; Index++)
		Crc += aux->Data.Byte[Index];

	Crc = 256 - Crc;

	aux->Data.Byte[0] = Crc;
}

/*****************************************************************************/
/**
*
* This function converts the XVidC_ColorFormat to XHdmiC_Colorspace
*
* @param  ColorFormat is the XVidC_ColorFormat value to be converted
*
* @return XHdmiC_Colorspace value.
*
* @note   None.
*
******************************************************************************/
XHdmiC_Colorspace XV_HdmiC_XVidC_To_IfColorformat(XVidC_ColorFormat ColorFormat) {
	XHdmiC_Colorspace Colorspace;

	switch(ColorFormat) {
		case XVIDC_CSF_RGB :
			Colorspace = XHDMIC_COLORSPACE_RGB;
			break;

		case XVIDC_CSF_YCRCB_422 :
			Colorspace = XHDMIC_COLORSPACE_YUV422;
			break;

		case XVIDC_CSF_YCRCB_444 :
			Colorspace = XHDMIC_COLORSPACE_YUV444;
			break;

		case XVIDC_CSF_YCRCB_420 :
			Colorspace = XHDMIC_COLORSPACE_YUV420;
			break;

		default:
			Colorspace = XHDMIC_COLORSPACE_RESERVED;
			break;
	}

	return Colorspace;
}

/*****************************************************************************/
/**
*
* This function converts the XVidC_ColorDepth to XHdmiC_ColorDepth
*
* @param  Bpc is the XVidC_ColorDepth value to be converted
*
* @return XHdmiC_ColorDepth value.
*
* @note   None.
*
******************************************************************************/
XVidC_AspectRatio XV_HdmiC_IFAspectRatio_To_XVidC(XHdmiC_PicAspectRatio AR) {
	XVidC_AspectRatio AspectRatio;

	switch(AR) {
		case XHDMIC_PIC_ASPECT_RATIO_4_3 :
			AspectRatio = XVIDC_AR_4_3;
			break;

		case XHDMIC_PIC_ASPECT_RATIO_16_9 :
			AspectRatio = XVIDC_AR_16_9;
			break;

		default:
			AspectRatio = XVIDC_AR_16_9;
			break;
	}

	return AspectRatio;
}
