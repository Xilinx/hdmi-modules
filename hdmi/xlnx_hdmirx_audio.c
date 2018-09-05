/*
 * Xilinx ALSA SoC HDMI audio capture support
 *
 * Copyright (C) 2017-2018 Xilinx, Inc.
 *
 * Author: Maruthi Srinivas Bayyavarapu <maruthis@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/of_address.h>
#include <sound/soc.h>
#include <sound/hdmi-codec.h>

#include "xlnx_hdmirx_audio.h"

#define XV_AES_ENABLE 0x8
#define XV_AES_CH_STS_REG1 0x50
#define XV_AES_CH_STS_REG2 0x54

/* audio params macros */
#define PROF_SAMPLERATE_MASK		GENMASK(7, 6)
#define PROF_SAMPLERATE_SHIFT		6
#define PROF_CHANNEL_COUNT_MASK		GENMASK(11, 8)
#define PROF_CHANNEL_COUNT_SHIFT	8
#define PROF_MAX_BITDEPTH_MASK		GENMASK(18, 16)
#define PROF_MAX_BITDEPTH_SHIFT		16
#define PROF_BITDEPTH_MASK		GENMASK(21, 19)
#define PROF_BITDEPTH_SHIFT		19

#define AES_FORMAT_MASK			BIT(0)
#define PROF_SAMPLERATE_UNDEFINED	0
#define PROF_SAMPLERATE_44100		1
#define PROF_SAMPLERATE_48000		2
#define PROF_SAMPLERATE_32000		3
#define PROF_CHANNELS_UNDEFINED		0
#define PROF_TWO_CHANNELS		8
#define PROF_STEREO_CHANNELS		2
#define PROF_MAX_BITDEPTH_UNDEFINED	0
#define PROF_MAX_BITDEPTH_20		2
#define PROF_MAX_BITDEPTH_24		4

#define CON_SAMPLE_RATE_MASK		GENMASK(27, 24)
#define CON_SAMPLE_RATE_SHIFT		24
#define CON_CHANNEL_COUNT_MASK		GENMASK(23, 20)
#define CON_CHANNEL_COUNT_SHIFT		20
#define CON_MAX_BITDEPTH_MASK		BIT(1)
#define CON_BITDEPTH_MASK		GENMASK(3, 1)
#define CON_BITDEPTH_SHIFT		0x1

#define CON_SAMPLERATE_44100		0
#define CON_SAMPLERATE_48000		2
#define CON_SAMPLERATE_32000		3
/* hdmirx_parse_aud_dt - parse AES node from DT
 * @dev: device
 *
 * Parse the DT entry related to AES IP. This IP AES header
 * from incoming audio stream.
 *
 */
void __iomem *hdmirx_parse_aud_dt(struct device *dev)
{
	struct device_node *aes_node;
	struct resource res;
	void __iomem *aes_base;
	int rc;
	struct device_node *node = dev->of_node;

	/* audio errors are not considered fatal */
	aes_node = of_parse_phandle(node, "xlnx,aes_parser", 0);
	if (!aes_node) {
		dev_err(dev, "aes parser not found\n");
		aes_base = NULL;
	} else {
		rc = of_address_to_resource(aes_node, 0, &res);
		if (rc) {
			dev_err(dev, "aes parser:addr to resource failed\n");
			aes_base = NULL;
		} else {
			aes_base = devm_ioremap_resource(dev, &res);
			if (IS_ERR(aes_base)) {
				dev_err(dev, "aes ioremap failed\n");
				aes_base = NULL;
			} else
				writel(1, aes_base + XV_AES_ENABLE);
		}
		of_node_put(aes_node);
	}
	return aes_base;
}

static struct audio_params *parse_professional_format(u32 reg1_val,
						      u32 reg2_val)
{
	u32 padded, val;
	struct audio_params *params;

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return NULL;

	val = (reg1_val & PROF_SAMPLERATE_MASK) >> PROF_SAMPLERATE_SHIFT;
	switch (val) {
	case PROF_SAMPLERATE_44100:
		params->srate = 44100;
		break;
	case PROF_SAMPLERATE_48000:
		params->srate = 48000;
		break;
	case PROF_SAMPLERATE_32000:
		params->srate = 32000;
		break;
	case PROF_SAMPLERATE_UNDEFINED:
	default:
		/* not indicated */
		kfree(params);
		return NULL;
	}

	val = (reg1_val & PROF_CHANNEL_COUNT_MASK) >> PROF_CHANNEL_COUNT_SHIFT;
	switch (val) {
	case PROF_CHANNELS_UNDEFINED:
	case PROF_STEREO_CHANNELS:
	case PROF_TWO_CHANNELS:
		params->channels = 2;
		break;
	default:
		/* TODO: handle more channels in future*/
		kfree(params);
		return NULL;
	}

	val = (reg1_val & PROF_MAX_BITDEPTH_MASK) >> PROF_MAX_BITDEPTH_SHIFT;
	switch (val) {
	case PROF_MAX_BITDEPTH_UNDEFINED:
	case PROF_MAX_BITDEPTH_20:
		padded = 0;
		break;
	case PROF_MAX_BITDEPTH_24:
		padded = 4;
		break;
	default:
		/* user defined values are not supported */
		kfree(params);
		return NULL;
	}

	val = (reg1_val & PROF_BITDEPTH_MASK) >> PROF_BITDEPTH_SHIFT;
	switch (val) {
	case 1:
		params->sig_bits = 16 + padded;
		break;
	case 2:
		params->sig_bits = 18 + padded;
		break;
	case 4:
		params->sig_bits = 19 + padded;
		break;
	case 5:
		params->sig_bits = 20 + padded;
		break;
	case 6:
		params->sig_bits = 17 + padded;
		break;
	case 0:
	default:
		kfree(params);
		return NULL;
	}

	return params;
}

static struct audio_params *parse_consumer_format(u32 reg1_val, u32 reg2_val)
{
	u32 padded, val;
	struct audio_params *params;

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return NULL;

	val = (reg1_val & CON_SAMPLE_RATE_MASK) >> CON_SAMPLE_RATE_SHIFT;
	switch (val) {
	case CON_SAMPLERATE_44100:
		params->srate = 44100;
		break;
	case CON_SAMPLERATE_48000:
		params->srate = 48000;
		break;
	case CON_SAMPLERATE_32000:
		params->srate = 32000;
		break;
	default:
		kfree(params);
		return NULL;
	}

	val = (reg1_val & CON_CHANNEL_COUNT_MASK) >> CON_CHANNEL_COUNT_SHIFT;
	params->channels = val;

	if (reg2_val & CON_MAX_BITDEPTH_MASK)
		padded = 4;
	else
		padded = 0;

	val = (reg2_val & CON_BITDEPTH_MASK) >> CON_BITDEPTH_SHIFT;
	switch (val) {
	case 1:
		params->sig_bits = 16 + padded;
		break;
	case 2:
		params->sig_bits = 18 + padded;
		break;
	case 4:
		params->sig_bits = 19 + padded;
		break;
	case 5:
		params->sig_bits = 20 + padded;
		break;
	case 6:
		params->sig_bits = 17 + padded;
		break;
	case 0:
	default:
		kfree(params);
		return NULL;
	}

	return params;
}

/* xlnx_rx_pcm_startup - initialze audio during audio usecase
 *
 * This function is called by ALSA framework before audio
 * capture begins. This callback initializes audio and extracts
 * channel status bits and sets them as constraints
 *
 * Return: 0 on success
 */
static int xlnx_rx_pcm_startup(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	int rate, err;
	u32 reg1_val, reg2_val;

	struct snd_soc_codec *codec = dai->codec;
	struct snd_pcm_runtime *rtd = substream->runtime;
	struct xlnx_hdmirx_audio_data *adata = hdmirx_get_audio_data(dai->dev);

	if (!adata)
		return -EINVAL;

	reg1_val = readl(adata->aes_base + XV_AES_CH_STS_REG1);
	reg2_val = readl(adata->aes_base + XV_AES_CH_STS_REG2);
	if (reg1_val & 0x1)
		adata->params = parse_professional_format(reg1_val, reg2_val);
	else
		adata->params = parse_consumer_format(reg1_val, reg2_val);

	if (!adata->params)
		return -EINVAL;

	if (!adata->params->channels)
		adata->params->channels = hdmirx_audio_startup(dai->dev);

	dev_info(codec->dev,
		 "Audio properties: srate %d sig_bits = %d channels = %d\n",
		 adata->params->srate, adata->params->sig_bits,
		 adata->params->channels);

	err = snd_pcm_hw_constraint_minmax(rtd, SNDRV_PCM_HW_PARAM_RATE,
				adata->params->srate, adata->params->srate);
	if (err < 0) {
		dev_err(codec->dev, "failed to constrain samplerate to %dHz\n",
			adata->params->srate);
		return err;
	}

	/* During record, after AES bits(8) are removed, pcm is at max 24bits.
	 * Out of 24 bits, sig_bits represent valid number of audio bits from
	 * input stream
	 */
	err = snd_pcm_hw_constraint_msbits(rtd, 0, 24, adata->params->sig_bits);

	if (err < 0) {
		dev_err(codec->dev,
		"failed to constrain 'bits per sample' %d bits\n",
		adata->params->sig_bits);
		return err;
	}
	err = snd_pcm_hw_constraint_minmax(rtd, SNDRV_PCM_HW_PARAM_CHANNELS,
					   adata->params->channels,
					   adata->params->channels);
	if (err < 0) {
		dev_err(codec->dev,
		"failed to constrain channel count to %d\n",
		adata->params->channels);
		return err;
	}

	return 0;
}

/* xlnx_rx_pcm_shutdown - Deinitialze audio when audio usecase is stopped
 *
 * This function is called by ALSA framework before audio capture usecase
 * ends.
 */
static void xlnx_rx_pcm_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct xlnx_hdmirx_audio_data *adata = hdmirx_get_audio_data(dai->dev);

	kfree(adata->params);
	hdmirx_audio_shutdown(dai->dev);
}

static const struct snd_soc_dai_ops xlnx_rx_dai_ops = {
	.startup = xlnx_rx_pcm_startup,
	.shutdown = xlnx_rx_pcm_shutdown,
};

static struct snd_soc_dai_driver xlnx_rx_audio_dai = {
	.name = "xlnx_hdmi_rx",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
			SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &xlnx_rx_dai_ops,
};

static int xlnx_rx_codec_probe(struct snd_soc_codec *codec)
{
	return 0;
}

static int xlnx_rx_codec_remove(struct snd_soc_codec *codec)
{
	return 0;
}

static struct snd_soc_codec_driver xlnx_rx_codec_driver = {
	.probe = xlnx_rx_codec_probe,
	.remove = xlnx_rx_codec_remove,
};

/* hdmirx_register_aud_dev - register audio device
 *
 * This functions registers codec DAI device as part of
 * ALSA SoC framework.
 */
int hdmirx_register_aud_dev(struct device *dev)
{
	return snd_soc_register_codec(dev, &xlnx_rx_codec_driver,
			&xlnx_rx_audio_dai, 1);
}

/* hdmirx_register_aud_dev - register audio device
 *
 * This functions unregisters codec DAI device
 */
void hdmirx_unregister_aud_dev(struct device *dev)
{
	snd_soc_unregister_codec(dev);
}

