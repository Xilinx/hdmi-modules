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

#ifndef __XILINX_HDMI_RX_AUD_H__
#define __XILINX_HDMI_RX_AUD_H__

#define XHDMI_AUDIO_DETECT_TIMEOUT 50

struct xlnx_hdmirx_audio_data *hdmirx_get_audio_data(struct device *dev);
int hdmirx_register_aud_dev(struct device *dev);
void hdmirx_unregister_aud_dev(struct device *dev);
u32 hdmirx_audio_startup(struct device *dev);
void hdmirx_audio_shutdown(struct device *dev);

struct xlnx_hdmirx_audio_data {
	void __iomem *aes_base;
	bool audio_detected;
	wait_queue_head_t audio_update_q;
	int format;
	u8 num_channels;
};
#endif /* __XILINX_HDMI_RX_AUD_H__ */
