// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ALSA SoC HDMI audio playback support
 *
 * Copyright (C) 2017-2018 Xilinx, Inc.
 *
 */

#ifndef __XILINX_HDMI_TX_AUD_H__
#define __XILINX_HDMI_TX_AUD_H__

struct xlnx_hdmitx_audio_data {
	u8 buffer[HDMI_INFOFRAME_SIZE(AUDIO)];
	unsigned int tmds_clk;
	void __iomem *acr_base;
};

struct xlnx_hdmitx_audio_data *hdmitx_get_audio_data(struct device *dev);
void __iomem *hdmitx_parse_aud_dt(struct device *dev);
struct platform_device *hdmitx_register_aud_dev(struct device *dev);

void hdmitx_audio_startup(struct device *dev);
void hdmitx_audio_shutdown(struct device *dev);
void hdmitx_audio_mute(struct device *dev, bool enable);

#endif /* __XILINX_HDMI_TX_AUD_H__ */
