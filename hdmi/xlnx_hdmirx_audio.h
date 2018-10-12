// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ALSA SoC HDMI audio capture support
 *
 * Copyright (C) 2017-2018 Xilinx, Inc.
 *
 */

#ifndef __XILINX_HDMI_RX_AUD_H__
#define __XILINX_HDMI_RX_AUD_H__

struct xlnx_hdmirx_audio_data *hdmirx_get_audio_data(struct device *dev);
int hdmirx_register_aud_dev(struct device *dev);
void hdmirx_unregister_aud_dev(struct device *dev);
void __iomem *hdmirx_parse_aud_dt(struct device *dev);
u32 hdmirx_audio_startup(struct device *dev);
void hdmirx_audio_shutdown(struct device *dev);

struct xlnx_hdmirx_audio_data {
	void __iomem *aes_base;
};
#endif /* __XILINX_HDMI_RX_AUD_H__ */
