// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ALSA SoC HDMI audio capture support
 *
 * Copyright (C) 2017-2018 Xilinx, Inc.
 *
 */

#include <linux/of_address.h>
#include <sound/soc.h>
#include <sound/hdmi-codec.h>

#include "xlnx_hdmirx_audio.h"

#define XV_AES_ENABLE 0x8
#define XV_AES_CH_STS_REG1 0x50
#define XV_AES_CH_STS_REG2 0x54

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

/* parse_consumer_format - parse AES stream header
 * @reg1_val: AES register content
 * @reg2_val: AES register content
 * @srate: sampling rate
 * @sig_bits: valid bits in given container format
 *
 * This function parses AES header in consumer format
 */
static void parse_consumer_format(u32 reg1_val, u32 reg2_val,
			u32 *srate, u32 *sig_bits)
{
	u32 max_word_length;

	switch ((reg1_val & 0x0F000000) >> 24) {
	case 0:
		*srate = 44100;
	break;
	case 2:
		*srate = 48000;
	break;
	case 3:
		*srate = 32000;
	break;
	}

	if (reg2_val & 0x1) {
		max_word_length = 24;
		reg2_val = (reg2_val & 0xE) >> 1;
		switch (reg2_val) {
		case 0:
			/* not indicated */
			*sig_bits = 0;
		break;
		case 1:
			*sig_bits = 20;
		break;
		case 6:
			*sig_bits = 21;
		break;
		case 2:
			*sig_bits = 22;
		break;
		case 4:
			*sig_bits = 23;
		break;
		case 5:
			*sig_bits = 24;
		break;
		}
	} else {
		max_word_length = 20;
		reg2_val = (reg2_val & 0xE) >> 1;
		switch (reg2_val) {
		case 0:
			/* not indicated */
			*sig_bits = 0;
		break;
		case 1:
			*sig_bits = 16;
		break;
		case 6:
			*sig_bits = 17;
		break;
		case 2:
			*sig_bits = 18;
		break;
		case 4:
			*sig_bits = 19;
		break;
		case 5:
			*sig_bits = 20;
		break;
		}
	}
}

static void parse_professional_format(u32 reg1_val, u32 reg2_val,
		u32 *srate, u32 *sig_bits)
{
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
	u32 channels, srate, sig_bits, reg1_val, reg2_val, status;

	struct snd_soc_codec *codec = dai->codec;
	struct snd_pcm_runtime *rtd = substream->runtime;
	struct xlnx_hdmirx_audio_data *adata = hdmirx_get_audio_data(dai->dev);

	if (!adata)
		return -EINVAL;

	channels = hdmirx_audio_startup(dai->dev);

	reg1_val = readl(adata->aes_base + XV_AES_CH_STS_REG1);
	reg2_val = readl(adata->aes_base + XV_AES_CH_STS_REG2);
	if (reg1_val & 0x1)
		parse_professional_format(reg1_val, reg2_val, &srate,
						&sig_bits);
	else
		parse_consumer_format(reg1_val, reg2_val, &srate, &sig_bits);

	err = snd_pcm_hw_constraint_minmax(rtd, SNDRV_PCM_HW_PARAM_RATE,
					srate, srate);
	if (err < 0) {
		dev_err(codec->dev, "failed to constrain samplerate to %dHz\n",
			srate);
		return err;
	}

	/* During record, after AES bits(8) are removed, pcm is at max 24bits.
	 * Out of 24 bits, sig_bits represent valid number of audio bits from
	 * input stream
	 */
	err = snd_pcm_hw_constraint_msbits(rtd, 0, 24, sig_bits);

	if (err < 0) {
		dev_err(codec->dev,
		"failed to constrain 'bits per sample' %d bits\n", sig_bits);
		return err;
	}
	err = snd_pcm_hw_constraint_minmax(rtd, SNDRV_PCM_HW_PARAM_CHANNELS,
					channels, channels);
	if (err < 0) {
		dev_err(codec->dev,
		"failed to constrain channel count to %d\n", channels);
		return err;
	}

	dev_info(codec->dev, "set samplerate constraint to %dHz\n", srate);
	dev_info(codec->dev, "set 'bits per sample' constraint to %d\n",
		sig_bits);
	dev_info(codec->dev, "set channels constraint to %d\n", channels);

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

