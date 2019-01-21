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


/* xlnx_rx_pcm_startup - initialze audio during audio usecase
 *
 * This function is called by ALSA framework before audio
 * capture begins.
 *
 * Return: 0 on success
 */
static int xlnx_rx_pcm_startup(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	u32 channels = hdmirx_audio_startup(dai->dev);

	if (!channels)
		return -EINVAL;

	dev_info(dai->dev,
		 "Detected audio with channel count = %d, starting capture\n",
		 channels);
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
			 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
			 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
			 SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &xlnx_rx_dai_ops,
};

static int xlnx_rx_codec_probe(struct snd_soc_component *component)
{
	return 0;
}

void xlnx_rx_codec_remove(struct snd_soc_component *component)
{
}

static const struct snd_soc_component_driver xlnx_rx_codec_driver = {
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
	return snd_soc_register_component(dev, &xlnx_rx_codec_driver,
			&xlnx_rx_audio_dai, 1);
}

/* hdmirx_register_aud_dev - register audio device
 *
 * This functions unregisters codec DAI device
 */
void hdmirx_unregister_aud_dev(struct device *dev)
{
	snd_soc_unregister_component(dev);
}

