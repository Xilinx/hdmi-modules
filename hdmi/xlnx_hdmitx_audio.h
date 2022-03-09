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

#ifndef __XILINX_HDMI_TX_AUD_H__
#define __XILINX_HDMI_TX_AUD_H__
struct xlnx_hdmitx_audio_data {
	u8 buffer[HDMI_INFOFRAME_SIZE(AUDIO)];
	unsigned int tmds_clk;
	void __iomem *acr_base;
	bool tmds_clk_ratio;
};

struct xlnx_hdmitx_audio_data *hdmitx_get_audio_data(struct device *dev);
void __iomem *hdmitx_parse_aud_dt(struct device *dev);
int hdmitx_register_aud_dev(struct device *dev, int instance);
void hdmitx_unregister_aud_dev(struct device *dev);
void hdmitx_audio_startup(struct device *dev);
void hdmitx_audio_hw_params(struct device *dev,
		struct hdmi_audio_infoframe *frame);
void hdmitx_audio_shutdown(struct device *dev);
void hdmitx_audio_mute(struct device *dev, bool enable);
int hdmitx_audio_geteld(struct device *dev, uint8_t *buf, size_t len);

#endif /* __XILINX_HDMI_TX_AUD_H__ */
