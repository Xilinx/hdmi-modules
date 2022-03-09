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

#include <drm/drm_crtc.h> /* This is only to get MAX_ELD_BYTES */
#include <linux/hdmi.h>
#include <linux/of_address.h>
#include <sound/soc.h>
#include <sound/pcm_drm_eld.h>

#include "xlnx_hdmitx_audio.h"

#define XV_ACR_ENABLE 0x4
#define XV_ACR_N 0xc
#define ACR_CTRL_TMDSCLKRATIO BIT(3)

/*
 * This list is only for formats allowed on the I2S bus. So there is
 * some formats listed that are not supported by HDMI interface. For
 * instance allowing the 32-bit formats enables 24-precision with CPU
 * DAIs that do not support 24-bit formats. If the extra formats cause
 * problems, we should add the video side driver an option to disable
 * them.
 */
#define I2S_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |\
			 SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE |\
			 SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_3BE |\
			 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE |\
			 SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE |\
			 SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE)
#define HDMI_RATES	(SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
			 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |\
			 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |\
			 SNDRV_PCM_RATE_192000)

/*
 * CEA speaker placement for HDMI 1.4:
 *
 *  FL  FLC   FC   FRC   FR   FRW
 *
 *                                  LFE
 *
 *  RL  RLC   RC   RRC   RR
 *
 *  Speaker placement has to be extended to support HDMI 2.0
 */
enum hdmi_codec_cea_spk_placement {
	FL  = BIT(0),	/* Front Left           */
	FC  = BIT(1),	/* Front Center         */
	FR  = BIT(2),	/* Front Right          */
	FLC = BIT(3),	/* Front Left Center    */
	FRC = BIT(4),	/* Front Right Center   */
	RL  = BIT(5),	/* Rear Left            */
	RC  = BIT(6),	/* Rear Center          */
	RR  = BIT(7),	/* Rear Right           */
	RLC = BIT(8),	/* Rear Left Center     */
	RRC = BIT(9),	/* Rear Right Center    */
	LFE = BIT(10),	/* Low Frequency Effect */
};

/*
 * cea Speaker allocation structure
 */
struct hdmi_codec_cea_spk_alloc {
	const int ca_id;
	unsigned int n_ch;
	unsigned long mask;
};

struct acr_n_table {
	u32 tmds_rate;
	u32 acr_nval[7];
};

/*
 * hdmi_codec_channel_alloc: speaker configuration available for CEA
 *
 * This is an ordered list that must match with hdmi_codec_8ch_chmaps struct
 * The preceding ones have better chances to be selected by
 * hdmi_codec_get_ch_alloc_table_idx().
 */
static const struct hdmi_codec_cea_spk_alloc hdmi_codec_channel_alloc[] = {
	{ .ca_id = 0x00, .n_ch = 2,
	  .mask = FL | FR},
	/* 2.1 */
	{ .ca_id = 0x01, .n_ch = 4,
	  .mask = FL | FR | LFE},
	/* Dolby Surround */
	{ .ca_id = 0x02, .n_ch = 4,
	  .mask = FL | FR | FC },
	/* surround51 */
	{ .ca_id = 0x0b, .n_ch = 6,
	  .mask = FL | FR | LFE | FC | RL | RR},
	/* surround40 */
	{ .ca_id = 0x08, .n_ch = 6,
	  .mask = FL | FR | RL | RR },
	/* surround41 */
	{ .ca_id = 0x09, .n_ch = 6,
	  .mask = FL | FR | LFE | RL | RR },
	/* surround50 */
	{ .ca_id = 0x0a, .n_ch = 6,
	  .mask = FL | FR | FC | RL | RR },
	/* 6.1 */
	{ .ca_id = 0x0f, .n_ch = 8,
	  .mask = FL | FR | LFE | FC | RL | RR | RC },
	/* surround71 */
	{ .ca_id = 0x13, .n_ch = 8,
	  .mask = FL | FR | LFE | FC | RL | RR | RLC | RRC },
	/* others */
	{ .ca_id = 0x03, .n_ch = 8,
	  .mask = FL | FR | LFE | FC },
	{ .ca_id = 0x04, .n_ch = 8,
	  .mask = FL | FR | RC},
	{ .ca_id = 0x05, .n_ch = 8,
	  .mask = FL | FR | LFE | RC },
	{ .ca_id = 0x06, .n_ch = 8,
	  .mask = FL | FR | FC | RC },
	{ .ca_id = 0x07, .n_ch = 8,
	  .mask = FL | FR | LFE | FC | RC },
	{ .ca_id = 0x0c, .n_ch = 8,
	  .mask = FL | FR | RC | RL | RR },
	{ .ca_id = 0x0d, .n_ch = 8,
	  .mask = FL | FR | LFE | RL | RR | RC },
	{ .ca_id = 0x0e, .n_ch = 8,
	  .mask = FL | FR | FC | RL | RR | RC },
	{ .ca_id = 0x10, .n_ch = 8,
	  .mask = FL | FR | RL | RR | RLC | RRC },
	{ .ca_id = 0x11, .n_ch = 8,
	  .mask = FL | FR | LFE | RL | RR | RLC | RRC },
	{ .ca_id = 0x12, .n_ch = 8,
	  .mask = FL | FR | FC | RL | RR | RLC | RRC },
	{ .ca_id = 0x14, .n_ch = 8,
	  .mask = FL | FR | FLC | FRC },
	{ .ca_id = 0x15, .n_ch = 8,
	  .mask = FL | FR | LFE | FLC | FRC },
	{ .ca_id = 0x16, .n_ch = 8,
	  .mask = FL | FR | FC | FLC | FRC },
	{ .ca_id = 0x17, .n_ch = 8,
	  .mask = FL | FR | LFE | FC | FLC | FRC },
	{ .ca_id = 0x18, .n_ch = 8,
	  .mask = FL | FR | RC | FLC | FRC },
	{ .ca_id = 0x19, .n_ch = 8,
	  .mask = FL | FR | LFE | RC | FLC | FRC },
	{ .ca_id = 0x1a, .n_ch = 8,
	  .mask = FL | FR | RC | FC | FLC | FRC },
	{ .ca_id = 0x1b, .n_ch = 8,
	  .mask = FL | FR | LFE | RC | FC | FLC | FRC },
	{ .ca_id = 0x1c, .n_ch = 8,
	  .mask = FL | FR | RL | RR | FLC | FRC },
	{ .ca_id = 0x1d, .n_ch = 8,
	  .mask = FL | FR | LFE | RL | RR | FLC | FRC },
	{ .ca_id = 0x1e, .n_ch = 8,
	  .mask = FL | FR | FC | RL | RR | FLC | FRC },
	{ .ca_id = 0x1f, .n_ch = 8,
	  .mask = FL | FR | LFE | FC | RL | RR | FLC | FRC },
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

static unsigned long hdmi_codec_spk_mask_from_alloc(int spk_alloc)
{
	int i;
	static const unsigned long hdmi_codec_eld_spk_alloc_bits[] = {
		[0] = FL | FR, [1] = LFE, [2] = FC, [3] = RL | RR,
		[4] = RC, [5] = FLC | FRC, [6] = RLC | RRC,
	};
	unsigned long spk_mask = 0;

	for (i = 0; i < ARRAY_SIZE(hdmi_codec_eld_spk_alloc_bits); i++) {
		if (spk_alloc & (1 << i))
			spk_mask |= hdmi_codec_eld_spk_alloc_bits[i];
	}

	return spk_mask;
}

static int hdmi_codec_get_ch_alloc_table_idx(u8 *eld, u8 channels)
{
	int i;
	u8 spk_alloc;
	unsigned long spk_mask;
	const struct hdmi_codec_cea_spk_alloc *cap = hdmi_codec_channel_alloc;

	spk_alloc = drm_eld_get_spk_alloc(eld);
	spk_mask = hdmi_codec_spk_mask_from_alloc(spk_alloc);

	for (i = 0; i < ARRAY_SIZE(hdmi_codec_channel_alloc); i++, cap++) {
		/* If spk_alloc == 0, HDMI is unplugged return stereo config*/
		if (!spk_alloc && cap->ca_id == 0)
			return i;
		if (cap->n_ch != channels)
			continue;
		if (!(cap->mask == (spk_mask & cap->mask)))
			continue;
		return i;
	}

	return -EINVAL;
}

static int hdmi_codec_fill_cea_params(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai,
				      unsigned int channels,
				      struct hdmi_audio_infoframe *cea)
{
	int idx;
	int ret;
	u8 eld[MAX_ELD_BYTES];

	ret = hdmitx_audio_geteld(dai->dev, eld, sizeof(eld));
	if (ret)
		return ret;

	ret = snd_pcm_hw_constraint_eld(substream->runtime, eld);
	if (ret)
		return ret;

	/* Select a channel allocation that matches with ELD and pcm channels */
	idx = hdmi_codec_get_ch_alloc_table_idx(eld, channels);
	if (idx < 0) {
		dev_err(dai->dev, "Not able to map channels to speakers (%d)\n",
			idx);
		return idx;
	}

	hdmi_audio_infoframe_init(cea);
	cea->channels = channels;
	cea->coding_type = HDMI_AUDIO_CODING_TYPE_STREAM;
	cea->sample_size = HDMI_AUDIO_SAMPLE_SIZE_STREAM;
	cea->sample_frequency = HDMI_AUDIO_SAMPLE_FREQUENCY_STREAM;
	cea->channel_allocation = hdmi_codec_channel_alloc[idx].ca_id;

	return 0;
}

/* xlnx_tx_pcm_startup - initialize audio during audio usecase
 *
 * This function is called by ALSA framework before audio
 * playback begins. This callback initializes audio
 *
 * Return: 0 on success
 */
static int xlnx_tx_pcm_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	hdmitx_audio_startup(dai->dev);

	return 0;
}

/* xlnx_tx_pcm_hw_params - sets the playback stream properties
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
static int xlnx_tx_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	u32 n, val;
	int ret;
	struct hdmi_audio_infoframe frame;
	struct xlnx_hdmitx_audio_data *adata = hdmitx_get_audio_data(dai->dev);

	if (!adata)
		return -EINVAL;

	ret = hdmi_codec_fill_cea_params(substream, dai,
					 params_channels(params),
					 &frame);
	if (ret < 0)
		return ret;

	hdmitx_audio_hw_params(dai->dev, &frame);
	hdmi_audio_infoframe_pack(&frame, adata->buffer,
				  HDMI_INFOFRAME_SIZE(AUDIO));

	n = xhdmi_acr_get_n(adata->tmds_clk, params_rate(params));

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

/* xlnx_tx_pcm_shutdown - Deinitialze audio when audio usecase is stopped
 *
 * This function is called by ALSA framework before audio playback usecase
 * ends.
 */
static void xlnx_tx_pcm_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	hdmitx_audio_shutdown(dai->dev);
}

/* xlnx_tx_pcm_digital_mute - mute or unmute audio
 *
 * This function is called by ALSA framework before audio usecase
 * starts and before audio usecase ends
 */
static int xlnx_tx_pcm_digital_mute(struct snd_soc_dai *dai, int enable,
				    int direction)
{
	hdmitx_audio_mute(dai->dev, enable);

	return 0;
}

int xlnx_tx_pcm_get_eld(struct device *dev, void *data,
			uint8_t *buf, size_t len)
{
	return hdmitx_audio_geteld(dev, buf, len);
}

static const struct snd_soc_dai_ops xlnx_hdmi_tx_dai_ops = {
	.startup = xlnx_tx_pcm_startup,
	.hw_params = xlnx_tx_pcm_hw_params,
	.shutdown = xlnx_tx_pcm_shutdown,
	.mute_stream = xlnx_tx_pcm_digital_mute,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver xlnx_hdmi_tx_dai = {
	.name = "xlnx_hdmi_tx",
	.playback = {
		.stream_name = "I2S Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = HDMI_RATES,
		.formats = I2S_FORMATS,
		.sig_bits = 24,
	},
	.ops = &xlnx_hdmi_tx_dai_ops,
};

static int xlnx_tx_codec_probe(struct snd_soc_component *component)
{
	return 0;
}

void xlnx_tx_codec_remove(struct snd_soc_component *component)
{
}

static const struct snd_soc_component_driver xlnx_hdmi_component = {
	.probe = xlnx_tx_codec_probe,
	.remove = xlnx_tx_codec_remove,
};

int hdmitx_register_aud_dev(struct device *dev, int instance)
{
	return devm_snd_soc_register_component(dev, &xlnx_hdmi_component,
			&xlnx_hdmi_tx_dai, 1);
}

void hdmitx_unregister_aud_dev(struct device *dev)
{
	snd_soc_unregister_component(dev);
}
