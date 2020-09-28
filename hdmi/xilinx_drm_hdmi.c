/*
 * Xilinx DRM HDMI encoder driver
 *
 * Xilinx HDMI-Tx Subsystem driver
 * Copyright (C) 2018 Rohit Consul <rohitco@xilinx.com>
 *
 * Authors: Rohit Consul <rohitco@xilinx.com>
 *			Leon Woestenberg <leon@sidebranch.com>
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

#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-zynqmp.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>

#include "linux/phy/phy-vphy.h"

/* baseline driver includes */
#include "xilinx-hdmi-tx/xv_hdmitxss.h"

/* for the HMAC, using password to decrypt HDCP keys */
#include "phy-xilinx-vphy/xhdcp22_common.h"
#include "phy-xilinx-vphy/aes256.h"

#include "xlnx_hdmitx_audio.h"

#define HDMI_MAX_LANES				4

#define XVPHY_TXREFCLK_RDY_LOW		0
#define XVPHY_TXREFCLK_RDY_HIGH		1
#define XHDMIPHY1_TXREFCLK_RDY_LOW		0
#define XHDMIPHY1_TXREFCLK_RDY_HIGH		1

#define hdmi_mutex_lock(x) mutex_lock(x)
#define hdmi_mutex_unlock(x) mutex_unlock(x)

/* TX Subsystem Sub-core offsets */
#define TXSS_TX_OFFSET				0x00000u
#define TXSS_VTC_OFFSET				0x10000u
#define TXSS_HDCP14_OFFSET			0x20000u
#define TXSS_HDCP14_TIMER_OFFSET	0x30000u
#define TXSS_HDCP22_OFFSET			0x40000u
/* HDCP22 sub-core offsets */
#define TX_HDCP22_CIPHER_OFFSET		0x00000u
#define TX_HDCP22_TIMER_OFFSET		0x10000u
#define TX_HDCP22_RNG_OFFSET		0x20000u

/**
 * struct xlnx_drm_hdmi - Xilinx HDMI core
 * @encoder: the drm encoder structure
 * @connector: the drm connector structure
 * @dev: device structure
 * @iomem: device I/O memory for register access
 * @hdcp1x_keymngmt_iomem: hdcp key management block I/O memory for register access
 * @clk: video clock
 * @axi_lite_clk: axi_lite clock for register access
 * @tmds_clk: clock configured per resolution
 * @retimer_clk: dp159 (retimer) clock
 * @irq: hdmi subsystem irq
 * @hdcp1x_irq;: hdcp14 block irq
 * @hdcp1x_timer_irq: hdcp1.4 timer irq
 * @hdcp22_irq: hdcp2.2 block irq
 * @hdcp22_timer_irq: hdcp2.2 time irq
 * @hdcp_authenticate: flag to enable/disable hdcp authentication
 * @hdcp_encrypt: flag to enable/disable encryption
 * @hdcp_protect: flag to prevent hdcp in pass-throuch mode
 * @hdcp_authenticated: authentication state flag
 * @hdcp_encrypted: ecryption state flag
 * @hdcp_password_accepted: flag to denote is user pwd was accepted
 * @delayed_work_hdcp_poll: work queue for hdcp polling
 * @hdcp_auth_counter: counter to control hdcp poll time
 * @teardown: flag to indicate driver is being unloaded
 * @phy: PHY handle for hdmi lanes
 * @hdmi_mutex: mutex to lock hdmi structure
 * @irq_lock: to lock irq handler
 * @cable_connected: flag to indicate cable state
 * @hdmi_stream_up: flag to inidcate video stream state
 * @have_edid: flag to indicate if edid is available
 * @is_hdmi_20_sink: flag to indicate if sink is hdmi2.0 capable
 * @dpms: current dpms state
 * @xvidc_colorfmt: hdmi ip internal colorformat representation
 * @config: IP configuration structure
 * @xv_hdmitxss: IP low level driver structure
 * @IntrStatus: Flag to indicate irq status
 * @xvphy: pointer to xilinx video phy
 * @xgtphy: pointer to Xilinx HDMI GT Controller phy
 * @isvphy: Flag to determine which Phy
 * @wait_for_streamup: Flag for TxStreamUpCallback done
 * @wait_event: Wait event queue for TxStreamUpCallback
 * @audio_enabled: flag to indicate audio is enabled in device tree
 * @audio_init: flag to indicate audio is initialized
 * @tx_audio_data: audio data to be shared with audio module
 * @audio_pdev: audio platform device
 */
struct xlnx_drm_hdmi {
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct device *dev;
	void __iomem *iomem;
	void __iomem *hdcp1x_keymngmt_iomem;
	/* video streaming bus clock */
	struct clk *clk;
	struct clk *axi_lite_clk;
	/* tmds clock for output res */
	struct clk *tmds_clk;
	/* retimer that we configure by setting a clock rate */
	struct clk *retimer_clk;

	/* HDMI TXSS interrupt */
	int irq;
	/* HDCP interrupts  */
	int hdcp1x_irq;
	int hdcp1x_timer_irq;
	int hdcp22_irq;
	int hdcp22_timer_irq;
	/* controls */
	bool hdcp_authenticate;
	bool hdcp_encrypt;
	bool hdcp_protect;
	/* status */
	bool hdcp_authenticated;
	bool hdcp_encrypted;
	bool hdcp_password_accepted;
	/* delayed work to drive HDCP poll */
	struct delayed_work delayed_work_hdcp_poll;
	int hdcp_auth_counter;

	bool teardown;

	struct phy *phy[HDMI_MAX_LANES];

	/* mutex to prevent concurrent access to this structure */
	struct mutex hdmi_mutex;
	/* protects concurrent access from interrupt context */
	spinlock_t irq_lock;

	bool cable_connected;
	bool hdmi_stream_up;
	bool have_edid;
	bool is_hdmi_20_sink;
	int dpms;

	XVidC_ColorFormat xvidc_colorfmt;
	XVidC_ColorDepth xvidc_colordepth;
	/* configuration for the baseline subsystem driver instance */
	XV_HdmiTxSs_Config config;
	/* bookkeeping for the baseline subsystem driver instance */
	XV_HdmiTxSs xv_hdmitxss;
	/* sub core interrupt status registers */
	u32 IntrStatus;
	/* pointer to xvphy */
	XVphy *xvphy;
	/* pointer to xgtphy */
	XHdmiphy1 *xgtphy;
	/* flag to determine which Phy */
	u32 isvphy;

	/* flag for waiting till TxStreamUpCallback ends */
	u32 wait_for_streamup:1;
	/* Queue to wait on event of TxStreamUpCallback */
	wait_queue_head_t wait_event;

	/* HDCP keys */
	u8 hdcp_password[32];
	u8 Hdcp22Lc128[16];
	u8 Hdcp22PrivateKey[902];
	u8 Hdcp14KeyA[328];
	u8 Hdcp14KeyB[328];
	bool audio_enabled;
	bool audio_init;
	struct xlnx_hdmitx_audio_data *tx_audio_data;
	struct platform_device *audio_pdev;
};

static const u8 Hdcp22Srm[] = {
  0x91, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x87, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xBE, 0x2D, 0x46,
  0x05, 0x9F, 0x00, 0x78, 0x7B, 0xF2, 0x84, 0x79, 0x7F, 0xC4, 0xF5, 0xF6, 0xC4, 0x06, 0x36, 0xA1,
  0x20, 0x2E, 0x57, 0xEC, 0x8C, 0xA6, 0x5C, 0xF0, 0x3A, 0x14, 0x38, 0xF0, 0xB7, 0xE3, 0x68, 0xF8,
  0xB3, 0x64, 0x22, 0x55, 0x6B, 0x3E, 0xA9, 0xA8, 0x08, 0x24, 0x86, 0x55, 0x3E, 0x20, 0x0A, 0xDB,
  0x0E, 0x5F, 0x4F, 0xD5, 0x0F, 0x33, 0x52, 0x01, 0xF3, 0x62, 0x54, 0x40, 0xF3, 0x43, 0x0C, 0xFA,
  0xCD, 0x98, 0x1B, 0xA8, 0xB3, 0x77, 0xB7, 0xF8, 0xFA, 0xF7, 0x4D, 0x71, 0xFB, 0xB5, 0xBF, 0x98,
  0x9F, 0x1A, 0x1E, 0x2F, 0xF2, 0xBA, 0x80, 0xAD, 0x20, 0xB5, 0x08, 0xBA, 0xF6, 0xB5, 0x08, 0x08,
  0xCF, 0xBA, 0x49, 0x8D, 0xA5, 0x73, 0xD5, 0xDE, 0x2B, 0xEA, 0x07, 0x58, 0xA8, 0x08, 0x05, 0x66,
  0xB8, 0xD5, 0x2B, 0x9C, 0x0B, 0x32, 0xF6, 0x5A, 0x61, 0xE4, 0x9B, 0xC2, 0xF6, 0xD1, 0xF6, 0x2D,
  0x0C, 0x19, 0x06, 0x0E, 0x3E, 0xCE, 0x62, 0x97, 0x80, 0xFC, 0x50, 0x56, 0x15, 0xCB, 0xE1, 0xC7,
  0x23, 0x4B, 0x52, 0x34, 0xC0, 0x9F, 0x85, 0xEA, 0xA9, 0x15, 0x8C, 0xDD, 0x7C, 0x78, 0xD6, 0xAD,
  0x1B, 0xB8, 0x28, 0x1F, 0x50, 0xD4, 0xD5, 0x42, 0x29, 0xEC, 0xDC, 0xB9, 0xA1, 0xF4, 0x26, 0xFA,
  0x43, 0xCC, 0xCC, 0xE7, 0xEA, 0xA5, 0xD1, 0x76, 0x4C, 0xDD, 0x92, 0x9B, 0x1B, 0x1E, 0x07, 0x89,
  0x33, 0xFE, 0xD2, 0x35, 0x2E, 0x21, 0xDB, 0xF0, 0x31, 0x8A, 0x52, 0xC7, 0x1B, 0x81, 0x2E, 0x43,
  0xF6, 0x59, 0xE4, 0xAD, 0x9C, 0xDB, 0x1E, 0x80, 0x4C, 0x8D, 0x3D, 0x9C, 0xC8, 0x2D, 0x96, 0x23,
  0x2E, 0x7C, 0x14, 0x13, 0xEF, 0x4D, 0x57, 0xA2, 0x64, 0xDB, 0x33, 0xF8, 0xA9, 0x10, 0x56, 0xF4,
  0x59, 0x87, 0x43, 0xCA, 0xFC, 0x54, 0xEA, 0x2B, 0x46, 0x7F, 0x8A, 0x32, 0x86, 0x25, 0x9B, 0x2D,
  0x54, 0xC0, 0xF2, 0xEF, 0x8F, 0xE7, 0xCC, 0xFD, 0x5A, 0xB3, 0x3C, 0x4C, 0xBC, 0x51, 0x89, 0x4F,
  0x41, 0x20, 0x7E, 0xF3, 0x2A, 0x90, 0x49, 0x5A, 0xED, 0x3C, 0x8B, 0x3D, 0x9E, 0xF7, 0xC1, 0xA8,
  0x21, 0x99, 0xCF, 0x20, 0xCC, 0x17, 0xFC, 0xC7, 0xB6, 0x5F, 0xCE, 0xB3, 0x75, 0xB5, 0x27, 0x76,
  0xCA, 0x90, 0x99, 0x2F, 0x80, 0x98, 0x9B, 0x19, 0x21, 0x6D, 0x53, 0x7E, 0x1E, 0xB9, 0xE6, 0xF3,
  0xFD, 0xCB, 0x69, 0x0B, 0x10, 0xD6, 0x2A, 0xB0, 0x10, 0x5B, 0x43, 0x47, 0x11, 0xA4, 0x60, 0x28,
  0x77, 0x1D, 0xB4, 0xB2, 0xC8, 0x22, 0xDB, 0x74, 0x3E, 0x64, 0x9D, 0xA8, 0xD9, 0xAA, 0xEA, 0xFC,
  0xA8, 0xA5, 0xA7, 0xD0, 0x06, 0x88, 0xBB, 0xD7, 0x35, 0x4D, 0xDA, 0xC0, 0xB2, 0x11, 0x2B, 0xFA,
  0xED, 0xBF, 0x2A, 0x34, 0xED, 0xA4, 0x30, 0x7E, 0xFD, 0xC5, 0x21, 0xB6
};

static inline struct xlnx_drm_hdmi *encoder_to_hdmi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct xlnx_drm_hdmi, encoder);
}

static inline struct xlnx_drm_hdmi *connector_to_hdmi(struct drm_connector *connector)
{
	return container_of(connector, struct xlnx_drm_hdmi, connector);
}


void HdmiTx_PioIntrHandler(XV_HdmiTx *InstancePtr);

static void XV_HdmiTxSs_IntrEnable(XV_HdmiTxSs *HdmiTxSsPtr)
{
	XV_HdmiTx_PioIntrEnable(HdmiTxSsPtr->HdmiTxPtr);
}

static void XV_HdmiTxSs_IntrDisable(XV_HdmiTxSs *HdmiTxSsPtr)
{
	XV_HdmiTx_PioIntrDisable(HdmiTxSsPtr->HdmiTxPtr);
}

static int __maybe_unused hdmitx_pm_suspend(struct device *dev)
{
	unsigned long flags;
	struct xlnx_drm_hdmi *xhdmi = dev_get_drvdata(dev);
	XV_HdmiTxSs *HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	dev_dbg(xhdmi->dev,"HDMI TX suspend function called\n");
	spin_lock_irqsave(&xhdmi->irq_lock, flags);
	XV_HdmiTxSs_IntrDisable(HdmiTxSsPtr);
	spin_unlock_irqrestore(&xhdmi->irq_lock, flags);
	return 0;
}

static int __maybe_unused hdmitx_pm_resume(struct device *dev)
{
	unsigned long flags;
	struct xlnx_drm_hdmi *xhdmi = dev_get_drvdata(dev);
	XV_HdmiTxSs *HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	dev_dbg(xhdmi->dev,"HDMI TX resume function called\n");
	spin_lock_irqsave(&xhdmi->irq_lock, flags);
	XV_HdmiTxSs_IntrEnable(HdmiTxSsPtr);
	spin_unlock_irqrestore(&xhdmi->irq_lock, flags);
	return 0;
}

/* XV_HdmiTx_IntrHandler */
static irqreturn_t hdmitx_irq_handler(int irq, void *dev_id)
{
	struct xlnx_drm_hdmi *xhdmi;

	XV_HdmiTxSs *HdmiTxSsPtr;
	unsigned long flags;

	xhdmi = (struct xlnx_drm_hdmi *)dev_id;
	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;

	if (HdmiTxSsPtr->IsReady != XIL_COMPONENT_IS_READY) {
		dev_dbg(xhdmi->dev, "hdmitx_irq_handler(): HDMI TX SS is not initialized?!\n");
	}

	/* read status registers */
	xhdmi->IntrStatus = XV_HdmiTx_ReadReg(HdmiTxSsPtr->HdmiTxPtr->Config.BaseAddress, (
							XV_HDMITX_PIO_STA_OFFSET)) & (XV_HDMITX_PIO_STA_IRQ_MASK);

	spin_lock_irqsave(&xhdmi->irq_lock, flags);
	/* mask interrupt request */
	XV_HdmiTxSs_IntrDisable(HdmiTxSsPtr);
	spin_unlock_irqrestore(&xhdmi->irq_lock, flags);

	/* call bottom-half */
	return IRQ_WAKE_THREAD;
}

/* (struct xlnx_drm_hdmi *)dev_id */
static irqreturn_t hdmitx_irq_thread(int irq, void *dev_id)
{
	struct xlnx_drm_hdmi *xhdmi;
	XV_HdmiTxSs *HdmiTxSsPtr;
	unsigned long flags;

	xhdmi = (struct xlnx_drm_hdmi *)dev_id;
	if (!xhdmi) {
		dev_dbg(xhdmi->dev, "irq_thread: !dev_id\n");
		return IRQ_HANDLED;
	}
	/* driver is being torn down, do not process further interrupts */
	if (xhdmi->teardown) {
		dev_dbg(xhdmi->dev, "irq_thread: teardown\n");
		return IRQ_HANDLED;
	}
	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;

	hdmi_mutex_lock(&xhdmi->hdmi_mutex);

	/* call baremetal interrupt handler, this in turn will
	 * call the registed callbacks functions */
	if (xhdmi->IntrStatus) HdmiTx_PioIntrHandler(HdmiTxSsPtr->HdmiTxPtr);

	hdmi_mutex_unlock(&xhdmi->hdmi_mutex);

	spin_lock_irqsave(&xhdmi->irq_lock, flags);
	/* unmask interrupt request */
	XV_HdmiTxSs_IntrEnable(HdmiTxSsPtr);
	spin_unlock_irqrestore(&xhdmi->irq_lock, flags);

	return IRQ_HANDLED;
}

/* top-half interrupt handler for HDMI TX HDCP */
static irqreturn_t hdmitx_hdcp_irq_handler(int irq, void *dev_id)
{
	struct xlnx_drm_hdmi *xhdmi;

	XV_HdmiTxSs *HdmiTxSsPtr;
	unsigned long flags;

	xhdmi = (struct xlnx_drm_hdmi *)dev_id;
	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;

	spin_lock_irqsave(&xhdmi->irq_lock, flags);
	/* mask/disable interrupt requests */
	if (irq == xhdmi->hdcp1x_irq) {
	  XHdcp1x_WriteReg(HdmiTxSsPtr->Hdcp14Ptr->Config.BaseAddress,
		  XHDCP1X_CIPHER_REG_INTERRUPT_MASK, (u32)0xFFFFFFFFu);
	} else if (irq == xhdmi->hdcp1x_timer_irq) {
	  XTmrCtr_DisableIntr(HdmiTxSsPtr->HdcpTimerPtr->BaseAddress, 0);
	} else if (irq == xhdmi->hdcp22_timer_irq) {
	  XTmrCtr_DisableIntr(HdmiTxSsPtr->Hdcp22Ptr->Timer.TmrCtr.BaseAddress, 0);
	  XTmrCtr_DisableIntr(HdmiTxSsPtr->Hdcp22Ptr->Timer.TmrCtr.BaseAddress, 1);
	}
	spin_unlock_irqrestore(&xhdmi->irq_lock, flags);

	/* call bottom-half */
	return IRQ_WAKE_THREAD;
}

/* HDCP service routine, runs outside of interrupt context and can sleep and takes mutexes */
static irqreturn_t hdmitx_hdcp_irq_thread(int irq, void *dev_id)
{
	struct xlnx_drm_hdmi *xhdmi;
	XV_HdmiTxSs *HdmiTxSsPtr;
	unsigned long flags;

	xhdmi = (struct xlnx_drm_hdmi *)dev_id;
	if (!xhdmi) {
		dev_dbg(xhdmi->dev, "irq_thread: !dev_id\n");
		return IRQ_HANDLED;
	}
	/* driver is being torn down, do not process further interrupts */
	if (xhdmi->teardown) {
		dev_dbg(xhdmi->dev, "irq_thread: teardown\n");
		return IRQ_HANDLED;
	}
	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;

	/* invoke the bare-metal interrupt handler under mutex lock */
	hdmi_mutex_lock(&xhdmi->hdmi_mutex);
	if (irq == xhdmi->hdcp1x_irq) {
		XV_HdmiTxSS_HdcpIntrHandler(HdmiTxSsPtr);
	} else if (irq == xhdmi->hdcp1x_timer_irq) {
		XV_HdmiTxSS_HdcpTimerIntrHandler(HdmiTxSsPtr);
	} else if (irq == xhdmi->hdcp22_timer_irq) {
		XV_HdmiTxSS_Hdcp22TimerIntrHandler(HdmiTxSsPtr);
	}
	hdmi_mutex_unlock(&xhdmi->hdmi_mutex);

	/* re-enable interrupt requests */
	spin_lock_irqsave(&xhdmi->irq_lock, flags);
	if (irq == xhdmi->hdcp1x_irq) {
		XHdcp1x_WriteReg(HdmiTxSsPtr->Hdcp14Ptr->Config.BaseAddress,
			XHDCP1X_CIPHER_REG_INTERRUPT_MASK, (u32)0xFFFFFFFDu);
	} else if (irq == xhdmi->hdcp1x_timer_irq) {
		XTmrCtr_EnableIntr(HdmiTxSsPtr->HdcpTimerPtr->BaseAddress, 0);
	} else if (irq == xhdmi->hdcp22_timer_irq) {
		XTmrCtr_EnableIntr(HdmiTxSsPtr->Hdcp22Ptr->Timer.TmrCtr.BaseAddress, 0);
		XTmrCtr_EnableIntr(HdmiTxSsPtr->Hdcp22Ptr->Timer.TmrCtr.BaseAddress, 1);
	}
	spin_unlock_irqrestore(&xhdmi->irq_lock, flags);

	return IRQ_HANDLED;
}

static void hdcp_protect_content(struct xlnx_drm_hdmi *xhdmi)
{
	XV_HdmiTxSs *HdmiTxSsPtr;
	HdmiTxSsPtr = &xhdmi->xv_hdmitxss;

	if (!XV_HdmiTxSs_HdcpIsReady(HdmiTxSsPtr)) return;
	/* content must be protected but is not encrypted? */
	if (xhdmi->hdcp_protect && (!xhdmi->hdcp_encrypted)) {
		/* blank content instead of encrypting */
		XV_HdmiTxSs_HdcpEnableBlank(HdmiTxSsPtr);
	} else {
		/* do not blank content; either no protection required or already encrypted */
		XV_HdmiTxSs_HdcpDisableBlank(HdmiTxSsPtr);
	}
}

static void XHdcp_Authenticate(XV_HdmiTxSs *HdmiTxSsPtr)
{
	if (!XV_HdmiTxSs_HdcpIsReady(HdmiTxSsPtr)) return;
	if (XV_HdmiTxSs_IsStreamUp(HdmiTxSsPtr)) {
		/* Trigger authentication on Idle */
		if (!(XV_HdmiTxSs_HdcpIsAuthenticated(HdmiTxSsPtr)) &&
			!(XV_HdmiTxSs_HdcpIsInProgress(HdmiTxSsPtr))) {
			XV_HdmiTxSs_HdcpPushEvent(HdmiTxSsPtr, XV_HDMITXSS_HDCP_AUTHENTICATE_EVT);
		}
		/* Trigger authentication on Toggle */
		else if (XV_HdmiTxSs_IsStreamToggled(HdmiTxSsPtr)) {
			XV_HdmiTxSs_HdcpPushEvent(HdmiTxSsPtr, XV_HDMITXSS_HDCP_AUTHENTICATE_EVT);
		}
	}
}

// Send Vendor Specific InfoFrame
static void SendVSInfoframe(XV_HdmiTxSs *HdmiTxSsPtr)
{
	XHdmiC_VSIF *VSIFPtr;
	XHdmiC_Aux Aux;

	VSIFPtr = XV_HdmiTxSs_GetVSIF(HdmiTxSsPtr);

	(void)memset((void *)VSIFPtr, 0, sizeof(XHdmiC_VSIF));
	(void)memset((void *)&Aux, 0, sizeof(XHdmiC_Aux));

	VSIFPtr->Version = 0x1;
	VSIFPtr->IEEE_ID = 0xC03;

	if (XVidC_IsStream3D(&(HdmiTxSsPtr->HdmiTxPtr->Stream.Video))) {
		VSIFPtr->Format = XHDMIC_VSIF_VF_3D;
		VSIFPtr->Info_3D.Stream = HdmiTxSsPtr->HdmiTxPtr->Stream.Video.Info_3D;
		VSIFPtr->Info_3D.MetaData.IsPresent = FALSE;
	} else if (HdmiTxSsPtr->HdmiTxPtr->Stream.Video.VmId == XVIDC_VM_3840x2160_24_P ||
			   HdmiTxSsPtr->HdmiTxPtr->Stream.Video.VmId == XVIDC_VM_3840x2160_25_P ||
			   HdmiTxSsPtr->HdmiTxPtr->Stream.Video.VmId == XVIDC_VM_3840x2160_30_P ||
			   HdmiTxSsPtr->HdmiTxPtr->Stream.Video.VmId == XVIDC_VM_4096x2160_24_P) {
		VSIFPtr->Format = XHDMIC_VSIF_VF_EXTRES;

		/* Set HDMI VIC */
		switch(HdmiTxSsPtr->HdmiTxPtr->Stream.Video.VmId) {
			case XVIDC_VM_4096x2160_24_P :
				VSIFPtr->HDMI_VIC = 4;
				break;
			case XVIDC_VM_3840x2160_24_P :
				VSIFPtr->HDMI_VIC = 3;
				break;
			case XVIDC_VM_3840x2160_25_P :
				VSIFPtr->HDMI_VIC = 2;
				break;
			case XVIDC_VM_3840x2160_30_P :
				VSIFPtr->HDMI_VIC = 1;
				break;
			default :
				break;
		}
	} else {
		VSIFPtr->Format = XHDMIC_VSIF_VF_NOINFO;
	}

	Aux = XV_HdmiC_VSIF_GeneratePacket(VSIFPtr);

	XV_HdmiTxSs_SendGenericAuxInfoframe(HdmiTxSsPtr, &Aux);
}

/* Send out all the InfoFrames in the AuxFifo during PassThrough mode
 * or send out AVI, Audio, Vendor Specific InfoFrames.
 */
static void SendInfoframe(XV_HdmiTxSs *HdmiTxSsPtr)
{
	u32 Status;
	XHdmiC_AVI_InfoFrame *AviInfoFramePtr;
	XHdmiC_AudioInfoFrame *AudioInfoFramePtr;
	XHdmiC_VSIF *VSIFPtr;
	XHdmiC_Aux AuxFifo;

	AviInfoFramePtr = XV_HdmiTxSs_GetAviInfoframe(HdmiTxSsPtr);
	AudioInfoFramePtr = XV_HdmiTxSs_GetAudioInfoframe(HdmiTxSsPtr);
	VSIFPtr = XV_HdmiTxSs_GetVSIF(HdmiTxSsPtr);
	Status = (XST_FAILURE);

	// Generate Aux from the current TX InfoFrame
	AuxFifo = XV_HdmiC_AVIIF_GeneratePacket(AviInfoFramePtr);
	XV_HdmiTxSs_SendGenericAuxInfoframe(HdmiTxSsPtr, &AuxFifo);

	/* GCP does not need to be sent out because GCP packets on the TX side is
	   handled by the HDMI TX core fully. */

	SendVSInfoframe(HdmiTxSsPtr);
}

static void TxToggleCallback(void *CallbackRef)
{
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)CallbackRef;
	XV_HdmiTxSs *HdmiTxSsPtr = &xhdmi->xv_hdmitxss;

	dev_dbg(xhdmi->dev,"%s()\n", __func__);
	XV_HdmiTxSs_StreamStart(HdmiTxSsPtr);
	if (XV_HdmiTxSs_HdcpIsReady(HdmiTxSsPtr) && xhdmi->hdcp_authenticate) {
		XHdcp_Authenticate(HdmiTxSsPtr);
	}
}

static void TxConnectCallback(void *CallbackRef)
{
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)CallbackRef;
	XV_HdmiTxSs *HdmiTxSsPtr = &xhdmi->xv_hdmitxss;
	XVphy *VphyPtr = xhdmi->xvphy;
	XHdmiphy1 *XGtPhyPtr = xhdmi->xgtphy;

	dev_dbg(xhdmi->dev,"%s()\n", __func__);
	xvphy_mutex_lock(xhdmi->phy[0]);
	if (HdmiTxSsPtr->IsStreamConnected) {
		int xst_hdmi20;
		xhdmi->cable_connected = 1;
		xhdmi->connector.status = connector_status_connected;
		/* Check HDMI sink version */
		xst_hdmi20 = XV_HdmiTxSs_DetectHdmi20(HdmiTxSsPtr);
		dev_dbg(xhdmi->dev,"TxConnectCallback(): TX connected to HDMI %s Sink Device\n",
			(xst_hdmi20 == XST_SUCCESS)? "2.0": "1.4");
		xhdmi->is_hdmi_20_sink = (xst_hdmi20 == XST_SUCCESS);
		if (xhdmi->isvphy)
			XVphy_IBufDsEnable(VphyPtr, 0, XVPHY_DIR_TX, (TRUE));
		else
			XHdmiphy1_IBufDsEnable(XGtPhyPtr, 0, XHDMIPHY1_DIR_TX, (TRUE));
		XV_HdmiTxSs_StreamStart(HdmiTxSsPtr);
		/* stream never goes down on disconnect. Force hdcp event */
		if (xhdmi->hdmi_stream_up &&
			XV_HdmiTxSs_HdcpIsReady(HdmiTxSsPtr) &&
			xhdmi->hdcp_authenticate) {
			/* Push the Authenticate event to the HDCP event queue */
			XV_HdmiTxSs_HdcpPushEvent(HdmiTxSsPtr, XV_HDMITXSS_HDCP_AUTHENTICATE_EVT);
		}
	}
	else {
		dev_dbg(xhdmi->dev,"TxConnectCallback(): TX disconnected\n");
		xhdmi->cable_connected = 0;
		xhdmi->connector.status = connector_status_disconnected;
		xhdmi->have_edid = 0;
		xhdmi->is_hdmi_20_sink = 0;
		/* do not disable ibufds - stream will not go down*/
		if (xhdmi->isvphy)
			XVphy_IBufDsEnable(VphyPtr, 0, XVPHY_DIR_TX, (FALSE));
		else
			XHdmiphy1_IBufDsEnable(XGtPhyPtr, 0, XHDMIPHY1_DIR_TX, (FALSE));
	}
	xvphy_mutex_unlock(xhdmi->phy[0]);

	if(xhdmi->connector.dev) {
		/* Not using drm_kms_helper_hotplug_event because apart from notifying
		 * user space about hotplug, it also calls output_poll_changed of drm device
		 * that is used to inform the fbdev helper of output changes. It is of no use
		 * here. Sometimes there were hang issue while running the application
		 * on using drm_kms_helper_hotplug_event API. */
		drm_sysfs_hotplug_event(xhdmi->connector.dev);
		//drm_kms_helper_hotplug_event(xhdmi->drm_dev);
		dev_dbg(xhdmi->dev,"Hotplug event sent to user space, Connect = %d", xhdmi->connector.status);
	} else {
		printk(KERN_WARNING "Not sending HOTPLUG event because "
				"drm device is NULL as drm_connector_init is not called yet.\n");
	}

	dev_dbg(xhdmi->dev,"TxConnectCallback() done\n");
}

static void TxStreamUpCallback(void *CallbackRef)
{
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)CallbackRef;
	XVphy *VphyPtr;
	XHdmiphy1 *XGtPhyPtr;
	XV_HdmiTxSs *HdmiTxSsPtr;
	XVphy_PllType TxPllType;
	XHdmiphy1_PllType GtTxPllType;
	u64 TxLineRate = 0;
	XHdmiC_AVI_InfoFrame *AVIInfoFramePtr;
	XVidC_VideoStream *HdmiTxSsVidStreamPtr;

	HdmiTxSsPtr = &xhdmi->xv_hdmitxss;

	VphyPtr = xhdmi->xvphy;
	XGtPhyPtr = xhdmi->xgtphy;

	dev_dbg(xhdmi->dev,"TxStreamUpCallback(): TX stream is up\n");
	/* Ensure that the bridge SYSRST is not released */
	XV_HdmiTxSs_SYSRST(HdmiTxSsPtr, TRUE);
	xhdmi->hdmi_stream_up = 1;

	AVIInfoFramePtr = XV_HdmiTxSs_GetAviInfoframe(HdmiTxSsPtr);
	HdmiTxSsVidStreamPtr = XV_HdmiTxSs_GetVideoStream(HdmiTxSsPtr);
	if ( (HdmiTxSsVidStreamPtr->VmId == XVIDC_VM_1440x480_60_I) ||
		 (HdmiTxSsVidStreamPtr->VmId == XVIDC_VM_1440x576_50_I) ) {
		AVIInfoFramePtr->PixelRepetition =
				XHDMIC_PIXEL_REPETITION_FACTOR_2;
		dev_dbg(xhdmi->dev,"Pixel repetition set to 2\n");
	} else {
		AVIInfoFramePtr->PixelRepetition =
				XHDMIC_PIXEL_REPETITION_FACTOR_1;
		dev_dbg(xhdmi->dev,"Pixel repetition set to 1\n");
	}

	xvphy_mutex_lock(xhdmi->phy[0]);
	if (xhdmi->isvphy) {
		TxPllType = XVphy_GetPllType(VphyPtr, 0, XVPHY_DIR_TX, XVPHY_CHANNEL_ID_CH1);
		if ((TxPllType == XVPHY_PLL_TYPE_CPLL)) {
			TxLineRate = XVphy_GetLineRateHz(VphyPtr, 0, XVPHY_CHANNEL_ID_CH1);
		}
		else if((TxPllType == XVPHY_PLL_TYPE_QPLL) ||
			(TxPllType == XVPHY_PLL_TYPE_QPLL0) ||
			(TxPllType == XVPHY_PLL_TYPE_PLL0)) {
			TxLineRate = XVphy_GetLineRateHz(VphyPtr, 0, XVPHY_CHANNEL_ID_CMN0);
		}
		else {
			TxLineRate = XVphy_GetLineRateHz(VphyPtr, 0, XVPHY_CHANNEL_ID_CMN1);
		}
	} else {
		GtTxPllType = XHdmiphy1_GetPllType(XGtPhyPtr, 0, XHDMIPHY1_DIR_TX, XHDMIPHY1_CHANNEL_ID_CH1);
		/* Versal - Implementation different here */
		if (GtTxPllType == XHDMIPHY1_PLL_TYPE_LCPLL) {
			TxLineRate =
				XHdmiphy1_GetLineRateHz(XGtPhyPtr, 0, XHDMIPHY1_CHANNEL_ID_CMN0);
			dev_dbg(xhdmi->dev, "GtPhy TxLineRate LCPLL %lld Kbps\r\n",(TxLineRate/1000));
		} else if (GtTxPllType == XHDMIPHY1_PLL_TYPE_RPLL) {
			TxLineRate =
				XHdmiphy1_GetLineRateHz(XGtPhyPtr, 0, XHDMIPHY1_CHANNEL_ID_CMN1);
			dev_dbg(xhdmi->dev, "GtPhy TxLineRate RPLL %lld Kbps\r\n",(TxLineRate/1000));
		} else {
			dev_err(xhdmi->dev, "GtPhy Error! Invalid GtTxPllType in TxStreamUpCallback.\r\n");
		}
	}

	/* configure an external retimer through a (virtual) CCF clock
	 * (this was tested against the DP159 misc retimer driver) */
	if (xhdmi->retimer_clk) {
		dev_dbg(xhdmi->dev,"retimer: clk_set_rate(xhdmi->retimer_clk, TxLineRate=%lld\n", TxLineRate);
		(void)clk_set_rate(xhdmi->retimer_clk, (signed long long)TxLineRate);
	}
	/* Copy Sampling Rate */
	if (xhdmi->isvphy)
		XV_HdmiTxSs_SetSamplingRate(HdmiTxSsPtr, VphyPtr->HdmiTxSampleRate);
	else
		XV_HdmiTxSs_SetSamplingRate(HdmiTxSsPtr, XGtPhyPtr->HdmiTxSampleRate);

	/* Enable TX TMDS clock*/
	if (xhdmi->isvphy)
		XVphy_Clkout1OBufTdsEnable(VphyPtr, XVPHY_DIR_TX, (TRUE));
	else
		XHdmiphy1_Clkout1OBufTdsEnable(XGtPhyPtr, XHDMIPHY1_DIR_TX, (TRUE));

	xvphy_mutex_unlock(xhdmi->phy[0]);

#ifdef DEBUG
	XV_HdmiTx_DebugInfo(HdmiTxSsPtr->HdmiTxPtr);
#endif
	if (xhdmi->hdcp_authenticate) {
		XHdcp_Authenticate(HdmiTxSsPtr);
	}

	/* Enable Audio */
	if (xhdmi->audio_enabled) {
		XV_HdmiTxSs_AudioMute(HdmiTxSsPtr, 0);
	}

	/* Check if Link Ready and Video Ready bits are set in PIO_IN register */
	if ((XV_HDMITX_PIO_IN_VID_RDY_MASK | XV_HDMITX_PIO_IN_LNK_RDY_MASK) &&
			XV_HdmiTx_ReadReg((uintptr_t)xhdmi->iomem,
					XV_HDMITX_PIO_IN_OFFSET)) {
		xhdmi->wait_for_streamup = 1;
	} else {
		xhdmi->wait_for_streamup = 0;
	}

	wake_up(&xhdmi->wait_event);

	/* When YUYV is base color format for pl display then HDMI doesn't come up
	 * first couple of tries as the EXT_SYSRST (bit 22) is cleared.
	 * Set this bit in case stream is up and encoder is enabled
	 */
	if (xhdmi->dpms == DRM_MODE_DPMS_ON && HdmiTxSsPtr->HdmiTxPtr->Stream.IsConnected)
		XV_HdmiTxSs_SYSRST(HdmiTxSsPtr, FALSE);

	dev_dbg(xhdmi->dev,"TxStreamUpCallback(): done\n");
}

static void TxStreamDownCallback(void *CallbackRef)
{
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)CallbackRef;

	dev_dbg(xhdmi->dev,"TxStreamDownCallback(): TX stream is down\n\r");
	xhdmi->hdmi_stream_up = 0;

	xhdmi->hdcp_authenticated = 0;
	xhdmi->hdcp_encrypted = 0;
	hdcp_protect_content(xhdmi);
}

static void TxVsCallback(void *CallbackRef)
{
	XHdmiC_Aux aud_aux_fifo;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)CallbackRef;
	XV_HdmiTxSs *HdmiTxSsPtr = &xhdmi->xv_hdmitxss;
	struct hdmi_drm_infoframe frame;
	struct v4l2_hdr10_payload *DRMInfoFramePtr =
				XV_HdmiTxSs_GetDrmInfoframe(HdmiTxSsPtr);
	XHdmiC_Aux hdr_aux_fifo;
	struct drm_connector_state *state = xhdmi->connector.state;

	/* Send NULL Aux packet */
	SendInfoframe(HdmiTxSsPtr);

	if (xhdmi->audio_init) {
		aud_aux_fifo.Header.Byte[0] = xhdmi->tx_audio_data->buffer[0];
		aud_aux_fifo.Header.Byte[1] = xhdmi->tx_audio_data->buffer[1];
		aud_aux_fifo.Header.Byte[2] = xhdmi->tx_audio_data->buffer[2];
		aud_aux_fifo.Header.Byte[3] = 0;

		aud_aux_fifo.Data.Byte[0] = xhdmi->tx_audio_data->buffer[3];
		aud_aux_fifo.Data.Byte[1] = xhdmi->tx_audio_data->buffer[4];
		aud_aux_fifo.Data.Byte[2] = xhdmi->tx_audio_data->buffer[5];
		aud_aux_fifo.Data.Byte[3] = xhdmi->tx_audio_data->buffer[6];
		aud_aux_fifo.Data.Byte[4] = xhdmi->tx_audio_data->buffer[7];
		aud_aux_fifo.Data.Byte[5] = xhdmi->tx_audio_data->buffer[8];

		XV_HdmiTxSs_SendGenericAuxInfoframe(HdmiTxSsPtr,
							&aud_aux_fifo);
	}

	if (!state->gen_hdr_output_metadata)
		return;

	drm_hdmi_infoframe_set_gen_hdr_metadata(&frame, state);
	/* hdmi_drm_infoframe to v4l2_hdr10_payload */
	DRMInfoFramePtr->eotf = (__u8) frame.eotf;
	DRMInfoFramePtr->metadata_type = (__u8) frame.metadata_type;
	DRMInfoFramePtr->display_primaries[0].x =
				(__u16) frame.display_primaries[0].x;
	DRMInfoFramePtr->display_primaries[0].y =
				(__u16) frame.display_primaries[0].y;
	DRMInfoFramePtr->display_primaries[1].x =
				(__u16) frame.display_primaries[1].x;
	DRMInfoFramePtr->display_primaries[1].y =
				(__u16) frame.display_primaries[1].y;
	DRMInfoFramePtr->display_primaries[2].x =
				(__u16) frame.display_primaries[2].x;
	DRMInfoFramePtr->display_primaries[2].y =
				(__u16) frame.display_primaries[2].y;
	DRMInfoFramePtr->white_point.x = (__u16) frame.white_point.x;
	DRMInfoFramePtr->white_point.y = (__u16) frame.white_point.y;
	DRMInfoFramePtr->max_mdl =
				(__u16) frame.max_display_mastering_luminance;
	DRMInfoFramePtr->min_mdl =
				(__u16) frame.min_display_mastering_luminance;
	DRMInfoFramePtr->max_cll = (__u16) frame.max_cll;
	DRMInfoFramePtr->max_fall = (__u16) frame.max_fall;

	XV_HdmiC_DRMIF_GeneratePacket(DRMInfoFramePtr, &hdr_aux_fifo);
	XV_HdmiTxSs_SendGenericAuxInfoframe(HdmiTxSsPtr, &hdr_aux_fifo);
}

void TxBrdgUnlockedCallback(void *CallbackRef)
{
	/* When video out bridge lost lock, reset TPG */
	/* ResetTpg();                                */
	/* Config and Run the TPG                     */
	/* XV_ConfigTpg(&Tpg);                        */
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)CallbackRef;
	dev_dbg(xhdmi->dev,"TX Bridge Unlocked Callback\r\n");
}

void TxBrdgOverflowCallback(void *CallbackRef)
{
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)CallbackRef;
	dev_dbg(xhdmi->dev,"TX Video Bridge Overflow\r\n");
}

void TxBrdgUnderflowCallback(void *CallbackRef)
{
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)CallbackRef;
	dev_dbg(xhdmi->dev,"TX Video Bridge Underflow\r\n");
}

void TxHdcpAuthenticatedCallback(void *CallbackRef)
{
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)CallbackRef;
	XV_HdmiTxSs *HdmiTxSsPtr = &xhdmi->xv_hdmitxss;

	xhdmi->hdcp_authenticated = 1;
	if (XV_HdmiTxSs_HdcpGetProtocol(HdmiTxSsPtr) == XV_HDMITXSS_HDCP_22) {
		dev_dbg(xhdmi->dev,"HDCP 2.2 TX authenticated.\n");
	}

	else if (XV_HdmiTxSs_HdcpGetProtocol(HdmiTxSsPtr) == XV_HDMITXSS_HDCP_14) {
		dev_dbg(xhdmi->dev,"HDCP 1.4 TX authenticated.\n");
	}

	if (xhdmi->hdcp_encrypt) {
		dev_dbg(xhdmi->dev,"Enabling Encryption.\n");
		XV_HdmiTxSs_HdcpEnableEncryption(HdmiTxSsPtr);
		xhdmi->hdcp_encrypted = 1;
		hdcp_protect_content(xhdmi);
	} else {
		dev_dbg(xhdmi->dev,"Not Enabling Encryption.\n");
	}
}

void TxHdcpUnauthenticatedCallback(void *CallbackRef)
{
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)CallbackRef;

	dev_dbg(xhdmi->dev,"TxHdcpUnauthenticatedCallback()\n");
	xhdmi->hdcp_authenticated = 0;
	xhdmi->hdcp_encrypted = 0;
	hdcp_protect_content(xhdmi);
}

/* entered with vphy mutex taken */
static void VphyHdmiTxInitCallback(void *CallbackRef)
{
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)CallbackRef;
	XVphy *VphyPtr;
	XHdmiphy1 *XGtPhyPtr;
	XV_HdmiTxSs *HdmiTxSsPtr;

	HdmiTxSsPtr = &xhdmi->xv_hdmitxss;

	if (xhdmi->isvphy)
		VphyPtr = xhdmi->xvphy;
	else
		XGtPhyPtr = xhdmi->xgtphy;

	dev_dbg(xhdmi->dev,"VphyHdmiTxInitCallback(): XV_HdmiTxSs_RefClockChangeInit()\n");

	/* a pair of mutexes must be locked in fixed order to prevent deadlock,
	 * and the order is TX SS then XVPHY, so first unlock XVPHY then lock both */
	xvphy_mutex_unlock(xhdmi->phy[0]);
	hdmi_mutex_lock(&xhdmi->hdmi_mutex);
	xvphy_mutex_lock(xhdmi->phy[0]);

	XV_HdmiTxSs_RefClockChangeInit(HdmiTxSsPtr);
	/* unlock TX SS mutex but keep XVPHY locked */
	hdmi_mutex_unlock(&xhdmi->hdmi_mutex);
	dev_dbg(xhdmi->dev,"VphyHdmiTxInitCallback() done\n");
}

/* entered with vphy mutex taken */
static void VphyHdmiTxReadyCallback(void *CallbackRef)
{
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)CallbackRef;

	dev_dbg(xhdmi->dev,"VphyHdmiTxReadyCallback(NOP) done\n");
}

/*
 * DRM connector functions
 */

static enum drm_connector_status
xlnx_drm_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	/* it takes HDMI 50 ms to detect connection on init */
	static int first_time_ms = 50;
	struct xlnx_drm_hdmi *xhdmi = connector_to_hdmi(connector);
	/* first time; wait 50 ms max until cable connected */
	while (first_time_ms && !xhdmi->cable_connected) {
		msleep(1);
		first_time_ms--;
	}
	/* connected in less than 50 ms? */
	if (first_time_ms) {
		/* do not wait during further connect detects */
		first_time_ms = 0;
		/* after first time, report immediately */
		dev_dbg(xhdmi->dev,"xlnx_drm_hdmi_connector_detect() waited %d ms until connect.\n", 50 - first_time_ms);
	}
	hdmi_mutex_lock(&xhdmi->hdmi_mutex);
	/* cable connected  */
	if (xhdmi->cable_connected) {
		hdmi_mutex_unlock(&xhdmi->hdmi_mutex);
		dev_dbg(xhdmi->dev, "xlnx_drm_hdmi_connector_detect() = connected\n");
		return connector_status_connected;
	}
	hdmi_mutex_unlock(&xhdmi->hdmi_mutex);
	dev_dbg(xhdmi->dev, "xlnx_drm_hdmi_connector_detect() = disconnected\n");
	return connector_status_disconnected;
}

static void xlnx_drm_hdmi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	connector->dev = NULL;
}

static const struct drm_connector_funcs xlnx_drm_hdmi_connector_funcs = {
//	.dpms			= drm_helper_connector_dpms,
	.detect			= xlnx_drm_hdmi_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= xlnx_drm_hdmi_connector_destroy,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
	.reset			= drm_atomic_helper_connector_reset,
};

static int xlnx_drm_hdmi_connector_mode_valid(struct drm_connector *connector,
				    struct drm_display_mode *mode)
{
	struct xlnx_drm_hdmi *xhdmi = connector_to_hdmi(connector);
	int max_rate = 340 * 1000;
	enum drm_mode_status status = MODE_OK;

	dev_dbg(xhdmi->dev, "%s\n", __func__);

	/* This is done to make the functionality similar as BM code in which the
	 * timing table has vdisplay value of 540 for 1080i usecase.
	 * By this change, doing modetest -M xlnx, will give vdisplay 540 instead of
	 * 1080 */
	if(mode->flags & DRM_MODE_FLAG_INTERLACE) {
		mode->vdisplay = mode->vdisplay / 2;
		dev_dbg(xhdmi->dev, "For DRM_MODE_FLAG_INTERLACE, divide mode->vdisplay %d\n", mode->vdisplay);
	}

	if((mode->flags & DRM_MODE_FLAG_DBLCLK) && (mode->flags & DRM_MODE_FLAG_INTERLACE)) {
		mode->clock *= 2;
		/* This logic is needed because the value of vrefresh is coming as zero for 480i@60 and 576i@50
		 * because of which after multiplying the pixel clock by 2, the mode getting selected is 480i@120
		 * 576i@100 from drm_edid.c file as this becomes the matching mode.
		 * Seems like bug in the kernel code for handling of DRM_MODE_FLAG_DBLCLK flag.
		 */
		if(mode->vrefresh == 0)
		{
			if(mode->vdisplay == 240)
				mode->vrefresh = 60;
			else if (mode->vdisplay == 288)
				mode->vrefresh = 50;
		}
		dev_dbg(xhdmi->dev, "For DRM_MODE_FLAG_DBLCLK, multiply pixel_clk by 2, New pixel clock %d, refresh rate = %d\n", mode->clock, mode->vrefresh);
	}

	drm_mode_debug_printmodeline(mode);
	hdmi_mutex_lock(&xhdmi->hdmi_mutex);
	/* HDMI 2.0 sink connected? */
	if (xhdmi->is_hdmi_20_sink)
		max_rate = 600 * 1000;
	/* pixel clock too high for sink? */
	if (mode->clock > max_rate)
		status = MODE_CLOCK_HIGH;
	hdmi_mutex_unlock(&xhdmi->hdmi_mutex);
	return status;
}

/* callback function for drm_do_get_edid(), used in xlnx_drm_hdmi_get_modes()
 * through drm_do_get_edid() from drm/drm_edid.c.
 *
 * called with hdmi_mutex taken
 *
 * Return 0 on success, !0 otherwise
 */
static int xlnx_drm_hdmi_get_edid_block(void *data, u8 *buf, unsigned int block,
				  size_t len)
{
	u8 *buffer;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)data;
	XV_HdmiTxSs *HdmiTxSsPtr;
	int ret;

	/* out of bounds? */
	if (((block * 128) + len) > 256) return -EINVAL;

	buffer = kzalloc(256, GFP_KERNEL);
	if (!buffer) return -ENOMEM;

	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	if (!HdmiTxSsPtr->IsStreamConnected) {
		dev_dbg(xhdmi->dev, "xlnx_drm_hdmi_get_edid_block() stream is not connected\n");
	}
	/* first obtain edid in local buffer */
	ret = XV_HdmiTxSs_ReadEdid(HdmiTxSsPtr, buffer);
	if (ret == XST_FAILURE) {
		dev_dbg(xhdmi->dev, "xlnx_drm_hdmi_get_edid_block() failed reading EDID\n");
		return -EINVAL;
	}

	/* then copy the requested 128-byte block(s) */
	memcpy(buf, buffer + block * 128, len);
	/* free our local buffer */
	kfree(buffer);
	return 0;
}

static int xlnx_drm_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct xlnx_drm_hdmi *xhdmi = connector_to_hdmi(connector);
	struct edid *edid = NULL;
	int ret;
	bool is_hdmi_sink;

	dev_dbg(xhdmi->dev, "%s\n", __func__);
	hdmi_mutex_lock(&xhdmi->hdmi_mutex);

	/* When the I2C adapter connected to the DDC bus is hidden behind a device that
	 * exposes a different interface to read EDID blocks this function can be used
	 * to get EDID data using a custom block read function. - from drm_edid.c
	 */

	/* private data hdmi is passed to xlnx_drm_hdmi_get_edid_block(data, ...) */
	edid = drm_do_get_edid(connector, xlnx_drm_hdmi_get_edid_block, xhdmi);

	hdmi_mutex_unlock(&xhdmi->hdmi_mutex);
	if (!edid) {
		xhdmi->have_edid = 0;
		dev_err(xhdmi->dev, "xlnx_drm_hdmi_get_modes() could not obtain edid, assume <= 1024x768 works.\n");
		drm_connector_update_edid_property(connector, NULL);
		return 0;
	}
	xhdmi->have_edid = 1;

	/* If the sink is non HDMI, set the stream type to DVI else HDMI */
	is_hdmi_sink = drm_detect_hdmi_monitor(edid);
	if(is_hdmi_sink) {
		XV_HdmiTxSs_SetVideoStreamType(&xhdmi->xv_hdmitxss, 1);
		dev_dbg(xhdmi->dev, "EDID shows HDMI sink is connected, setting stream type to HDMI\n");
	} else {
		XV_HdmiTxSs_SetVideoStreamType(&xhdmi->xv_hdmitxss, 0);
		dev_dbg(xhdmi->dev, "EDID shows non HDMI sink is connected, setting stream type to DVI\n");
	}

	drm_connector_update_edid_property(connector, edid);
	ret = drm_add_edid_modes(connector, edid);
	kfree(edid);
	dev_dbg(xhdmi->dev, "xlnx_drm_hdmi_get_modes() done\n");
	return ret;
}

static struct drm_encoder *
xlnx_drm_hdmi_connector_best_encoder(struct drm_connector *connector)
{
	struct xlnx_drm_hdmi *xhdmi = connector_to_hdmi(connector);

	return &xhdmi->encoder;
}

static struct drm_connector_helper_funcs xlnx_drm_hdmi_connector_helper_funcs = {
	.get_modes		= xlnx_drm_hdmi_connector_get_modes,
	.best_encoder	= xlnx_drm_hdmi_connector_best_encoder,
	.mode_valid		= xlnx_drm_hdmi_connector_mode_valid,
};

/*
 * DRM encoder functions
 */
static void xlnx_drm_hdmi_encoder_dpms(struct drm_encoder *encoder, int dpms)
{
	struct xlnx_drm_hdmi *xhdmi = encoder_to_hdmi(encoder);
	hdmi_mutex_lock(&xhdmi->hdmi_mutex);
	dev_dbg(xhdmi->dev,"xilinx_drm_hdmi_dpms(dpms = %d)\n", dpms);

	if (xhdmi->dpms == dpms) {
		goto done;
	}

	xhdmi->dpms = dpms;

	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		/* power-up */
		goto done;
	default:
		/* power-down */
		goto done;
	}
done:
	hdmi_mutex_unlock(&xhdmi->hdmi_mutex);
}

static void xlnx_drm_hdmi_encoder_enable(struct drm_encoder *encoder)
{
	struct xlnx_drm_hdmi *xhdmi = encoder_to_hdmi(encoder);
	XV_HdmiTxSs *HdmiTxSsPtr = &xhdmi->xv_hdmitxss;

	xlnx_drm_hdmi_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
	/* Enable the EXT VRST which actually starts the bridge */
	XV_HdmiTxSs_SYSRST(HdmiTxSsPtr, FALSE);
}

static void xlnx_drm_hdmi_encoder_disable(struct drm_encoder *encoder)
{
	struct xlnx_drm_hdmi *xhdmi = encoder_to_hdmi(encoder);
	XV_HdmiTxSs *HdmiTxSsPtr = &xhdmi->xv_hdmitxss;

	xlnx_drm_hdmi_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);
	/* Disable the EXT VRST which actually starts the bridge */
	XV_HdmiTxSs_SYSRST(HdmiTxSsPtr, TRUE);
}

static u32 hdmitx_find_media_bus(struct xlnx_drm_hdmi *xhdmi, u32 drm_fourcc)
{
	switch(drm_fourcc) {

	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
		xhdmi->xvidc_colordepth = XVIDC_BPC_8;
		return XVIDC_CSF_RGB;
	case DRM_FORMAT_XBGR2101010:
		xhdmi->xvidc_colordepth = XVIDC_BPC_10;
		return XVIDC_CSF_RGB;

	case DRM_FORMAT_VUY888:
	case DRM_FORMAT_XVUY8888:
	case DRM_FORMAT_Y8:
		xhdmi->xvidc_colordepth = XVIDC_BPC_8;
		return XVIDC_CSF_YCRCB_444;
	case DRM_FORMAT_XVUY2101010:
	case DRM_FORMAT_Y10:
		xhdmi->xvidc_colordepth = XVIDC_BPC_10;
		return XVIDC_CSF_YCRCB_444;

	case DRM_FORMAT_YUYV: //packed, 8b
	case DRM_FORMAT_UYVY: //packed, 8b
	case DRM_FORMAT_NV16: //semi-planar, 8b
		xhdmi->xvidc_colordepth = XVIDC_BPC_8;
		return XVIDC_CSF_YCRCB_422;
	case DRM_FORMAT_XV20: //semi-planar, 10b
		xhdmi->xvidc_colordepth = XVIDC_BPC_10;
		return XVIDC_CSF_YCRCB_422;

	case DRM_FORMAT_NV12: //semi-planar, 8b
		xhdmi->xvidc_colordepth = XVIDC_BPC_8;
		return XVIDC_CSF_YCRCB_420;
	case DRM_FORMAT_XV15: //semi-planar, 10b
		xhdmi->xvidc_colordepth = XVIDC_BPC_10;
		return XVIDC_CSF_YCRCB_420;

	default:
		printk("Warning: Unknown drm_fourcc format code: %d\n", drm_fourcc);
		xhdmi->xvidc_colordepth = XVIDC_BPC_UNKNOWN;
		return XVIDC_CSF_RGB;
	}
}

/**
 * xlnx_drm_hdmi_encoder_atomic_mode_set -  drive the HDMI timing parameters
 *
 * @encoder: pointer to Xilinx DRM encoder
 * @crtc_state: DRM crtc state
 * @connector_state: DRM connector state
 *
 * This function derives the HDMI IP timing parameters from the timing
 * values given to timing module.
 */
static void xlnx_drm_hdmi_encoder_atomic_mode_set(struct drm_encoder *encoder,
				  struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *connector_state)
{
	struct xlnx_drm_hdmi *xhdmi = encoder_to_hdmi(encoder);
	struct drm_display_mode *mode = &crtc_state->mode;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;

	XVidC_VideoTiming vt;
	XVphy *VphyPtr;
	XHdmiphy1 *XGtPhyPtr;
	XV_HdmiTxSs *HdmiTxSsPtr;
	XVidC_VideoStream *HdmiTxSsVidStreamPtr;
	XHdmiC_AVI_InfoFrame *AviInfoFramePtr;
	XHdmiC_VSIF *VSIFPtr;
	u32 TmdsClock = 0;
	u32 PrevPhyTxRefClock = 0;
	u32 Result;
	u32 drm_fourcc;
	int ret;

	dev_dbg(xhdmi->dev,"%s\n", __func__);
	HdmiTxSsPtr = &xhdmi->xv_hdmitxss;

	VphyPtr = xhdmi->xvphy;
	XGtPhyPtr = xhdmi->xgtphy;

	hdmi_mutex_lock(&xhdmi->hdmi_mutex);
	xvphy_mutex_lock(xhdmi->phy[0]);
	drm_mode_debug_printmodeline(mode);

	drm_fourcc = encoder->crtc->primary->state->fb->format->format;
	xhdmi->xvidc_colorfmt = hdmitx_find_media_bus(xhdmi, drm_fourcc);
	dev_dbg(xhdmi->dev,"xvidc_colorfmt = %d\n", xhdmi->xvidc_colorfmt);
	dev_dbg(xhdmi->dev,"xvidc_colordepth = %d\n", xhdmi->xvidc_colordepth);

	dev_dbg(xhdmi->dev,"mode->clock = %d\n", mode->clock * 1000);
	dev_dbg(xhdmi->dev,"mode->crtc_clock = %d\n", mode->crtc_clock * 1000);
	dev_dbg(xhdmi->dev,"mode->pvsync = %d\n",
		!!(mode->flags & DRM_MODE_FLAG_PVSYNC));
	dev_dbg(xhdmi->dev,"mode->phsync = %d\n",
		!!(mode->flags & DRM_MODE_FLAG_PHSYNC));
	dev_dbg(xhdmi->dev,"mode->hsync_end = %d\n", mode->hsync_end);
	dev_dbg(xhdmi->dev,"mode->hsync_start = %d\n", mode->hsync_start);
	dev_dbg(xhdmi->dev,"mode->vsync_end = %d\n", mode->vsync_end);
	dev_dbg(xhdmi->dev,"mode->vsync_start = %d\n", mode->vsync_start);
	dev_dbg(xhdmi->dev,"mode->hdisplay = %d\n", mode->hdisplay);
	dev_dbg(xhdmi->dev,"mode->vdisplay = %d\n", mode->vdisplay);
	dev_dbg(xhdmi->dev,"mode->htotal = %d\n", mode->htotal);
	dev_dbg(xhdmi->dev,"mode->vtotal = %d\n", mode->vtotal);
	dev_dbg(xhdmi->dev,"mode->vrefresh = %d\n", mode->vrefresh);
	dev_dbg(xhdmi->dev,"mode->flags = %d interlace = %d\n", mode->flags,
			!!(mode->flags & DRM_MODE_FLAG_INTERLACE));

	/* see slide 20 of http://events.linuxfoundation.org/sites/events/files/slides/brezillon-drm-kms.pdf */
	vt.HActive = mode->hdisplay;
	vt.HFrontPorch = mode->hsync_start - mode->hdisplay;
	vt.HSyncWidth = mode->hsync_end - mode->hsync_start;
	vt.HBackPorch = mode->htotal - mode->hsync_end;
	vt.HTotal = mode->htotal;
	vt.HSyncPolarity = !!(mode->flags & DRM_MODE_FLAG_PHSYNC);

	/* Enable this code for debugging of NTSC and PAL resolution */
	if((((mode->hdisplay == 720) && (mode->vdisplay == 240) && (mode->vrefresh == 60)) ||
			((mode->hdisplay == 720) && (mode->vdisplay == 288) && (mode->vrefresh == 50)))
			&& (mode->flags & DRM_MODE_FLAG_INTERLACE) && (mode->flags & DRM_MODE_FLAG_DBLCLK))
	{
		dev_dbg(xhdmi->dev,"NTSC/PAL\n");
		vt.HActive *= 2;
		vt.HFrontPorch *= 2;
		vt.HSyncWidth *= 2;
		vt.HBackPorch *= 2;
		vt.HTotal *= 2;
	}

	vt.VActive = mode->vdisplay;
	/* Progressive timing data is stored in field 0 */
	vt.F0PVFrontPorch = mode->vsync_start - mode->vdisplay;
	vt.F0PVSyncWidth = mode->vsync_end - mode->vsync_start;
	vt.F0PVBackPorch = mode->vtotal - mode->vsync_end;
	vt.F0PVTotal = mode->vtotal;

	if(mode->flags & DRM_MODE_FLAG_INTERLACE) {
		dev_dbg(xhdmi->dev,"Programming fields for interlace");

		vt.VActive = mode->vdisplay;

		vt.F0PVFrontPorch = (mode->vsync_start - (mode->vdisplay * 2)) / 2;
		vt.F0PVSyncWidth = (mode->vsync_end - mode->vsync_start) / 2;
		vt.F0PVBackPorch = (mode->vtotal - mode->vsync_end) / 2;
		vt.F0PVTotal = mode->vdisplay + vt.F0PVFrontPorch + vt.F0PVSyncWidth
				+ vt.F0PVBackPorch;

		if((mode->vtotal - mode->vsync_end) % 2)
			vt.F1VFrontPorch = 1 + (mode->vsync_start - (mode->vdisplay * 2)) / 2;
		else
			vt.F1VFrontPorch = (mode->vsync_start - (mode->vdisplay * 2)) / 2;
		vt.F1VSyncWidth = (mode->vsync_end - mode->vsync_start) / 2;
		vt.F1VBackPorch = (mode->vtotal - mode->vsync_end) / 2;
		vt.F1VTotal = mode->vdisplay + vt.F1VFrontPorch + vt.F1VSyncWidth
				+ vt.F1VBackPorch;
	}

	vt.VSyncPolarity = !!(mode->flags & DRM_MODE_FLAG_PVSYNC);

	HdmiTxSsVidStreamPtr = XV_HdmiTxSs_GetVideoStream(HdmiTxSsPtr);
	AviInfoFramePtr = XV_HdmiTxSs_GetAviInfoframe(HdmiTxSsPtr);
	VSIFPtr = XV_HdmiTxSs_GetVSIF(HdmiTxSsPtr);

	// Reset Avi InfoFrame
	(void)memset((void *)AviInfoFramePtr, 0, sizeof(XHdmiC_AVI_InfoFrame));
	// Reset Vendor Specific InfoFrame
	(void)memset((void *)VSIFPtr, 0, sizeof(XHdmiC_VSIF));

	if (xhdmi->isvphy) {
		/* Get current Tx Ref clock from PHY */
		PrevPhyTxRefClock = VphyPtr->HdmiTxRefClkHz;

		/* Disable TX TDMS clock */
		XVphy_Clkout1OBufTdsEnable(VphyPtr, XVPHY_DIR_TX, (FALSE));
	} else {
		/* Get current Tx Ref clock from PHY */
		PrevPhyTxRefClock = XGtPhyPtr->HdmiTxRefClkHz;

		/* Disable TX TDMS clock */
		XHdmiphy1_Clkout1OBufTdsEnable(XGtPhyPtr, XHDMIPHY1_DIR_TX, (FALSE));
	}

	/* The isExtensive is made true to get the correct video timing by matching
	 * all the parameters */
	HdmiTxSsVidStreamPtr->VmId = XVidC_GetVideoModeIdExtensive(&vt,
			mode->vrefresh, !!(mode->flags & DRM_MODE_FLAG_INTERLACE), TRUE);

	dev_dbg(xhdmi->dev,"VmId = %d Interlaced = %d\n", HdmiTxSsVidStreamPtr->VmId, !!(mode->flags & DRM_MODE_FLAG_INTERLACE));
	if (HdmiTxSsVidStreamPtr->VmId == XVIDC_VM_NOT_SUPPORTED) { //no match found in timing table
		dev_dbg(xhdmi->dev,"Tx Video Mode not supported. Using DRM Timing\n");
		HdmiTxSsVidStreamPtr->VmId = XVIDC_VM_CUSTOM;
		HdmiTxSsVidStreamPtr->FrameRate = mode->vrefresh;
		HdmiTxSsVidStreamPtr->Timing = vt; //overwrite with drm detected timing
		HdmiTxSsVidStreamPtr->IsInterlaced = (!!(mode->flags & DRM_MODE_FLAG_INTERLACE));
#ifdef DEBUG
		XVidC_ReportTiming(&HdmiTxSsVidStreamPtr->Timing, !!(mode->flags & DRM_MODE_FLAG_INTERLACE));
#endif
	}

	/* The value of xvidc_colordepth is set by calling hdmitx_find_media_bus()
	 * API earlier in this function. Check whether the value is valid or not */
	if (XVIDC_BPC_UNKNOWN == xhdmi->xvidc_colordepth)
		xhdmi->xvidc_colordepth = HdmiTxSsPtr->Config.MaxBitsPerPixel;

	/* check if resolution is supported at requested bit depth */
	switch (xhdmi->xvidc_colorfmt) {
		case XVIDC_CSF_RGB:
		case XVIDC_CSF_YCRCB_444:
			if ((xhdmi->xvidc_colordepth > XVIDC_BPC_8) &&
				(mode->hdisplay >= 3840) &&
				(mode->vdisplay >= 2160) &&
				(mode->vrefresh >= XVIDC_FR_50HZ)) {
					dev_dbg(xhdmi->dev,"INFO> UHD only supports 24-bits color depth\n");
					xhdmi->xvidc_colordepth = XVIDC_BPC_8;
			}
			break;

		default:
			break;
	}

	TmdsClock = XV_HdmiTxSs_SetStream(HdmiTxSsPtr, HdmiTxSsVidStreamPtr->VmId, xhdmi->xvidc_colorfmt,
			xhdmi->xvidc_colordepth, NULL);

	//Update AVI InfoFrame
	AviInfoFramePtr->Version = 2;
	AviInfoFramePtr->ColorSpace = XV_HdmiC_XVidC_To_IfColorformat(xhdmi->xvidc_colorfmt);
	AviInfoFramePtr->VIC = HdmiTxSsPtr->HdmiTxPtr->Stream.Vic;

	if ( (HdmiTxSsVidStreamPtr->VmId == XVIDC_VM_1440x480_60_I) ||
			(HdmiTxSsVidStreamPtr->VmId == XVIDC_VM_1440x576_50_I) ) {
		AviInfoFramePtr->PixelRepetition = XHDMIC_PIXEL_REPETITION_FACTOR_2;
	} else {
		AviInfoFramePtr->PixelRepetition = XHDMIC_PIXEL_REPETITION_FACTOR_1;
	}

	// Set TX reference clock
	if (xhdmi->isvphy)
		VphyPtr->HdmiTxRefClkHz = TmdsClock;
	else
		XGtPhyPtr->HdmiTxRefClkHz = TmdsClock;

	dev_dbg(xhdmi->dev,"(TmdsClock = %u, from XV_HdmiTxSs_SetStream())\n", TmdsClock);

	if (xhdmi->isvphy) {
		dev_dbg(xhdmi->dev,"XVphy_SetHdmiTxParam(PixPerClk = %d, ColorDepth = %d, ColorFormatId=%d)\n",
		(int)HdmiTxSsVidStreamPtr->PixPerClk, (int)HdmiTxSsVidStreamPtr->ColorDepth,
		(int)HdmiTxSsVidStreamPtr->ColorFormatId);

		// Set GT TX parameters, this might change VphyPtr->HdmiTxRefClkHz
		Result = XVphy_SetHdmiTxParam(VphyPtr, 0, XVPHY_CHANNEL_ID_CHA,
					HdmiTxSsVidStreamPtr->PixPerClk,
					HdmiTxSsVidStreamPtr->ColorDepth,
					HdmiTxSsVidStreamPtr->ColorFormatId);
	} else {
		dev_dbg(xhdmi->dev,"XHdmiphy1_SetHdmiTxParam(PixPerClk = %d, ColorDepth = %d, ColorFormatId=%d)\n",
		(int)HdmiTxSsVidStreamPtr->PixPerClk, (int)HdmiTxSsVidStreamPtr->ColorDepth,
		(int)HdmiTxSsVidStreamPtr->ColorFormatId);

		// Set GT TX parameters, this might change XGtPhyPtr->HdmiTxRefClkHz
		Result = XHdmiphy1_SetHdmiTxParam(XGtPhyPtr, 0, XHDMIPHY1_CHANNEL_ID_CHA,
					HdmiTxSsVidStreamPtr->PixPerClk,
					HdmiTxSsVidStreamPtr->ColorDepth,
					HdmiTxSsVidStreamPtr->ColorFormatId);
	}
	if (Result == (XST_FAILURE)) {
		dev_dbg(xhdmi->dev,"Unable to set requested TX video resolution.\n\r");
		xvphy_mutex_unlock(xhdmi->phy[0]);
		hdmi_mutex_unlock(&xhdmi->hdmi_mutex);
		return;
	}

	if (xhdmi->isvphy)
		adjusted_mode->clock = VphyPtr->HdmiTxRefClkHz / 1000;
	else
		adjusted_mode->clock = XGtPhyPtr->HdmiTxRefClkHz / 1000;

	dev_dbg(xhdmi->dev,"adjusted_mode->clock = %u Hz\n", adjusted_mode->clock);

	/* request required tmds clock rate */
	ret = clk_set_rate(xhdmi->tmds_clk,
			   adjusted_mode->clock * (unsigned long)1000);
	if (ret) {
		dev_err(xhdmi->dev, "failed to set tmds clock rate to %d: %d\n",
					(adjusted_mode->clock * 1000), ret);
	}
	/* When switching between modes with same Phy RefClk phy tx_refxlk_rdy_en
	 * signal must be toggled (asserted and de-asserted) to reset phy's
	 * internal frequency detection state machine
	 */
	if (xhdmi->isvphy) {
		dev_dbg(xhdmi->dev,"PrevPhyTxRefClock: %d, NewRefClock: %d\n", PrevPhyTxRefClock, VphyPtr->HdmiTxRefClkHz);
		if (PrevPhyTxRefClock == VphyPtr->HdmiTxRefClkHz) {
			/* Switching between resolutions with same frequency */
			dev_dbg(xhdmi->dev,"***** Reset Phy Tx Frequency *******\n");
			XVphy_ClkDetFreqReset(VphyPtr, 0, XVPHY_DIR_TX);
		}
	} else {
		dev_dbg(xhdmi->dev,"PrevPhyTxRefClock: %d, NewRefClock: %d\n", PrevPhyTxRefClock, XGtPhyPtr->HdmiTxRefClkHz);
		if (PrevPhyTxRefClock == XGtPhyPtr->HdmiTxRefClkHz) {
			/* Switching between resolutions with same frequency */
			dev_dbg(xhdmi->dev,"***** Reset Phy Tx Frequency *******\n");
			XHdmiphy1_ClkDetFreqReset(XGtPhyPtr, 0, XHDMIPHY1_DIR_TX);
		}
	}

	xhdmi->tx_audio_data->tmds_clk = clk_get_rate(xhdmi->tmds_clk);
	/* if the mode is HDMI 2.0, use a multiplier value of 4 */
	if (HdmiTxSsPtr->HdmiTxPtr->Stream.TMDSClockRatio) {
		xhdmi->tx_audio_data->tmds_clk =
			xhdmi->tx_audio_data->tmds_clk * 4;
		xhdmi->tx_audio_data->tmds_clk_ratio = true;
	} else {
		xhdmi->tx_audio_data->tmds_clk_ratio = false;
	}

	xvphy_mutex_unlock(xhdmi->phy[0]);
	hdmi_mutex_unlock(&xhdmi->hdmi_mutex);

	xhdmi->wait_for_streamup = 0;
	wait_event_timeout(xhdmi->wait_event, xhdmi->wait_for_streamup, msecs_to_jiffies(10000));
	if (!xhdmi->wait_for_streamup)
		dev_dbg(xhdmi->dev, "wait_for_streamup timeout\n");
	/* Keep SYS_RST asserted */
	XV_HdmiTxSs_SYSRST(HdmiTxSsPtr, TRUE);
}

static const struct drm_encoder_funcs xlnx_drm_hdmi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct drm_encoder_helper_funcs xlnx_drm_hdmi_encoder_helper_funcs = {
	//.dpms				= xlnx_drm_hdmi_encoder_dpms,
	.enable				= xlnx_drm_hdmi_encoder_enable,
	.disable			= xlnx_drm_hdmi_encoder_disable,
	.atomic_mode_set	= xlnx_drm_hdmi_encoder_atomic_mode_set,
};

/* this function is responsible for periodically calling XV_HdmiTxSs_HdcpPoll()
	and XHdcp_Authenticate */
static void hdcp_poll_work(struct work_struct *work)
{
	/* find our parent container structure */
	struct xlnx_drm_hdmi *xhdmi = container_of(work, struct xlnx_drm_hdmi,
		delayed_work_hdcp_poll.work);
	XV_HdmiTxSs *HdmiTxSsPtr;

	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;

	if (XV_HdmiTxSs_HdcpIsReady(HdmiTxSsPtr)) {
		hdmi_mutex_lock(&xhdmi->hdmi_mutex);
		XV_HdmiTxSs_HdcpPoll(HdmiTxSsPtr);
		xhdmi->hdcp_auth_counter++;
		if(xhdmi->hdcp_auth_counter >= 10) { //every 10ms
			xhdmi->hdcp_auth_counter = 0;
			if (xhdmi->hdcp_authenticate) {
				XHdcp_Authenticate(HdmiTxSsPtr);
			}
		}
		hdmi_mutex_unlock(&xhdmi->hdmi_mutex);
	}
	/* reschedule this work again in 1 millisecond */
	schedule_delayed_work(&xhdmi->delayed_work_hdcp_poll, msecs_to_jiffies(1));
	return;
}

static int XHdcp_KeyManagerInit(uintptr_t BaseAddress, u8 *Hdcp14Key)
{
	u32 RegValue;
	u8 Row;
	u8 i;
	u8 *KeyPtr;
	u8 Status;

	/* Assign key pointer */
	KeyPtr = Hdcp14Key;

	/* Reset */
	Xil_Out32((BaseAddress + 0x0c), (1<<31));

	// There are 41 rows
	for (Row=0; Row<41; Row++)
	{
		/* Set write enable */
		Xil_Out32((BaseAddress + 0x20), 1);

		/* High data */
		RegValue = 0;
		for (i=0; i<4; i++)
		{
			RegValue <<= 8;
			RegValue |= *KeyPtr;
			KeyPtr++;
		}

		/* Write high data */
		Xil_Out32((BaseAddress + 0x2c), RegValue);

		/* Low data */
		RegValue = 0;
		for (i=0; i<4; i++)
		{
			RegValue <<= 8;
			RegValue |= *KeyPtr;
			KeyPtr++;
		}

		/* Write low data */
		Xil_Out32((BaseAddress + 0x30), RegValue);

		/* Table / Row Address */
		Xil_Out32((BaseAddress + 0x28), Row);

		// Write in progress
		do
		{
			RegValue = Xil_In32(BaseAddress + 0x24);
			RegValue &= 1;
		} while (RegValue != 0);
	}

	// Verify

	/* Re-Assign key pointer */
	KeyPtr = Hdcp14Key;

	/* Default Status */
	Status = XST_SUCCESS;

	/* Start at row 0 */
	Row = 0;

	do
	{
		/* Set read enable */
		Xil_Out32((BaseAddress + 0x20), (1<<1));

		/* Table / Row Address */
		Xil_Out32((BaseAddress + 0x28), Row);

		// Read in progress
		do
		{
			RegValue = Xil_In32(BaseAddress + 0x24);
			RegValue &= 1;
		} while (RegValue != 0);

		/* High data */
		RegValue = 0;
		for (i=0; i<4; i++)
		{
			RegValue <<= 8;
			RegValue |= *KeyPtr;
			KeyPtr++;
		}

		if (RegValue != Xil_In32(BaseAddress + 0x2c))
			Status = XST_FAILURE;

		/* Low data */
		RegValue = 0;
		for (i=0; i<4; i++)
		{
			RegValue <<= 8;
			RegValue |= *KeyPtr;
			KeyPtr++;
		}

		if (RegValue != Xil_In32(BaseAddress + 0x30))
			Status = XST_FAILURE;

		/* Increment row */
		Row++;

	} while ((Row<41) && (Status == XST_SUCCESS));

	if (Status == XST_SUCCESS)
	{
		/* Set read lockout */
		Xil_Out32((BaseAddress + 0x20), (1<<31));

		/* Start AXI-Stream */
		Xil_Out32((BaseAddress + 0x0c), (1));
	}

	return Status;
}


/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static int instance = 0;
/* TX uses [1, 127] and RX uses [128, 254] */
/* The HDCP22 timer uses an additional offset of +64 */
#define TX_DEVICE_ID_BASE 1

/* Local Global table for all sub-core instance(s) configuration settings */
XVtc_Config XVtc_ConfigTable[XPAR_XVTC_NUM_INSTANCES];
XV_HdmiTx_Config XV_HdmiTx_ConfigTable[XPAR_XV_HDMITX_NUM_INSTANCES];

extern XHdcp22_Cipher_Config XHdcp22_Cipher_ConfigTable[];
extern XHdcp22_Rng_Config XHdcp22_Rng_ConfigTable[];
extern XHdcp1x_Config XHdcp1x_ConfigTable[];
extern XTmrCtr_Config XTmrCtr_ConfigTable[];
extern XHdcp22_Tx_Config XHdcp22_Tx_ConfigTable[];

/* Compute the absolute address by adding subsystem base address
   to sub-core offset */
static int xhdmi_drm_subcore_AbsAddr(uintptr_t SubSys_BaseAddr,
									 uintptr_t SubSys_HighAddr,
									 uintptr_t SubCore_Offset,
									 uintptr_t *SubCore_AbsAddr)
{
  int Status;
  uintptr_t absAddr;

  absAddr = SubSys_BaseAddr | SubCore_Offset;
  if((absAddr>=SubSys_BaseAddr) && (absAddr<=SubSys_HighAddr)) {
    *SubCore_AbsAddr = absAddr;
    Status = XST_SUCCESS;
  } else {
    *SubCore_AbsAddr = 0;
    Status = XST_FAILURE;
  }

  return(Status);
}

/* Each sub-core within the subsystem has defined offset read from
   device-tree. */
static int xhdmi_drm_compute_subcore_AbsAddr(XV_HdmiTxSs_Config *config)
{
	int ret;

	/* Subcore: Tx */
	ret = xhdmi_drm_subcore_AbsAddr(config->BaseAddress,
									config->HighAddress,
									config->HdmiTx.AbsAddr,
									&(config->HdmiTx.AbsAddr));
	if (ret != XST_SUCCESS) {
	   return -EFAULT;
	}
	XV_HdmiTx_ConfigTable[instance].BaseAddress = config->HdmiTx.AbsAddr;

	/* Subcore: Vtc */
	ret = xhdmi_drm_subcore_AbsAddr(config->BaseAddress,
									config->HighAddress,
									config->Vtc.AbsAddr,
									&(config->Vtc.AbsAddr));
	if (ret != XST_SUCCESS) {
	   return -EFAULT;
	}
	XVtc_ConfigTable[instance].BaseAddress = config->Vtc.AbsAddr;

	/* Subcore: hdcp1x */
	if (config->Hdcp14.IsPresent) {
	  ret = xhdmi_drm_subcore_AbsAddr(config->BaseAddress,
		  							  config->HighAddress,
									  config->Hdcp14.AbsAddr,
									  &(config->Hdcp14.AbsAddr));
	  if (ret != XST_SUCCESS) {
	     return -EFAULT;
	  }
	  XHdcp1x_ConfigTable[instance].BaseAddress = config->Hdcp14.AbsAddr;
	}

	/* Subcore: hdcp1x timer */
	if (config->HdcpTimer.IsPresent) {
	  ret = xhdmi_drm_subcore_AbsAddr(config->BaseAddress,
	  								  config->HighAddress,
	  								  config->HdcpTimer.AbsAddr,
	  								  &(config->HdcpTimer.AbsAddr));
	  if (ret != XST_SUCCESS) {
	     return -EFAULT;
	  }
	  XTmrCtr_ConfigTable[instance * 2 + 0].BaseAddress = config->HdcpTimer.AbsAddr;
	}

	/* Subcore: hdcp22 */
	if (config->Hdcp22.IsPresent) {
	  ret = xhdmi_drm_subcore_AbsAddr(config->BaseAddress,
	  								  config->HighAddress,
	  								  config->Hdcp22.AbsAddr,
	  								  &(config->Hdcp22.AbsAddr));

	  if (ret != XST_SUCCESS) {
	     return -EFAULT;
	  }
	  XHdcp22_Tx_ConfigTable[instance].BaseAddress = config->Hdcp22.AbsAddr;
	}

	return (ret);
}

/*
*	tx driver sysfs entries
*/
static ssize_t vphy_log_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XVphy *VphyPtr;
	XHdmiphy1 *XGtPhyPtr;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	if (xhdmi->isvphy) {
		VphyPtr = xhdmi->xvphy;
		count = XVphy_LogShow(VphyPtr, buf, PAGE_SIZE);
	} else {
		XGtPhyPtr = xhdmi->xgtphy;
		count = XHdmiphy1_LogShow(XGtPhyPtr, buf, PAGE_SIZE);
	}
	return count;
}

static ssize_t vphy_info_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XVphy *VphyPtr;
	XHdmiphy1 *XGtPhyPtr;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	if (xhdmi->isvphy) {
		VphyPtr = xhdmi->xvphy;
		count = XVphy_HdmiDebugInfo(VphyPtr, 0, XVPHY_CHANNEL_ID_CHA, buf, PAGE_SIZE);
		count += scnprintf(&buf[count], (PAGE_SIZE-count), "Tx Ref Clk: %0d Hz\n",
				XVphy_ClkDetGetRefClkFreqHz(xhdmi->xvphy, XVPHY_DIR_TX));
	} else {
		XGtPhyPtr = xhdmi->xgtphy;
		count = XHdmiphy1_HdmiDebugInfo(XGtPhyPtr, 0, XHDMIPHY1_CHANNEL_ID_CHA, buf, PAGE_SIZE);
		count += scnprintf(&buf[count], (PAGE_SIZE-count), "Tx Ref Clk: %0d Hz\n",
				XHdmiphy1_ClkDetGetRefClkFreqHz(xhdmi->xgtphy, XHDMIPHY1_DIR_TX));
	}
	return count;
}

static ssize_t hdmi_log_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiTxSs *HdmiTxSsPtr;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	count = XV_HdmiTxSs_LogShow(HdmiTxSsPtr, buf, PAGE_SIZE);
	return count;
}

static ssize_t hdmi_info_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiTxSs *HdmiTxSsPtr;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	count = XVidC_ShowStreamInfo(&HdmiTxSsPtr->HdmiTxPtr->Stream.Video, buf, PAGE_SIZE);
	count += XV_HdmiTxSs_ShowInfo(HdmiTxSsPtr, &buf[count], (PAGE_SIZE-count));
	return count;
}

static ssize_t hdcp_log_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiTxSs *HdmiTxSsPtr;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	count = XV_HdmiTxSs_HdcpInfo(HdmiTxSsPtr, buf, PAGE_SIZE);
	return count;
}

static ssize_t hdcp_authenticate_store(struct device *sysfs_dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	long int i;
	XV_HdmiTxSs *HdmiTxSsPtr;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	if (kstrtol(buf, 10, &i)) {
		dev_dbg(xhdmi->dev, "hdcp_authenticate_store() input invalid.\n");
		return count;
	}
	i = !!i;
	xhdmi->hdcp_authenticate = i;
	if (i && XV_HdmiTxSs_HdcpIsReady(HdmiTxSsPtr)) {
		XV_HdmiTxSs_HdcpSetProtocol(HdmiTxSsPtr, XV_HDMITXSS_HDCP_22);
		XV_HdmiTxSs_HdcpAuthRequest(HdmiTxSsPtr);
	}
	return count;
}

static ssize_t hdcp_encrypt_store(struct device *sysfs_dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	long int i;
	XV_HdmiTxSs *HdmiTxSsPtr;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	if (kstrtol(buf, 10, &i)) {
		dev_dbg(xhdmi->dev, "hdcp_encrypt_store() input invalid.\n");
		return count;
	}
	i = !!i;
	xhdmi->hdcp_encrypt = i;
	return count;
}

static ssize_t hdcp_protect_store(struct device *sysfs_dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	long int i;
	XV_HdmiTxSs *HdmiTxSsPtr;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	if (kstrtol(buf, 10, &i)) {
		dev_dbg(xhdmi->dev, "hdcp_protect_store() input invalid.\n");
		return count;
	}
	i = !!i;
	xhdmi->hdcp_protect = i;
	hdcp_protect_content(xhdmi);
	return count;
}

static ssize_t hdcp_debugen_store(struct device *sysfs_dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	long int i;
	XV_HdmiTxSs *HdmiTxSsPtr;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	if (kstrtol(buf, 10, &i)) {
		dev_dbg(xhdmi->dev, "hdcp_debugen_store() input invalid.\n");
		return count;
	}
	i = !!i;
	if (i) {
		/* Enable detail logs for hdcp transactions*/
		XV_HdmiTxSs_HdcpSetInfoDetail(HdmiTxSsPtr, TRUE);
	} else {
		/* Disable detail logs for hdcp transactions*/
		XV_HdmiTxSs_HdcpSetInfoDetail(HdmiTxSsPtr, FALSE);
	}
	return count;
}

static ssize_t hdcp_authenticate_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiTxSs *HdmiTxSsPtr;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	count = scnprintf(buf, PAGE_SIZE, "%d", xhdmi->hdcp_authenticate);
	return count;
}

static ssize_t hdcp_encrypt_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiTxSs *HdmiTxSsPtr;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	count = scnprintf(buf, PAGE_SIZE, "%d", xhdmi->hdcp_encrypt);
	return count;
}

static ssize_t hdcp_protect_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiTxSs *HdmiTxSsPtr;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	count = scnprintf(buf, PAGE_SIZE, "%d", xhdmi->hdcp_protect);
	return count;
}

static ssize_t hdcp_authenticated_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiTxSs *HdmiTxSsPtr;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	count = scnprintf(buf, PAGE_SIZE, "%d", xhdmi->hdcp_authenticated);
	return count;
}

static ssize_t hdcp_encrypted_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiTxSs *HdmiTxSsPtr;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	count = scnprintf(buf, PAGE_SIZE, "%d", xhdmi->hdcp_encrypted);
	return count;
}


/* This function decrypts the HDCP keys, uses aes256.c */
/* Note that the bare-metal implementation deciphers in-place in the cipherbuffer, then after that copies to the plaintext buffer,
 * thus trashing the source.
 *
 * In this implementation, a local buffer is created (aligned to 16Byte boundary), the cipher is first copied to the local buffer,
 * where it is then decrypted in-place and then copied over to target Plain Buffer. This leaves the source buffer intact.
 */
static int Decrypt(const u8 *CipherBufferPtr/*src*/, u8 *PlainBufferPtr/*dst*/, u8 *Key, u16 Length)
{
	u16 i;
	u8 *AesBufferPtr;
	u8 *LocalBuf; //16Byte aligned
	u16 AesLength;
	aes256_context ctx;

	AesLength = Length/16; // The aes always encrypts 16 bytes
	if (Length % 16) {
		AesLength++;
	}

	//Allocate local buffer that is 16Byte aligned
	LocalBuf = kzalloc((size_t)(AesLength*16), GFP_KERNEL);
	if (!LocalBuf) {
		printk(KERN_ERR "%s - %s Unable to allocate memory!\n",
		       __FILE__, __func__);
		return XST_FAILURE;
	}

	// Copy cipher into local buffer
	memcpy(LocalBuf, CipherBufferPtr, (AesLength*16));

	// Assign local Pointer // @NOTE: Changed
	AesBufferPtr = LocalBuf;

	// Initialize AES256
	aes256_init(&ctx, Key);

	for (i=0; i<AesLength; i++)
	{
		// Decrypt
		aes256_decrypt_ecb(&ctx, AesBufferPtr);

		// Increment pointer
		AesBufferPtr += 16;	// The aes always encrypts 16 bytes
	}

	// Done
	aes256_done(&ctx);

	//copy decrypted key into Plainbuffer
	memcpy(PlainBufferPtr, LocalBuf, Length);

	//free local buffer
	kfree(LocalBuf);

	return XST_SUCCESS;
}

#define SIGNATURE_OFFSET			0
#define HDCP22_LC128_OFFSET			16
#define HDCP22_CERTIFICATE_OFFSET	32
#define HDCP14_KEY1_OFFSET			1024
#define HDCP14_KEY2_OFFSET			1536

/* buffer points to the encrypted data (from EEPROM), password points to a 32-character password */
static int XHdcp_LoadKeys(const u8 *Buffer, u8 *Password, u8 *Hdcp22Lc128, u32 Hdcp22Lc128Size, u8 *Hdcp22RxPrivateKey, u32 Hdcp22RxPrivateKeySize,
	u8 *Hdcp14KeyA, u32 Hdcp14KeyASize, u8 *Hdcp14KeyB, u32 Hdcp14KeyBSize)
{
	u8 i;
	const u8 HdcpSignature[16] = { "xilinx_hdcp_keys" };
	u8 Key[32] = {0};
	u8 SignatureOk;
	u8 HdcpSignatureBuffer[16];
	int ret;

	// Generate password hash
	XHdcp22Cmn_Sha256Hash(Password, 32, Key);

	/* decrypt the signature */
	ret = Decrypt(&Buffer[SIGNATURE_OFFSET]/*source*/, HdcpSignatureBuffer/*destination*/, Key, sizeof(HdcpSignature));
	if (ret != XST_SUCCESS)
		return ret;

	SignatureOk = 1;
	for (i = 0; i < sizeof(HdcpSignature); i++) {
		if (HdcpSignature[i] != HdcpSignatureBuffer[i])
			SignatureOk = 0;
	}

	/* password and buffer are correct, as the generated key could correctly decrypt the signature */
	if (SignatureOk == 1) {
		/* decrypt the keys */
		ret = Decrypt(&Buffer[HDCP22_LC128_OFFSET], Hdcp22Lc128, Key, Hdcp22Lc128Size);
		if (ret != XST_SUCCESS)
			return ret;
		ret = Decrypt(&Buffer[HDCP22_CERTIFICATE_OFFSET], Hdcp22RxPrivateKey, Key, Hdcp22RxPrivateKeySize);
		if (ret != XST_SUCCESS)
			return ret;
		ret = Decrypt(&Buffer[HDCP14_KEY1_OFFSET], Hdcp14KeyA, Key, Hdcp14KeyASize);
		if (ret != XST_SUCCESS)
			return ret;
		ret = Decrypt(&Buffer[HDCP14_KEY2_OFFSET], Hdcp14KeyB, Key, Hdcp14KeyBSize);
		if (ret != XST_SUCCESS)
			return ret;

		return XST_SUCCESS;
	} else {
		printk(KERN_INFO "HDCP key store signature mismatch; HDCP key data and/or password are invalid.\n");
	}
	return XST_FAILURE;
}

/* assume the HDCP C structures containing the keys are valid, and sets them in the bare-metal driver / IP */
static int hdcp_keys_configure(struct xlnx_drm_hdmi *xhdmi)
{
	XV_HdmiTxSs *HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;

	if (xhdmi->config.Hdcp14.IsPresent && xhdmi->config.HdcpTimer.IsPresent && xhdmi->hdcp1x_keymngmt_iomem) {
		u8 Status;
		dev_dbg(xhdmi->dev,"HDCP1x components are all there.\n");
		/* Set pointer to HDCP 1.4 key */
		XV_HdmiTxSs_HdcpSetKey(HdmiTxSsPtr, XV_HDMITXSS_KEY_HDCP14, xhdmi->Hdcp14KeyA);
		/* Key manager Init */
		Status = XHdcp_KeyManagerInit((uintptr_t)xhdmi->hdcp1x_keymngmt_iomem, HdmiTxSsPtr->Hdcp14KeyPtr);
		if (Status != XST_SUCCESS) {
			dev_err(xhdmi->dev, "HDCP 1.4 TX Key Manager initialization error.\n");
			return -EINVAL;
		}
		dev_info(xhdmi->dev, "HDCP 1.4 TX Key Manager initialized OK.\n");
	}
	if (xhdmi->config.Hdcp22.IsPresent) {
		/* Set pointer to HDCP 2.2 LC128 */
		XV_HdmiTxSs_HdcpSetKey(HdmiTxSsPtr, XV_HDMITXSS_KEY_HDCP22_LC128, xhdmi->Hdcp22Lc128);
		XV_HdmiTxSs_HdcpSetKey(HdmiTxSsPtr, XV_HDMITXSS_KEY_HDCP22_SRM, (u8 *)&Hdcp22Srm[0]);
	}
	return 0;
}

/* the EEPROM contents (i.e. the encrypted HDCP keys) must be dumped as a binary blob;
 * the user must first upload the password */
static ssize_t hdcp_key_store(struct device *sysfs_dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);
	XV_HdmiTxSs *HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;

	/* check for valid size of HDCP encrypted key binary blob, @TODO adapt */
	if (count < 1872) {
		dev_dbg(xhdmi->dev, "hdcp_key_store(count = %d, expected >=1872)\n", (int)count);
		return -EINVAL;
	}
	xhdmi->hdcp_password_accepted = 0;
	/* decrypt the keys from the binary blob (buffer) into the C structures for keys */
	if (XHdcp_LoadKeys(buf, xhdmi->hdcp_password,
		xhdmi->Hdcp22Lc128, sizeof(xhdmi->Hdcp22Lc128),
		xhdmi->Hdcp22PrivateKey, sizeof(xhdmi->Hdcp22PrivateKey),
		xhdmi->Hdcp14KeyA, sizeof(xhdmi->Hdcp14KeyA),
		xhdmi->Hdcp14KeyB, sizeof(xhdmi->Hdcp14KeyB)) == XST_SUCCESS) {

		xhdmi->hdcp_password_accepted = 1;

		/* configure the keys in the IP */
		hdcp_keys_configure(xhdmi);

		/* configure HDCP in HDMI */
		u8 Status = XV_HdmiTxSs_CfgInitializeHdcp(HdmiTxSsPtr, &xhdmi->config, (uintptr_t)xhdmi->iomem);
		if (Status != XST_SUCCESS) {
			dev_err(xhdmi->dev, "XV_HdmiTxSs_CfgInitializeHdcp() failed with error %d\n", Status);
			return -EINVAL;
		}
		XV_HdmiTxSs_SetCallback(HdmiTxSsPtr, XV_HDMITXSS_HANDLER_HDCP_AUTHENTICATED,
			TxHdcpAuthenticatedCallback, (void *)xhdmi);
		XV_HdmiTxSs_SetCallback(HdmiTxSsPtr, XV_HDMITXSS_HANDLER_HDCP_UNAUTHENTICATED,
			TxHdcpUnauthenticatedCallback, (void *)xhdmi);

		if (xhdmi->config.Hdcp14.IsPresent || xhdmi->config.Hdcp22.IsPresent) {
			/* call into hdcp_poll_work, which will reschedule itself */
			hdcp_poll_work(&xhdmi->delayed_work_hdcp_poll.work);
		}
	}
	return count;
}

static ssize_t hdcp_password_show(struct device *sysfs_dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	XV_HdmiTxSs *HdmiTxSsPtr;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;
	count = scnprintf(buf, PAGE_SIZE, "%s", xhdmi->hdcp_password_accepted? "accepted": "rejected");
	return count;
}

/* store the HDCP key password, after this the HDCP key can be written to sysfs */
static ssize_t hdcp_password_store(struct device *sysfs_dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int i = 0;
	struct xlnx_drm_hdmi *xhdmi = (struct xlnx_drm_hdmi *)dev_get_drvdata(sysfs_dev);

	if (count > sizeof(xhdmi->hdcp_password)) return -EINVAL;
	/* copy password characters up to newline or carriage return */
	while ((i < count) && (i < sizeof(xhdmi->hdcp_password))) {
		/* do not include newline or carriage return in password */
		if ((buf[i] == '\n') || (buf[i] == '\r')) break;
		xhdmi->hdcp_password[i] = buf[i];
		i++;
	}
	/* zero remaining characters */
	while (i < sizeof(xhdmi->hdcp_password)) {
		xhdmi->hdcp_password[i] = 0;
		i++;
	}
	return count;
}

static DEVICE_ATTR(vphy_log,  0444, vphy_log_show, NULL/*store*/);
static DEVICE_ATTR(vphy_info, 0444, vphy_info_show, NULL/*store*/);
static DEVICE_ATTR(hdmi_log,  0444, hdmi_log_show, NULL/*store*/);
static DEVICE_ATTR(hdcp_log,  0444, hdcp_log_show, NULL/*store*/);
static DEVICE_ATTR(hdmi_info, 0444, hdmi_info_show, NULL/*store*/);
static DEVICE_ATTR(hdcp_debugen, 0220, NULL/*show*/, hdcp_debugen_store);
static DEVICE_ATTR(hdcp_key, 0220, NULL/*show*/, hdcp_key_store);
static DEVICE_ATTR(hdcp_password, 0660, hdcp_password_show, hdcp_password_store);

/* readable and writable controls */
DEVICE_ATTR(hdcp_authenticate, 0664, hdcp_authenticate_show, hdcp_authenticate_store);
DEVICE_ATTR(hdcp_encrypt, 0664, hdcp_encrypt_show, hdcp_encrypt_store);
DEVICE_ATTR(hdcp_protect, 0664, hdcp_protect_show, hdcp_protect_store);
/* read-only status */
DEVICE_ATTR(hdcp_authenticated, 0444, hdcp_authenticated_show, NULL/*store*/);
DEVICE_ATTR(hdcp_encrypted, 0444, hdcp_encrypted_show, NULL/*store*/);

static struct attribute *attrs[] = {
	&dev_attr_vphy_log.attr,
	&dev_attr_vphy_info.attr,
	&dev_attr_hdmi_log.attr,
	&dev_attr_hdcp_log.attr,
	&dev_attr_hdmi_info.attr,
	&dev_attr_hdcp_debugen.attr,
	&dev_attr_hdcp_key.attr,
	&dev_attr_hdcp_password.attr,
	&dev_attr_hdcp_authenticate.attr,
	&dev_attr_hdcp_encrypt.attr,
	&dev_attr_hdcp_protect.attr,
	&dev_attr_hdcp_authenticated.attr,
	&dev_attr_hdcp_encrypted.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static int xlnx_drm_hdmi_create_connector(struct drm_encoder *encoder)
{
	struct xlnx_drm_hdmi *xhdmi = encoder_to_hdmi(encoder);
	struct drm_connector *connector = &xhdmi->connector;
	int ret;

	connector->polled = DRM_CONNECTOR_POLL_HPD;
	connector->interlace_allowed = true;

	ret = drm_connector_init(encoder->dev, connector,
				 &xlnx_drm_hdmi_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		dev_err(xhdmi->dev, "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &xlnx_drm_hdmi_connector_helper_funcs);
	ret = drm_connector_register(connector);
	if (ret) {
		dev_err(xhdmi->dev, "Failed to register the connector (ret=%d)\n", ret);
		return ret;
	}
	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret) {
		dev_err(xhdmi->dev,
			"Failed to attach encoder to connector (ret=%d)\n", ret);
		return ret;
	}

	drm_object_attach_property(&connector->base,
			connector->dev->mode_config.gen_hdr_output_metadata_property,
			0);

	return 0;
}

static int xlnx_drm_hdmi_bind(struct device *dev, struct device *master,
			void *data)
{
	struct xlnx_drm_hdmi *xhdmi = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &xhdmi->encoder;
	struct drm_device *drm_dev = data;
	int ret;

	/* NOTE - "xlnx-drm" is the platform driver
	 * "xlnx" is drm driver (Xilinx DRM KMS Driver)
	 * In above case - drm_dev->driver->name = xlnx
	 */

	/*
	 * TODO: The possible CRTCs are 1 now as per current implementation of
	 * HDMI tx driver. DRM framework can support more than one CRTCs and
	 * HDMI driver can be enhanced for that.
	 */
	encoder->possible_crtcs = 1;

	/* initialize encoder */
	drm_encoder_init(drm_dev, encoder, &xlnx_drm_hdmi_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);
	drm_encoder_helper_add(encoder, &xlnx_drm_hdmi_encoder_helper_funcs);

	/* create connector */
	ret = xlnx_drm_hdmi_create_connector(encoder);
	if (ret) {
		dev_err(xhdmi->dev, "failed creating connector, ret = %d\n", ret);
		drm_encoder_cleanup(encoder);
	}
	return ret;
}

static void xlnx_drm_hdmi_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct xlnx_drm_hdmi *xhdmi = dev_get_drvdata(dev);

	xlnx_drm_hdmi_encoder_dpms(&xhdmi->encoder, DRM_MODE_DPMS_OFF);
	drm_encoder_cleanup(&xhdmi->encoder);
	drm_connector_cleanup(&xhdmi->connector);
}

static const struct component_ops xlnx_drm_hdmi_component_ops = {
	.bind	= xlnx_drm_hdmi_bind,
	.unbind	= xlnx_drm_hdmi_unbind
};

static void xlnx_drm_hdmi_initialize(struct xlnx_drm_hdmi *xhdmi)
{
	unsigned long flags;
	XV_HdmiTxSs *HdmiTxSsPtr;
	u32 Status;
	int ret;

	dev_dbg(xhdmi->dev, "%s\n", __func__);

	hdmi_mutex_lock(&xhdmi->hdmi_mutex);
	HdmiTxSsPtr = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;

	Status = XV_HdmiTxSs_CfgInitialize(HdmiTxSsPtr, &xhdmi->config, (uintptr_t)xhdmi->iomem);
	if (Status != XST_SUCCESS) {
		dev_err(xhdmi->dev, "initialization failed with error %d\n", Status);
	}

	spin_lock_irqsave(&xhdmi->irq_lock, flags);
	XV_HdmiTxSs_IntrDisable(HdmiTxSsPtr);
	spin_unlock_irqrestore(&xhdmi->irq_lock, flags);

	/* TX SS callback setup */
	XV_HdmiTxSs_SetCallback(HdmiTxSsPtr, XV_HDMITXSS_HANDLER_CONNECT,
		TxConnectCallback, (void *)xhdmi);
	XV_HdmiTxSs_SetCallback(HdmiTxSsPtr, XV_HDMITXSS_HANDLER_TOGGLE,
		TxToggleCallback, (void *)xhdmi);
	XV_HdmiTxSs_SetCallback(HdmiTxSsPtr, XV_HDMITXSS_HANDLER_STREAM_UP,
		TxStreamUpCallback, (void *)xhdmi);
	XV_HdmiTxSs_SetCallback(HdmiTxSsPtr, XV_HDMITXSS_HANDLER_STREAM_DOWN,
		TxStreamDownCallback, (void *)xhdmi);
	XV_HdmiTxSs_SetCallback(HdmiTxSsPtr, XV_HDMITXSS_HANDLER_VS,
		TxVsCallback, (void *)xhdmi);
	XV_HdmiTxSs_SetCallback(HdmiTxSsPtr, XV_HDMITXSS_HANDLER_BRDGUNLOCK,
		TxBrdgUnlockedCallback, (void *)xhdmi);
	XV_HdmiTxSs_SetCallback(HdmiTxSsPtr, XV_HDMITXSS_HANDLER_BRDGOVERFLOW,
		TxBrdgOverflowCallback, (void *)xhdmi);
	XV_HdmiTxSs_SetCallback(HdmiTxSsPtr, XV_HDMITXSS_HANDLER_BRDGUNDERFLOW,
		TxBrdgUnderflowCallback, (void *)xhdmi);

	/* get a reference to the XVphy data structure */
	if (xhdmi->isvphy)
		xhdmi->xvphy = xvphy_get_xvphy(xhdmi->phy[0]);
	else
		xhdmi->xgtphy = xvphy_get_xvphy(xhdmi->phy[0]);

	xvphy_mutex_lock(xhdmi->phy[0]);
	/* the callback is not specific to a single lane, but we need to
	 * provide one of the phys as reference */

	if (xhdmi->isvphy) {
		XVphy_SetHdmiCallback(xhdmi->xvphy, XVPHY_HDMI_HANDLER_TXINIT,
			VphyHdmiTxInitCallback, (void *)xhdmi);
		XVphy_SetHdmiCallback(xhdmi->xvphy, XVPHY_HDMI_HANDLER_TXREADY,
			VphyHdmiTxReadyCallback, (void *)xhdmi);
	} else {
		XHdmiphy1_SetHdmiCallback(xhdmi->xgtphy, XHDMIPHY1_HDMI_HANDLER_TXINIT,
			VphyHdmiTxInitCallback, (void *)xhdmi);
		XHdmiphy1_SetHdmiCallback(xhdmi->xgtphy, XHDMIPHY1_HDMI_HANDLER_TXREADY,
			VphyHdmiTxReadyCallback, (void *)xhdmi);
	}
	xvphy_mutex_unlock(xhdmi->phy[0]);

	/* Request the interrupt */
	ret = devm_request_threaded_irq(xhdmi->dev, xhdmi->irq, hdmitx_irq_handler, hdmitx_irq_thread,
		IRQF_TRIGGER_HIGH /*| IRQF_SHARED*/, "xilinx-hdmitxss", xhdmi/*dev_id*/);
	if (ret) {
		dev_err(xhdmi->dev, "unable to request IRQ %d\n", xhdmi->irq);
	}

	/* HDCP 1.4 Cipher interrupt */
	if (xhdmi->hdcp1x_irq > 0) {
		ret = devm_request_threaded_irq(xhdmi->dev, xhdmi->hdcp1x_irq, hdmitx_hdcp_irq_handler, hdmitx_hdcp_irq_thread,
			IRQF_TRIGGER_HIGH /*| IRQF_SHARED*/, "xilinx-hdmitxss-hdcp1x-cipher", xhdmi/*dev_id*/);
		if (ret) {
			dev_err(xhdmi->dev, "unable to request IRQ %d\n", xhdmi->hdcp1x_irq);
		}
	}

	/* HDCP 1.4 Timer interrupt */
	if (xhdmi->hdcp1x_timer_irq > 0) {
		ret = devm_request_threaded_irq(xhdmi->dev, xhdmi->hdcp1x_timer_irq, hdmitx_hdcp_irq_handler, hdmitx_hdcp_irq_thread,
			IRQF_TRIGGER_HIGH /*| IRQF_SHARED*/, "xilinx-hdmitxss-hdcp1x-timer", xhdmi/*dev_id*/);
		if (ret) {
			dev_err(xhdmi->dev, "unable to request IRQ %d\n", xhdmi->hdcp1x_timer_irq);
		}
	}

	/* HDCP 2.2 Timer interrupt */
	if (xhdmi->hdcp22_timer_irq > 0) {
		ret = devm_request_threaded_irq(xhdmi->dev, xhdmi->hdcp22_timer_irq, hdmitx_hdcp_irq_handler, hdmitx_hdcp_irq_thread,
			IRQF_TRIGGER_HIGH /*| IRQF_SHARED*/, "xilinx-hdmitxss-hdcp22-timer", xhdmi/*dev_id*/);
		if (ret) {
			dev_err(xhdmi->dev, "unable to request IRQ %d\n", xhdmi->hdcp22_timer_irq);
		}
	}
	hdmi_mutex_unlock(&xhdmi->hdmi_mutex);

	spin_lock_irqsave(&xhdmi->irq_lock, flags);
	XV_HdmiTxSs_IntrEnable(HdmiTxSsPtr);
	spin_unlock_irqrestore(&xhdmi->irq_lock, flags);
}

static int xlnx_drm_hdmi_parse_of(struct xlnx_drm_hdmi *xhdmi, XV_HdmiTxSs_Config *config)
{
	struct device *dev = xhdmi->dev;
	struct device_node *node = dev->of_node;
	int rc;
	u32 val = 0;
	bool isHdcp14_en, isHdcp22_en;

	rc = of_property_read_u32(node, "xlnx,input-pixels-per-clock", &val);
	if (rc < 0)
		goto error_dt;
	config->Ppc = val;

	rc = of_property_read_u32(node, "xlnx,max-bits-per-component", &val);
	if (rc < 0)
		goto error_dt;
	config->MaxBitsPerPixel = val;


	/* Tx Core */
	config->HdmiTx.DeviceId = TX_DEVICE_ID_BASE + instance;
	config->HdmiTx.IsPresent = 1;
	config->HdmiTx.AbsAddr = TXSS_TX_OFFSET;
	XV_HdmiTx_ConfigTable[instance].DeviceId = TX_DEVICE_ID_BASE + instance;
	XV_HdmiTx_ConfigTable[instance].BaseAddress = TXSS_TX_OFFSET;
	/*VTC Core */
	config->Vtc.IsPresent = 1;
	config->Vtc.DeviceId = TX_DEVICE_ID_BASE + instance;
	config->Vtc.AbsAddr = TXSS_VTC_OFFSET;
	XVtc_ConfigTable[instance].DeviceId = config->Vtc.DeviceId;
	XVtc_ConfigTable[instance].BaseAddress = TXSS_VTC_OFFSET;

	isHdcp14_en = of_property_read_bool(node, "xlnx,include-hdcp-1-4");
	isHdcp22_en = of_property_read_bool(node, "xlnx,include-hdcp-2-2");
	xhdmi->audio_enabled =
		of_property_read_bool(node, "xlnx,audio-enabled");

	if (isHdcp14_en) {
		/* HDCP14 Core */
		/* make subcomponent of TXSS present */
		config->Hdcp14.IsPresent = 1;
		config->Hdcp14.DeviceId = TX_DEVICE_ID_BASE + instance;
		config->Hdcp14.AbsAddr = TXSS_HDCP14_OFFSET;
		XHdcp1x_ConfigTable[instance].DeviceId = config->Hdcp14.DeviceId;
		XHdcp1x_ConfigTable[instance].BaseAddress = TXSS_HDCP14_OFFSET;
		XHdcp1x_ConfigTable[instance].IsRx = 0;
		XHdcp1x_ConfigTable[instance].IsHDMI = 1;

		/* HDCP14 Timer Core */
		/* make subcomponent of TXSS present */
		config->HdcpTimer.DeviceId = TX_DEVICE_ID_BASE + instance;
		config->HdcpTimer.IsPresent = 1;
		config->HdcpTimer.AbsAddr = TXSS_HDCP14_TIMER_OFFSET;

		/* and configure it */
		XTmrCtr_ConfigTable[instance * 2 + 0].DeviceId = config->HdcpTimer.DeviceId;
		XTmrCtr_ConfigTable[instance * 2 + 0].BaseAddress = TXSS_HDCP14_TIMER_OFFSET;
		/* @TODO increment timer index */
	}

	if (isHdcp22_en) {
		/* HDCP22 SS */
		config->Hdcp22.DeviceId = TX_DEVICE_ID_BASE + instance;
		config->Hdcp22.IsPresent = 1;
		config->Hdcp22.AbsAddr = TXSS_HDCP22_OFFSET;
		XHdcp22_Tx_ConfigTable[instance].DeviceId = config->Hdcp22.DeviceId;
		XHdcp22_Tx_ConfigTable[instance].BaseAddress = TXSS_HDCP22_OFFSET;
		XHdcp22_Tx_ConfigTable[instance].Protocol = 0; //HDCP22_TX_HDMI
		XHdcp22_Tx_ConfigTable[instance].Mode = 0; //XHDCP22_TX_TRANSMITTER
		XHdcp22_Tx_ConfigTable[instance].TimerDeviceId = TX_DEVICE_ID_BASE + 64 + instance;
		XHdcp22_Tx_ConfigTable[instance].CipherId = TX_DEVICE_ID_BASE + instance;
		XHdcp22_Tx_ConfigTable[instance].RngId = TX_DEVICE_ID_BASE + instance;

		/* HDCP22 Cipher Core */
		XHdcp22_Cipher_ConfigTable[instance].DeviceId = TX_DEVICE_ID_BASE + instance;
		XHdcp22_Cipher_ConfigTable[instance].BaseAddress = TX_HDCP22_CIPHER_OFFSET;
		/* HDCP22-Timer Core */
		XTmrCtr_ConfigTable[instance * 2 + 1].DeviceId = TX_DEVICE_ID_BASE + 64 + instance;
		XTmrCtr_ConfigTable[instance * 2 + 1].BaseAddress = TX_HDCP22_TIMER_OFFSET;
		/* HDCP22 RNG Core */
		XHdcp22_Rng_ConfigTable[instance].DeviceId = TX_DEVICE_ID_BASE + instance;
		XHdcp22_Rng_ConfigTable[instance].BaseAddress = TX_HDCP22_RNG_OFFSET;
	}

	if (isHdcp14_en || isHdcp22_en) {
		rc = of_property_read_u32(node, "xlnx,hdcp-authenticate", &val);
		if (rc == 0) {
			xhdmi->hdcp_authenticate = val;
		}
		rc = of_property_read_u32(node, "xlnx,hdcp-encrypt", &val);
		if (rc == 0) {
			xhdmi->hdcp_encrypt = val;
		}
	} else {
		xhdmi->hdcp_authenticate = 0;
		xhdmi->hdcp_encrypt = 0;
	}
	// set default color format to RGB
	xhdmi->xvidc_colorfmt = XVIDC_CSF_RGB;

	if (xhdmi->audio_enabled) {
		xhdmi->tx_audio_data->acr_base = hdmitx_parse_aud_dt(dev);
		if (!xhdmi->tx_audio_data->acr_base) {
			xhdmi->audio_init = false;
			dev_err(dev, "tx audio: acr base parse failed\n");
		}
	} else {
		dev_info(dev, "hdmi tx audio disabled in DT\n");
	}
	return 0;

error_dt:
	dev_err(xhdmi->dev, "Error parsing device tree");
	return rc;
}

static int xlnx_drm_hdmi_probe(struct platform_device *pdev)
{
	struct xlnx_drm_hdmi *xhdmi;
	int ret;
	unsigned int index;
	struct resource *res;
	unsigned long axi_clk_rate;

	dev_info(&pdev->dev, "probe started\n");
	/* allocate zeroed HDMI TX device structure */
	xhdmi = devm_kzalloc(&pdev->dev, sizeof(*xhdmi), GFP_KERNEL);
	if (!xhdmi)
		return -ENOMEM;

	xhdmi->tx_audio_data =
		devm_kzalloc(&pdev->dev, sizeof(struct xlnx_hdmitx_audio_data),
			     GFP_KERNEL);
	if (!xhdmi->tx_audio_data)
		return -ENOMEM;

	/* store pointer of the real device inside platform device */
	xhdmi->dev = &pdev->dev;

	/* get ownership of the HDMI TXSS MMIO register space resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "unable to get register space resource!\n");
		return -EINVAL;
	}
	/* map the MMIO region */
	xhdmi->iomem = devm_ioremap_resource(xhdmi->dev, res);
	if (IS_ERR(xhdmi->iomem)) {
		dev_err(xhdmi->dev, "failed to remap io region\n");
		return PTR_ERR(xhdmi->iomem);
	}
	/* mutex that protects against concurrent access */
	mutex_init(&xhdmi->hdmi_mutex);
	spin_lock_init(&xhdmi->irq_lock);

	init_waitqueue_head(&xhdmi->wait_event);

	dev_dbg(xhdmi->dev,"DT parse start\n");
	/* parse open firmware device tree data */
	ret = xlnx_drm_hdmi_parse_of(xhdmi, &xhdmi->config);
	if (ret < 0)
		return ret;
	dev_dbg(xhdmi->dev,"DT parse done\n");

	/* acquire vphy lanes */
	for (index = 0; index < 3; index++)
	{
		static const struct of_device_id xlnx_hdmi_phy_id_table[] = {
			{ .compatible = "xlnx,hdmi-gt-controller-1.0", },
			{ .compatible = "xlnx,vid-phy-controller-2.2", },
			{ /* end of table */ },
		};
		const struct of_device_id *match;

		char phy_name[16];
		snprintf(phy_name, sizeof(phy_name), "hdmi-phy%d", index);
		xhdmi->phy[index] = devm_phy_get(xhdmi->dev, phy_name);
		if (IS_ERR(xhdmi->phy[index])) {
			ret = PTR_ERR(xhdmi->phy[index]);
			xhdmi->phy[index] = NULL;
			if (ret == -EPROBE_DEFER) {
				dev_info(xhdmi->dev, "xvphy/xgtphy not ready -EPROBE_DEFER\n");
				return ret;
			}
			if (ret != -EPROBE_DEFER)
				dev_err(xhdmi->dev, "failed to get phy lane %s index %d, error %d\n",
					phy_name, index, ret);
			goto error_phy;
		}

		match = of_match_node(xlnx_hdmi_phy_id_table, xhdmi->phy[index]->dev.parent->of_node);
		if (!match) {
			dev_err(xhdmi->dev, "of_match_node failed for phy!\n");
			goto error_phy;
		}

		if (strncmp(match->compatible, "xlnx,vid-phy-controller", 23) == 0)
			xhdmi->isvphy = 1;
		else
			xhdmi->isvphy = 0;

		ret = phy_init(xhdmi->phy[index]);
		if (ret) {
			dev_err(xhdmi->dev,
				"failed to init phy lane %d\n", index);
			goto error_phy;
		}
	}

	xhdmi->config.DeviceId = instance;
	xhdmi->config.BaseAddress = (uintptr_t)xhdmi->iomem;
	xhdmi->config.HighAddress = (uintptr_t)xhdmi->iomem + resource_size(res) - 1;

	/* Compute sub-core AbsAddres */
	ret = xhdmi_drm_compute_subcore_AbsAddr(&xhdmi->config);
	if (ret == -EFAULT) {
	   dev_err(xhdmi->dev, "hdmi-tx sub-core address out-of range\n");
	   return ret;
	}

	/* 4 clock sources to be acquired and enabled */
	/* acquire video streaming bus clock */
	xhdmi->clk = devm_clk_get(xhdmi->dev, "s_axis_video_aclk");
	if (IS_ERR(xhdmi->clk)) {
		ret = PTR_ERR(xhdmi->clk);
		if (ret == -EPROBE_DEFER)
			dev_info(xhdmi->dev, "video-clk not ready -EPROBE_DEFER\n");
		if (ret != -EPROBE_DEFER)
			dev_err(xhdmi->dev, "failed to get video clk\n");
		return ret;
	}

	ret = clk_prepare_enable(xhdmi->clk);
	if (ret) {
		dev_err(xhdmi->dev, "failed to prep and enable axis video clk!\n");
		return ret;
	}

	/* acquire axi-lite register bus clock */
	xhdmi->axi_lite_clk = devm_clk_get(xhdmi->dev, "s_axi_cpu_aclk");
	if (IS_ERR(xhdmi->axi_lite_clk)) {
		ret = PTR_ERR(xhdmi->axi_lite_clk);
		if (ret == -EPROBE_DEFER)
			dev_info(xhdmi->dev, "axi-lite-clk not ready -EPROBE_DEFER\n");
		if (ret != -EPROBE_DEFER)
			dev_err(xhdmi->dev, "failed to get axi-lite clk\n");
		return ret;
	}

	ret = clk_prepare_enable(xhdmi->axi_lite_clk);
	if (ret) {
		dev_err(xhdmi->dev, "failed to prep and enable axilite clk!\n");
		return ret;
	}

	axi_clk_rate = clk_get_rate(xhdmi->axi_lite_clk);
	dev_dbg(xhdmi->dev,"axi_clk_rate = %lu Hz\n", axi_clk_rate);
	xhdmi->config.AxiLiteClkFreq = axi_clk_rate;

	/* we now know the AXI clock rate */
	XHdcp1x_ConfigTable[instance].SysFrequency = axi_clk_rate;
	XTmrCtr_ConfigTable[instance * 2 + 0].SysClockFreqHz = axi_clk_rate;
	XTmrCtr_ConfigTable[instance * 2 + 1].SysClockFreqHz = axi_clk_rate;
	XV_HdmiTx_ConfigTable[instance].AxiLiteClkFreq = axi_clk_rate;

	/* acquire Tmds clock for output resolution */
	xhdmi->tmds_clk = devm_clk_get(&pdev->dev, "txref-clk");
	if (IS_ERR(xhdmi->tmds_clk)) {
		ret = PTR_ERR(xhdmi->tmds_clk);
		xhdmi->tmds_clk = NULL;
		if (ret == -EPROBE_DEFER)
			dev_info(xhdmi->dev, "tx-clk not ready -EPROBE_DEFER\n");
		if (ret != -EPROBE_DEFER)
			dev_err(xhdmi->dev, "failed to get tx-clk.\n");
		return ret;
	}
	dev_dbg(xhdmi->dev, "got txref-clk (default rate = %lu)\n", clk_get_rate(xhdmi->tmds_clk));
	ret = clk_prepare_enable(xhdmi->tmds_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable tx clk\n");
		return ret;
	}

	xhdmi->tx_audio_data->tmds_clk = clk_get_rate(xhdmi->tmds_clk);
	/* support to drive an external retimer IC on the TX path, depending on TX clock line rate */
	xhdmi->retimer_clk = devm_clk_get(&pdev->dev, "retimer-clk");
	if (IS_ERR(xhdmi->retimer_clk)) {
		ret = PTR_ERR(xhdmi->retimer_clk);
		xhdmi->retimer_clk = NULL;
		if (ret == -EPROBE_DEFER)
			dev_info(xhdmi->dev, "retimer-clk not ready -EPROBE_DEFER\n");
		if (ret != -EPROBE_DEFER)
			dev_err(xhdmi->dev, "Did not find a retimer-clk, not driving an external retimer device driver.\n");
		return ret;
	} else if (xhdmi->retimer_clk) {
		dev_dbg(xhdmi->dev,"got retimer-clk\n");
		ret = clk_prepare_enable(xhdmi->retimer_clk);
		if (ret) {
			dev_err(xhdmi->dev, "failed to enable retimer-clk\n");
			return ret;
		}
		dev_dbg(xhdmi->dev,"prepared and enabled retimer-clk\n");
	} else {
		dev_dbg(xhdmi->dev,"no retimer clk specified, assuming no redriver/retimer is used.\n");
	}

	/* get ownership of the HDCP1x key management MMIO register space resource */
	if (xhdmi->config.Hdcp14.IsPresent) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hdcp1x-keymngmt");

		if (res) {
			dev_dbg(xhdmi->dev,"Mapping HDCP1x key management block.\n");
			xhdmi->hdcp1x_keymngmt_iomem = devm_ioremap_resource(xhdmi->dev, res);
			dev_dbg(xhdmi->dev,"HDCP1x key management block @%p.\n", xhdmi->hdcp1x_keymngmt_iomem);
			if (IS_ERR(xhdmi->hdcp1x_keymngmt_iomem)) {
				dev_dbg(xhdmi->dev,"Could not ioremap hdcp1x-keymngmt.\n");
				return PTR_ERR(xhdmi->hdcp1x_keymngmt_iomem);
			}
		}
	}

	/* get HDMI TXSS irq */
	xhdmi->irq = platform_get_irq(pdev, 0);
	if (xhdmi->irq <= 0) {
		dev_err(xhdmi->dev, "platform_get_irq() failed\n");
		return xhdmi->irq;
	}

	if (xhdmi->config.Hdcp14.IsPresent) {
	  xhdmi->hdcp1x_irq = platform_get_irq_byname(pdev, "hdcp14_irq");
	  dev_dbg(xhdmi->dev,"xhdmi->hdcp1x_irq = %d\n", xhdmi->hdcp1x_irq);
	  xhdmi->hdcp1x_timer_irq = platform_get_irq_byname(pdev, "hdcp14_timer_irq");
	  dev_dbg(xhdmi->dev,"xhdmi->hdcp1x_timer_irq = %d\n", xhdmi->hdcp1x_timer_irq);
	}

	if (xhdmi->config.Hdcp22.IsPresent) {
	  xhdmi->hdcp22_irq = platform_get_irq_byname(pdev, "hdcp22_irq");
	  dev_dbg(xhdmi->dev,"xhdmi->hdcp22_irq = %d\n", xhdmi->hdcp22_irq);
	  xhdmi->hdcp22_timer_irq = platform_get_irq_byname(pdev, "hdcp22_timer_irq");
	  dev_dbg(xhdmi->dev,"xhdmi->hdcp22_timer_irq = %d\n", xhdmi->hdcp22_timer_irq);
	}

	if (xhdmi->config.Hdcp14.IsPresent || xhdmi->config.Hdcp22.IsPresent) {
		INIT_DELAYED_WORK(&xhdmi->delayed_work_hdcp_poll, hdcp_poll_work/*function*/);
	}

	/* create sysfs group */
	ret = sysfs_create_group(&xhdmi->dev->kobj, &attr_group);
	if (ret) {
		dev_err(xhdmi->dev, "sysfs group creation (%d) failed \n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, xhdmi);

	/* initialize hw */
	xlnx_drm_hdmi_initialize(xhdmi);

	/* probe has succeeded for this instance, increment instance index */
	instance++;

	if (xhdmi->audio_enabled && xhdmi->tx_audio_data->acr_base) {
		xhdmi->audio_pdev = hdmitx_register_aud_dev(xhdmi->dev);
		if (IS_ERR(xhdmi->audio_pdev)) {
			xhdmi->audio_init = false;
			dev_err(xhdmi->dev, "hdmi tx audio init failed\n");
		} else {
			xhdmi->audio_init = true;
			dev_info(xhdmi->dev, "hdmi tx audio initialized\n");
		}
	}
	dev_info(xhdmi->dev, "probe successful\n");
	return component_add(xhdmi->dev, &xlnx_drm_hdmi_component_ops);

error_phy:
	dev_info(xhdmi->dev, "probe failed:: error_phy:\n");
	index = 0;
	/* release the lanes that we did get, if we did not get all lanes */
	if (xhdmi->phy[index]) {
		printk(KERN_INFO "phy_exit() xhdmi->phy[%d] = %p\n", index, xhdmi->phy[index]);
		phy_exit(xhdmi->phy[index]);
		xhdmi->phy[index] = NULL;
	}

	return ret;
}

static int xlnx_drm_hdmi_remove(struct platform_device *pdev)
{
	struct xlnx_drm_hdmi *xhdmi = platform_get_drvdata(pdev);

	if (xhdmi->audio_init)
		platform_device_unregister(xhdmi->audio_pdev);
	sysfs_remove_group(&pdev->dev.kobj, &attr_group);
	component_del(&pdev->dev, &xlnx_drm_hdmi_component_ops);
	return 0;
}

struct xlnx_hdmitx_audio_data *hdmitx_get_audio_data(struct device *dev)
{
	struct xlnx_drm_hdmi *xhdmi = dev_get_drvdata(dev);

	if (!xhdmi)
		return NULL;
	else
		return xhdmi->tx_audio_data;
}

void hdmitx_audio_startup(struct device *dev)
{
	struct xlnx_drm_hdmi *xhdmi = dev_get_drvdata(dev);
	XV_HdmiTxSs *xv_hdmitxss = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;

	hdmi_mutex_lock(&xhdmi->hdmi_mutex);
	XV_HdmiTxSs_AudioMute(xv_hdmitxss, 0);
	hdmi_mutex_unlock(&xhdmi->hdmi_mutex);
}

void hdmitx_audio_hw_params(struct device *dev,
		struct hdmi_audio_infoframe *frame)
{
	struct xlnx_drm_hdmi *xhdmi = dev_get_drvdata(dev);
	XV_HdmiTxSs *xv_hdmitxss = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;

	hdmi_mutex_lock(&xhdmi->hdmi_mutex);
	XV_HdmiTxSs_SetAudioChannels(xv_hdmitxss, frame->channels);
	XV_HdmiTxSs_AudioMute(xv_hdmitxss, 0);
	hdmi_mutex_unlock(&xhdmi->hdmi_mutex);

}

void hdmitx_audio_shutdown(struct device *dev)
{
	struct xlnx_drm_hdmi *xhdmi = dev_get_drvdata(dev);
	XV_HdmiTxSs *xv_hdmitxss = (XV_HdmiTxSs *)&xhdmi->xv_hdmitxss;

	hdmi_mutex_lock(&xhdmi->hdmi_mutex);
	XV_HdmiTxSs_AudioMute(xv_hdmitxss, 1);
	hdmi_mutex_unlock(&xhdmi->hdmi_mutex);
}

void hdmitx_audio_mute(struct device *dev, bool enable)
{
	if (enable)
		hdmitx_audio_shutdown(dev);
	else
		hdmitx_audio_startup(dev);
}

int hdmitx_audio_geteld(struct device *dev, uint8_t *buf, size_t len)
{
	struct xlnx_drm_hdmi *xhdmi = dev_get_drvdata(dev);
	size_t size;

	if (xhdmi->have_edid) {
		size = drm_eld_size(xhdmi->connector.eld);
		if (size != 0) {
			if (len < size)
				size = len;
			memcpy(buf, xhdmi->connector.eld, size);
			return 0;
		} else {
			return -EINVAL;
		}
	}

	return -EIO;
}

static const struct dev_pm_ops xhdmitx_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(hdmitx_pm_suspend, hdmitx_pm_resume)
};

static const struct of_device_id xlnx_drm_hdmi_of_match[] = {
	{ .compatible = "xlnx,v-hdmi-tx-ss-3.1", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xlnx_drm_hdmi_of_match);

static struct platform_driver xlnx_drm_hdmi_driver = {
	.probe			= xlnx_drm_hdmi_probe,
	.remove			= xlnx_drm_hdmi_remove,
	.driver			= {
		.owner		= THIS_MODULE,
		.name		= "xlnx-drm-hdmi",
		.pm			= &xhdmitx_pm_ops,
		.of_match_table	= xlnx_drm_hdmi_of_match,
	},
};


module_platform_driver(xlnx_drm_hdmi_driver);

MODULE_AUTHOR("rohit consul <rohitco@xilinx.com>");
MODULE_DESCRIPTION("Xilinx DRM KMS HDMI Driver");
MODULE_LICENSE("GPL v2");
