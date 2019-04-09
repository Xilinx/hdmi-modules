/*
 * Xilinx ALSA SoC HDMI audio playback support
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
#include <sound/hdmi-codec.h>

#include "xlnx_hdmitx_audio.h"

#define XV_ACR_ENABLE 0x4
#define XV_ACR_N 0xc
#define ACR_CTRL_TMDSCLKRATIO BIT(3)

struct acr_n_table {
	u32 tmds_rate;
	u32 acr_nval[7];
};

/* N values for Audio Clock Regeneration */
const struct acr_n_table acr_n_table[] = {
	/* TMDSClk    32k   44k1   48k   88k2    96k  176k4   192k */
	{        0, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{ 25200000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{ 27000000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{ 31500000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{ 33750000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{ 37800000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{ 40500000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{ 50400000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{ 54000000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{ 67500000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{ 74250000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{ 81000000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{ 92812500, { 8192, 6272, 12288, 12544, 24576, 25088, 49152} },
	{108000000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{111375000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{148500000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{185625000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{222750000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{297000000, { 3072, 4704,  5120,  9408, 10240, 18816, 20480} },
	{371250000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{445500000, { 4096, 6272,  6144, 12544, 12288, 25088, 24576} },
	{594000000, { 3072, 9408,  6144, 18816, 12288, 37632, 24576} }
};

static u16 srate_to_index(u32 srate)
{
	u16 index;

	switch (srate) {
	case 32000:
		index = 0;
	break;
	case 44100:
		index = 1;
	break;
	case 48000:
		index = 2;
	break;
	case 88200:
		index = 3;
	break;
	case 96000:
		index = 4;
	break;
	case 176400:
		index = 5;
	break;
	case 192000:
		index = 6;
	break;
	default:
		index = 0;
	break;
	}

	return index;
}

/* xhdmi_acr_get_n - calculate N value from lookup table
 * @tmds_rate: TMDS clock
 * @srate: sampling rate
 *
 * This function retrieves N value from the lookup table using
 * TMDS clock and sampling rate
 *
 * Return: N value
 */
static const unsigned int xhdmi_acr_get_n(unsigned int tmds_rate, int srate)
{
	struct acr_n_table const *item;
	u16 i, idx;

	for (i = 0; i < sizeof(acr_n_table)/sizeof(struct acr_n_table); i++) {
		item = &acr_n_table[i];
		if (item->tmds_rate == tmds_rate) {
			idx = srate_to_index(srate);
			return item->acr_nval[idx];
		}
	}

	/* if not found return default */
	item = &acr_n_table[0];
	idx = srate_to_index(srate);
	return item->acr_nval[idx];
}

/* hdmitx_parse_aud_dt - parse ACR node from DT
 * @dev: device
 *
 * Parse the DT entry related to ACR IP.
 */
void __iomem *hdmitx_parse_aud_dt(struct device *dev)
{
	u32 val;
	int rc;
	struct device_node *node, *acr_node;
	void __iomem *acr_base;
	struct resource acr_res;

	node = dev->of_node;
	acr_node = of_parse_phandle(node, "xlnx,xlnx-hdmi-acr-ctrl", 0);
	if (!acr_node) {
		dev_err(dev, "failed to get acr_node!\n");
		acr_base = NULL;
	} else {
		rc = of_address_to_resource(acr_node, 0, &acr_res);
		if (rc) {
			dev_err(dev, "acr resource failed: %d\n", rc);
			acr_base = NULL;
		} else {
			acr_base = devm_ioremap_resource(dev, &acr_res);
			if (IS_ERR(acr_base)) {
				dev_err(dev, "acr ioremap failed\n");
				acr_base = NULL;
			}
		}
		of_node_put(acr_node);
	}
	return acr_base;
}

/* audio_codec_startup - initialze audio during audio usecase
 *
 * This function is called by ALSA framework before audio
 * playback begins. This callback initializes audio
 *
 * Return: 0 on success
 */
static int audio_codec_startup(struct device *dev, void *data)
{
	hdmitx_audio_startup(dev);

	return 0;
}

/* audio_codec_hw_params - sets the playback stream properties
 * @dev: device
 * @data: optional data set during registration
 * @fmt: Protocol between ASoC cpu-dai and HDMI-encoder
 * @hparams: stream parameters
 *
 * This function is called by ALSA framework after startup callback
 * packs the audio infoframe from stream paremters and programs ACR
 * block
 *
 * Return: 0 on success
 */
static int audio_codec_hw_params(struct device *dev, void *data,
			 struct hdmi_codec_daifmt *fmt,
			 struct hdmi_codec_params *hparams)
{
	u32 n, i, val;
	struct hdmi_audio_infoframe *frame = &hparams->cea;
	struct xlnx_hdmitx_audio_data *adata = hdmitx_get_audio_data(dev);

	if (!adata)
		return -EINVAL;

	hdmi_audio_infoframe_pack(&hparams->cea, adata->buffer,
				  HDMI_INFOFRAME_SIZE(AUDIO));

	n = xhdmi_acr_get_n(adata->tmds_clk, hparams->sample_rate);

	/* Disable ACR */
	writel(2, adata->acr_base + XV_ACR_ENABLE);
	/* program 'N' */
	writel(n, adata->acr_base + XV_ACR_N);

	val = 3;
	if (adata->tmds_clk_ratio)
		val |= ACR_CTRL_TMDSCLKRATIO;

	/* Enable ACR */
	writel(val, adata->acr_base + XV_ACR_ENABLE);

	return 0;
}

/* audio_codec_shutdown - Deinitialze audio when audio usecase is stopped
 *
 * This function is called by ALSA framework before audio playback usecase
 * ends.
 */
static void audio_codec_shutdown(struct device *dev, void *data)
{
	hdmitx_audio_shutdown(dev);
}

/* audio_codec_digital_mute - mute or unmute audio
 *
 * This function is called by ALSA framework before audio usecase
 * starts and before audio usecase ends
 */

static int audio_codec_digital_mute(struct device *dev, void *data, bool enable)
{
	hdmitx_audio_mute(dev, enable);

	return 0;
}

static const struct hdmi_codec_ops audio_ops = {
	.audio_startup = audio_codec_startup,
	.hw_params = audio_codec_hw_params,
	.audio_shutdown = audio_codec_shutdown,
	.digital_mute = audio_codec_digital_mute,
};

/* hdmitx_register_aud_dev - register audio device
 * @dev: device
 *
 * This functions registers a new platform device and a corresponding
 * module is loaded which registers a audio codec device and
 * calls the registered callbacks
 *
 * Return: platform device
 */
struct platform_device *hdmitx_register_aud_dev(struct device *dev)
{
	struct platform_device *audio_pdev;
	struct hdmi_codec_pdata codec_pdata = {
		.ops = &audio_ops,
		.i2s = 1,
		.max_i2s_channels = 2,
	};

	audio_pdev = platform_device_register_data(dev, HDMI_CODEC_DRV_NAME,
		0, &codec_pdata, sizeof(codec_pdata));

	return audio_pdev;
}
