/*
 * Xilinx VPHY driver
 *
 * The Video Phy is a high-level wrapper around the GT to configure it
 * for video applications. The driver also provides common functionality
 * for its tightly-bound video protocol drivers such as HDMI RX/TX.
 *
 * Copyright (C) 2016, 2017 Leon Woestenberg <leon@sidebranch.com>
 * Copyright (C) 2014, 2015, 2017 Xilinx, Inc.
 *
 * Authors: Leon Woestenberg <leon@sidebranch.com>
 *          Rohit Consul <rohitco@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <dt-bindings/phy/phy.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include "linux/phy/phy-vphy.h"

/* baseline driver includes */
#include "phy-xilinx-vphy/xvphy.h"
#include "phy-xilinx-vphy/xvphy_i.h"
#include "phy-xilinx-vphy/xhdmiphy1.h"
#include "phy-xilinx-vphy/xhdmiphy1_i.h"

/* common RX/TX */
#include "phy-xilinx-vphy/xdebug.h"
#include "phy-xilinx-vphy/xvidc.h"
#include "phy-xilinx-vphy/xtmrctr.h"
#include "phy-xilinx-vphy/xhdcp1x.h"
#include "phy-xilinx-vphy/xhdcp22_rx.h"
#include "phy-xilinx-vphy/xhdcp22_tx.h"
#include "phy-xilinx-vphy/bigdigits.h"
#include "phy-xilinx-vphy/xhdcp22_cipher.h"
#include "phy-xilinx-vphy/xhdcp22_mmult.h"
#include "phy-xilinx-vphy/xhdcp22_rng.h"
#include "phy-xilinx-vphy/xhdcp22_common.h"
#include "phy-xilinx-vphy/aes256.h"
#include "phy-xilinx-vphy/xv_hdmic.h"
#include "phy-xilinx-vphy/xv_hdmic_vsif.h"

#define XVPHY_DRU_REF_CLK_HZ	156250000
/* TODO - [Versal] - This needs to be changed for versal */
#define XHDMIPHY1_DRU_REF_CLK_HZ	200000000

#define hdmi_mutex_lock(x) mutex_lock(x)
#define hdmi_mutex_unlock(x) mutex_unlock(x)

/**
 * struct xvphy_lane - representation of a lane
 * @phy: pointer to the kernel PHY device
 *
 * @type: controller which uses this lane
 * @lane: lane number
 * @protocol: protocol in which the lane operates
 * @ref_clk: enum of allowed ref clock rates for this lane PLL
 * @pll_lock: PLL status
 * @data: pointer to hold private data
 * @direction: 0=rx, 1=tx
 * @share_laneclk: lane number of the clock to be shared
 */
struct xvphy_lane {
	struct phy *phy;
	u8 type;
	u8 lane;
	u8 protocol;
	bool pll_lock;
	/* data is pointer to parent xvphy_dev */
	void *data;
	bool direction_tx;
	u32 share_laneclk;
};

/**
 * struct xvphy_dev - representation of a Xilinx Video PHY
 * @dev: pointer to device
 * @iomem: serdes base address
 */
struct xvphy_dev {
	struct device *dev;
	/* virtual remapped I/O memory */
	void __iomem *iomem;
	int irq;
	/* protects the XVphy/XHdmiphy1 baseline against concurrent access */
	struct mutex xvphy_mutex;
	struct xvphy_lane *lanes[4];
	/* bookkeeping for the baseline subsystem driver instance */
	XVphy xvphy;
	XHdmiphy1 xgtphy;
	/* AXI Lite clock drives the clock detector */
	struct clk *axi_lite_clk;
	/* NI-DRU clock input */
	struct clk *dru_clk;
	/* If Video Phy flag */
	u32 isvphy;
};

/* given the (Linux) phy handle, return the xvphy */
void *xvphy_get_xvphy(struct phy *phy)
{
	struct xvphy_lane *vphy_lane = phy_get_drvdata(phy);
	struct xvphy_dev *vphy_dev = vphy_lane->data;

	if (vphy_dev->isvphy)
		return (void *)&vphy_dev->xvphy;

	return (void *)&vphy_dev->xgtphy;
}
EXPORT_SYMBOL_GPL(xvphy_get_xvphy);

/* given the (Linux) phy handle, enter critical section of xvphy baseline code
 * XVphy/XHdmiphy1 functions must be called with mutex acquired to prevent concurrent access
 * by XVphy/XHdmiphy1 and upper-layer video protocol drivers */
void xvphy_mutex_lock(struct phy *phy)
{
	struct xvphy_lane *vphy_lane = phy_get_drvdata(phy);
	struct xvphy_dev *vphy_dev = vphy_lane->data;
	hdmi_mutex_lock(&vphy_dev->xvphy_mutex);
}
EXPORT_SYMBOL_GPL(xvphy_mutex_lock);

void xvphy_mutex_unlock(struct phy *phy)
{
	struct xvphy_lane *vphy_lane = phy_get_drvdata(phy);
	struct xvphy_dev *vphy_dev = vphy_lane->data;
	hdmi_mutex_unlock(&vphy_dev->xvphy_mutex);
}
EXPORT_SYMBOL_GPL(xvphy_mutex_unlock);

/* XVphy functions must be called with mutex acquired to prevent concurrent access
 * by XVphy and upper-layer video protocol drivers */
EXPORT_SYMBOL_GPL(XVphy_GetPllType);
EXPORT_SYMBOL_GPL(XVphy_IBufDsEnable);
EXPORT_SYMBOL_GPL(XVphy_SetHdmiCallback);
EXPORT_SYMBOL_GPL(XVphy_HdmiCfgCalcMmcmParam);
EXPORT_SYMBOL_GPL(XVphy_MmcmStart);
EXPORT_SYMBOL_GPL(XVphy_HdmiDebugInfo);
EXPORT_SYMBOL_GPL(XVphy_RegisterDebug);
EXPORT_SYMBOL_GPL(XVphy_LogShow);
EXPORT_SYMBOL_GPL(XVphy_DruGetRefClkFreqHz);
EXPORT_SYMBOL_GPL(XVphy_GetLineRateHz);

/* exclusively required by TX */
EXPORT_SYMBOL_GPL(XVphy_Clkout1OBufTdsEnable);
EXPORT_SYMBOL_GPL(XVphy_SetHdmiTxParam);
EXPORT_SYMBOL_GPL(XVphy_IsBonded);
EXPORT_SYMBOL_GPL(XVphy_ClkDetFreqReset);
EXPORT_SYMBOL_GPL(XVphy_ClkDetGetRefClkFreqHz);

/* XHdmiphy1 functions must be called with mutex acquired to prevent concurrent access
 * by XHdmiphy1 and upper-layer video protocol drivers */
EXPORT_SYMBOL_GPL(XHdmiphy1_GetPllType);
EXPORT_SYMBOL_GPL(XHdmiphy1_IBufDsEnable);
EXPORT_SYMBOL_GPL(XHdmiphy1_SetHdmiCallback);
EXPORT_SYMBOL_GPL(XHdmiphy1_HdmiCfgCalcMmcmParam);
EXPORT_SYMBOL_GPL(XHdmiphy1_MmcmStart);
EXPORT_SYMBOL_GPL(XHdmiphy1_HdmiDebugInfo);
EXPORT_SYMBOL_GPL(XHdmiphy1_RegisterDebug);
EXPORT_SYMBOL_GPL(XHdmiphy1_LogShow);
EXPORT_SYMBOL_GPL(XHdmiphy1_DruGetRefClkFreqHz);
EXPORT_SYMBOL_GPL(XHdmiphy1_GetLineRateHz);

/* exclusively required by TX */
EXPORT_SYMBOL_GPL(XHdmiphy1_Clkout1OBufTdsEnable);
EXPORT_SYMBOL_GPL(XHdmiphy1_SetHdmiTxParam);
//EXPORT_SYMBOL_GPL(XHdmiphy1_IsBonded);
EXPORT_SYMBOL_GPL(XHdmiphy1_ClkDetFreqReset);
EXPORT_SYMBOL_GPL(XHdmiphy1_ClkDetGetRefClkFreqHz);

static void xvphy_intr_disable(struct xvphy_dev *vphydev)
{
	if (vphydev->isvphy)
		XVphy_IntrDisable(&vphydev->xvphy, XVPHY_INTR_HANDLER_TYPE_TXRESET_DONE |
			XVPHY_INTR_HANDLER_TYPE_RXRESET_DONE |
			XVPHY_INTR_HANDLER_TYPE_CPLL_LOCK |
			XVPHY_INTR_HANDLER_TYPE_QPLL0_LOCK |
			XVPHY_INTR_HANDLER_TYPE_TXALIGN_DONE |
			XVPHY_INTR_HANDLER_TYPE_QPLL1_LOCK |
			XVPHY_INTR_HANDLER_TYPE_TX_CLKDET_FREQ_CHANGE |
			XVPHY_INTR_HANDLER_TYPE_RX_CLKDET_FREQ_CHANGE |
			XVPHY_INTR_HANDLER_TYPE_TX_MMCM_LOCK_CHANGE |
			XVPHY_INTR_HANDLER_TYPE_RX_MMCM_LOCK_CHANGE |
			XVPHY_INTR_HANDLER_TYPE_TX_TMR_TIMEOUT |
			XVPHY_INTR_HANDLER_TYPE_RX_TMR_TIMEOUT);
	else
		XHdmiphy1_IntrDisable(&vphydev->xgtphy, XHDMIPHY1_INTR_HANDLER_TYPE_TXRESET_DONE |
			XHDMIPHY1_INTR_HANDLER_TYPE_RXRESET_DONE |
			XHDMIPHY1_INTR_HANDLER_TYPE_LCPLL_LOCK |
			XHDMIPHY1_INTR_HANDLER_TYPE_RPLL_LOCK |
			XHDMIPHY1_INTR_HANDLER_TYPE_TX_GPO_RISING_EDGE |
			XHDMIPHY1_INTR_HANDLER_TYPE_RX_GPO_RISING_EDGE |
			XHDMIPHY1_INTR_HANDLER_TYPE_TX_CLKDET_FREQ_CHANGE |
			XHDMIPHY1_INTR_HANDLER_TYPE_RX_CLKDET_FREQ_CHANGE |
			XHDMIPHY1_INTR_HANDLER_TYPE_TX_MMCM_LOCK_CHANGE |
			XHDMIPHY1_INTR_HANDLER_TYPE_RX_MMCM_LOCK_CHANGE |
			XHDMIPHY1_INTR_HANDLER_TYPE_TX_TMR_TIMEOUT |
			XHDMIPHY1_INTR_HANDLER_TYPE_RX_TMR_TIMEOUT);
}

static void xvphy_intr_enable(struct xvphy_dev *vphydev)
{
	if (vphydev->isvphy)
		XVphy_IntrEnable(&vphydev->xvphy, XVPHY_INTR_HANDLER_TYPE_TXRESET_DONE |
			XVPHY_INTR_HANDLER_TYPE_RXRESET_DONE |
			XVPHY_INTR_HANDLER_TYPE_CPLL_LOCK |
			XVPHY_INTR_HANDLER_TYPE_QPLL0_LOCK |
			XVPHY_INTR_HANDLER_TYPE_TXALIGN_DONE |
			XVPHY_INTR_HANDLER_TYPE_QPLL1_LOCK |
			XVPHY_INTR_HANDLER_TYPE_TX_CLKDET_FREQ_CHANGE |
			XVPHY_INTR_HANDLER_TYPE_RX_CLKDET_FREQ_CHANGE |
			XVPHY_INTR_HANDLER_TYPE_TX_MMCM_LOCK_CHANGE |
			XVPHY_INTR_HANDLER_TYPE_RX_MMCM_LOCK_CHANGE |
			XVPHY_INTR_HANDLER_TYPE_TX_TMR_TIMEOUT |
			XVPHY_INTR_HANDLER_TYPE_RX_TMR_TIMEOUT);
	else
		XHdmiphy1_IntrEnable(&vphydev->xgtphy, XHDMIPHY1_INTR_HANDLER_TYPE_TXRESET_DONE |
			XHDMIPHY1_INTR_HANDLER_TYPE_RXRESET_DONE |
			XHDMIPHY1_INTR_HANDLER_TYPE_LCPLL_LOCK |
			XHDMIPHY1_INTR_HANDLER_TYPE_RPLL_LOCK |
			XHDMIPHY1_INTR_HANDLER_TYPE_TX_GPO_RISING_EDGE |
			XHDMIPHY1_INTR_HANDLER_TYPE_RX_GPO_RISING_EDGE |
			XHDMIPHY1_INTR_HANDLER_TYPE_TX_CLKDET_FREQ_CHANGE |
			XHDMIPHY1_INTR_HANDLER_TYPE_RX_CLKDET_FREQ_CHANGE |
			XHDMIPHY1_INTR_HANDLER_TYPE_TX_MMCM_LOCK_CHANGE |
			XHDMIPHY1_INTR_HANDLER_TYPE_RX_MMCM_LOCK_CHANGE |
			XHDMIPHY1_INTR_HANDLER_TYPE_TX_TMR_TIMEOUT |
			XHDMIPHY1_INTR_HANDLER_TYPE_RX_TMR_TIMEOUT);
}

static irqreturn_t xvphy_irq_handler(int irq, void *dev_id)
{
	struct xvphy_dev *vphydev;

	vphydev = (struct xvphy_dev *)dev_id;
	if (!vphydev)
		return IRQ_NONE;

	/* Disable interrupts in the VPHY, they are re-enabled once serviced */
	xvphy_intr_disable(vphydev);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t xvphy_irq_thread(int irq, void *dev_id)
{
	struct xvphy_dev *vphydev;
	u32 IntrStatus;

	vphydev = (struct xvphy_dev *)dev_id;
	if (!vphydev)
		return IRQ_NONE;

	/* call baremetal interrupt handler with mutex locked */
	hdmi_mutex_lock(&vphydev->xvphy_mutex);

	if (vphydev->isvphy) {
		IntrStatus = XVphy_ReadReg(vphydev->xvphy.Config.BaseAddr, XVPHY_INTR_STS_REG);
		dev_dbg(vphydev->dev,"XVphy IntrStatus = 0x%08x\n", IntrStatus);
	} else {
		IntrStatus = XHdmiphy1_ReadReg(vphydev->xgtphy.Config.BaseAddr, XHDMIPHY1_INTR_STS_REG);
		dev_dbg(vphydev->dev,"XHdmiphy1 IntrStatus = 0x%08x\n", IntrStatus);
	}

	/* handle pending interrupts */
	if (vphydev->isvphy)
		XVphy_InterruptHandler(&vphydev->xvphy);
	else
		XHdmiphy1_InterruptHandler(&vphydev->xgtphy);
	hdmi_mutex_unlock(&vphydev->xvphy_mutex);

	/* Enable interrupt requesting in the VPHY */
	xvphy_intr_enable(vphydev);

	return IRQ_HANDLED;
}

/**
 * xvphy_phy_init - initializes a lane
 * @phy: pointer to kernel PHY device
 *
 * Return: 0 on success or error on failure
 */
static int xvphy_phy_init(struct phy *phy)
{
	struct xvphy_lane *vphy_lane = phy_get_drvdata(phy);
	struct xvphy_dev *vphy_dev = vphy_lane->data;

	dev_dbg(vphy_dev->dev, "xvphy_phy_init(%p).\n", phy);
	return 0;
}

/**
 * xvphy_xlate - provides a PHY specific to a controller
 * @dev: pointer to device
 * @args: arguments from dts
 *
 * Return: pointer to kernel PHY device or error on failure
 *
 */
static struct phy *xvphy_xlate(struct device *dev,
				   struct of_phandle_args *args)
{
	struct xvphy_dev *vphydev = dev_get_drvdata(dev);
	struct xvphy_lane *vphy_lane = NULL;
	struct device_node *phynode = args->np;
	int index;
	u8 controller;
	u8 instance_num;

	if (args->args_count != 4) {
		dev_err(dev, "Invalid number of cells in 'phy' property\n");
		return ERR_PTR(-EINVAL);
	}
	if (!of_device_is_available(phynode)) {
		dev_warn(dev, "requested PHY is disabled\n");
		return ERR_PTR(-ENODEV);
	}
	for (index = 0; index < of_get_child_count(dev->of_node); index++) {
		if (phynode == vphydev->lanes[index]->phy->dev.of_node) {
			vphy_lane = vphydev->lanes[index];
			break;
		}
	}
	if (!vphy_lane) {
		dev_err(dev, "failed to find appropriate phy\n");
		return ERR_PTR(-EINVAL);
	}

	/* get type of controller from lanes */
	controller = args->args[0];

	/* get controller instance number */
	instance_num = args->args[1];

	/* Check if lane sharing is required */
	vphy_lane->share_laneclk = args->args[2];

	/* get the direction for controller from lanes */
	vphy_lane->direction_tx = args->args[3];

	return vphy_lane->phy;
}

/* Local Global table for phy instance(s) configuration settings */
XVphy_Config XVphy_ConfigTable[XPAR_XVPHY_NUM_INSTANCES];
XHdmiphy1_Config XHdmiphy1_ConfigTable[XPAR_XHDMIPHY1_NUM_INSTANCES];

static struct phy_ops xvphy_phyops = {
	.init		= xvphy_phy_init,
	.owner		= THIS_MODULE,
};

static int instance = 0;
/* TX uses [1, 127], RX uses [128, 254] and VPHY uses [256, ...]. Note that 255 is used for not-present. */
#define VPHY_DEVICE_ID_BASE 256

static int vphy_parse_of(struct xvphy_dev *vphydev, void *c)
{
	struct device *dev = vphydev->dev;
	struct device_node *node = dev->of_node;
	int rc;
	u32 val;
	bool has_err_irq;
	XVphy_Config *vphycfg = (XVphy_Config *)c;
	XHdmiphy1_Config *xgtphycfg = (XHdmiphy1_Config *)c;

	rc = of_property_read_u32(node, "xlnx,transceiver-type", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->XcvrType = val;
	else
		xgtphycfg->XcvrType = val;

	rc = of_property_read_u32(node, "xlnx,tx-buffer-bypass", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->TxBufferBypass = val;
	else
		xgtphycfg->TxBufferBypass = val;

	rc = of_property_read_u32(node, "xlnx,input-pixels-per-clock", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->Ppc = val;
	else
		xgtphycfg->Ppc = val;

	rc = of_property_read_u32(node, "xlnx,nidru", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->DruIsPresent = val;
	else
		xgtphycfg->DruIsPresent = val;

	rc = of_property_read_u32(node, "xlnx,nidru-refclk-sel", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->DruRefClkSel = val;
	else
		xgtphycfg->DruRefClkSel = val;

	rc = of_property_read_u32(node, "xlnx,rx-no-of-channels", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->RxChannels = val;
	else
		xgtphycfg->RxChannels = val;

	rc = of_property_read_u32(node, "xlnx,tx-no-of-channels", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->TxChannels = val;
	else
		xgtphycfg->TxChannels = val;

	rc = of_property_read_u32(node, "xlnx,rx-protocol", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->RxProtocol = val;
	else
		xgtphycfg->RxProtocol = val;

	rc = of_property_read_u32(node, "xlnx,tx-protocol", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->TxProtocol = val;
	else
		xgtphycfg->TxProtocol = val;

	rc = of_property_read_u32(node, "xlnx,rx-refclk-sel", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->RxRefClkSel = val;
	else
		xgtphycfg->RxRefClkSel = val;

	rc = of_property_read_u32(node, "xlnx,tx-refclk-sel", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->TxRefClkSel = val;
	else
		xgtphycfg->TxRefClkSel = val;

	rc = of_property_read_u32(node, "xlnx,rx-pll-selection", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->RxSysPllClkSel = val;
	else
		xgtphycfg->RxSysPllClkSel = val;

	rc = of_property_read_u32(node, "xlnx,tx-pll-selection", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->TxSysPllClkSel = val;
	else
		xgtphycfg->TxSysPllClkSel = val;

	rc = of_property_read_u32(node, "xlnx,hdmi-fast-switch", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->HdmiFastSwitch = val;
	else
		xgtphycfg->HdmiFastSwitch = val;

	rc = of_property_read_u32(node, "xlnx,transceiver-width", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->TransceiverWidth = val;
	else
		xgtphycfg->TransceiverWidth = val;

	has_err_irq = false;
	has_err_irq = of_property_read_bool(node, "xlnx,err-irq-en");
	if (vphydev->isvphy)
		vphycfg->ErrIrq = has_err_irq;
	else
		xgtphycfg->ErrIrq = has_err_irq;

	rc = of_property_read_u32(node, "xlnx,use-gt-ch4-hdmi", &val);
	if (rc < 0)
		goto error_dt;
	if (vphydev->isvphy)
		vphycfg->UseGtAsTxTmdsClk = val;
	else
		xgtphycfg->UseGtAsTxTmdsClk = val;

	if (!vphydev->isvphy) {
		rc = of_property_read_u32(node, "xlnx,rx-frl-refclk-sel", &val);
		if (rc < 0)
			goto error_dt;
		xgtphycfg->RxFrlRefClkSel = val;

		rc = of_property_read_u32(node, "xlnx,tx-frl-refclk-sel", &val);
		if (rc < 0)
			goto error_dt;
		xgtphycfg->TxFrlRefClkSel = val;
	}

	return 0;

error_dt:
	dev_err(vphydev->dev, "Error parsing device tree");
	return -EINVAL;
}

/* Match table for of_platform binding */
static const struct of_device_id xvphy_of_match[] = {
	{ .compatible = "xlnx,hdmi-gt-controller-1.0" },
	{ .compatible = "xlnx,vid-phy-controller-2.2" },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xvphy_of_match);

/**
 * xvphy_probe - The device probe function for driver initialization.
 * @pdev: pointer to the platform device structure.
 *
 * Return: 0 for success and error value on failure
 */
static int xvphy_probe(struct platform_device *pdev)
{
	struct device_node *child, *np = pdev->dev.of_node;
	struct xvphy_dev *vphydev;
	struct phy_provider *provider;
	struct phy *phy;
	unsigned long axi_lite_rate;
	unsigned long dru_clk_rate;

	struct resource *res;
	int port = 0, index = 0;
	int ret;
	u32 Status;
	u32 Data;
	u16 DrpVal;

	void __iomem *iomem1;
	const struct of_device_id *match;

	dev_info(&pdev->dev, "probe started\n");
	vphydev = devm_kzalloc(&pdev->dev, sizeof(*vphydev), GFP_KERNEL);
	if (!vphydev)
		return -ENOMEM;

	/* mutex that protects against concurrent access */
	mutex_init(&vphydev->xvphy_mutex);

	vphydev->dev = &pdev->dev;
	/* set a pointer to our driver data */
	platform_set_drvdata(pdev, vphydev);

	match = of_match_node(xvphy_of_match, np);
	if (!match)
		return -ENODEV;

	if (strncmp(match->compatible, "xlnx,vid-phy-controller", 23) == 0)
		vphydev->isvphy = 1;
	else
		vphydev->isvphy = 0;

	XVphy_ConfigTable[instance].DeviceId = VPHY_DEVICE_ID_BASE + instance;
	XHdmiphy1_ConfigTable[instance].DeviceId = VPHY_DEVICE_ID_BASE + instance;

	dev_dbg(vphydev->dev,"DT parse start\n");
	if (vphydev->isvphy)
		ret = vphy_parse_of(vphydev, &XVphy_ConfigTable[instance]);
	else
		ret = vphy_parse_of(vphydev, &XHdmiphy1_ConfigTable[instance]);
	if (ret) return ret;
	dev_dbg(vphydev->dev,"DT parse done\n");

	for_each_child_of_node(np, child) {
		struct xvphy_lane *vphy_lane;

		vphy_lane = devm_kzalloc(&pdev->dev, sizeof(*vphy_lane),
					 GFP_KERNEL);
		if (!vphy_lane)
			return -ENOMEM;

		/* Assign lane number to gtr_phy instance */
		vphy_lane->lane = index;

		/* Disable lane sharing as default */
		vphy_lane->share_laneclk = -1;

		if (port >= 4) {
			dev_err(&pdev->dev, "MAX 4 PHY Lanes are supported\n");
			return -E2BIG;
		}

		/* array of pointer to vphy_lane structs */
		vphydev->lanes[port] = vphy_lane;

		/* create phy device for each lane */
		phy = devm_phy_create(&pdev->dev, child, &xvphy_phyops);
		if (IS_ERR(phy)) {
			ret = PTR_ERR(phy);
			if (ret == -EPROBE_DEFER)
				dev_info(&pdev->dev, "xvphy probe deferred\n");
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev, "failed to create PHY\n");
			return ret;
		}
		/* array of pointer to phy */
		vphydev->lanes[port]->phy = phy;
		/* where each phy device has vphy_lane as driver data */
		phy_set_drvdata(phy, vphydev->lanes[port]);
		/* and each vphy_lane points back to parent device */
		vphy_lane->data = vphydev;
		port++;
		index++;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	vphydev->iomem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(vphydev->iomem))
		return PTR_ERR(vphydev->iomem);

	/* set address in configuration data */
	XVphy_ConfigTable[instance].BaseAddr = (uintptr_t)vphydev->iomem;
	XHdmiphy1_ConfigTable[instance].BaseAddr = (uintptr_t)vphydev->iomem;

	vphydev->irq = platform_get_irq(pdev, 0);
	if (vphydev->irq <= 0) {
		dev_err(&pdev->dev, "platform_get_irq() failed\n");
		return vphydev->irq;
	}

	/* the AXI lite clock is used for the clock rate detector */
	if (vphydev->isvphy)
		vphydev->axi_lite_clk = devm_clk_get(&pdev->dev, "vid_phy_axi4lite_aclk");
	else
		vphydev->axi_lite_clk = devm_clk_get(&pdev->dev, "axi4lite_aclk");
	if (IS_ERR(vphydev->axi_lite_clk)) {
		ret = PTR_ERR(vphydev->axi_lite_clk);
		vphydev->axi_lite_clk = NULL;
		if (ret == -EPROBE_DEFER)
			dev_info(&pdev->dev, "axi-lite-clk not ready -EPROBE_DEFER\n");
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get the axi lite clk.\n");
		return ret;
	}

	ret = clk_prepare_enable(vphydev->axi_lite_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable axi-lite clk\n");
		return ret;
	}
	axi_lite_rate = clk_get_rate(vphydev->axi_lite_clk);
	dev_dbg(vphydev->dev,"AXI Lite clock rate = %lu Hz\n", axi_lite_rate);

	/* set axi-lite clk in configuration data */
	XVphy_ConfigTable[instance].AxiLiteClkFreq = axi_lite_rate;
	XVphy_ConfigTable[instance].DrpClkFreq = axi_lite_rate;
	XHdmiphy1_ConfigTable[instance].AxiLiteClkFreq = axi_lite_rate;
	XHdmiphy1_ConfigTable[instance].DrpClkFreq = axi_lite_rate;

	/* dru-clk is used for the nidru block for low res support */
	if ((vphydev->isvphy && (XVphy_ConfigTable[instance].DruIsPresent == (TRUE))) ||
		(!vphydev->isvphy && (XHdmiphy1_ConfigTable[instance].DruIsPresent == (TRUE)))) {
		vphydev->dru_clk = devm_clk_get(&pdev->dev, "dru-clk");
		if (IS_ERR(vphydev->dru_clk)) {
			ret = PTR_ERR(vphydev->dru_clk);
			vphydev->dru_clk = NULL;
			if (ret == -EPROBE_DEFER)
				dev_info(&pdev->dev, "dru-clk not ready -EPROBE_DEFER\n");
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev, "failed to get the nidru clk.\n");
			return ret;
		}

		ret = clk_prepare_enable(vphydev->dru_clk);
		if (ret) {
			dev_err(&pdev->dev, "failed to enable nidru clk\n");
			return ret;
		}

		dru_clk_rate = clk_get_rate(vphydev->dru_clk);
		dev_dbg(vphydev->dev, "default dru-clk rate = %lu\n", dru_clk_rate);
		if ((vphydev->isvphy && (dru_clk_rate != XVPHY_DRU_REF_CLK_HZ)) ||
			(!vphydev->isvphy && (dru_clk_rate != XHDMIPHY1_DRU_REF_CLK_HZ))) {

			if (vphydev->isvphy)
				ret = clk_set_rate(vphydev->dru_clk, XVPHY_DRU_REF_CLK_HZ);
			else
				ret = clk_set_rate(vphydev->dru_clk, XHDMIPHY1_DRU_REF_CLK_HZ);

			if (ret != 0) {
				dev_err(&pdev->dev, "Cannot set rate : %d\n", ret);
			}
			dru_clk_rate = clk_get_rate(vphydev->dru_clk);
			dev_dbg(vphydev->dev, "ref dru-clk rate = %lu\n", dru_clk_rate);
		}
	}
	else
	{
		dev_dbg(vphydev->dev, "DRU is not enabled from device tree\n");
	}

	provider = devm_of_phy_provider_register(&pdev->dev, xvphy_xlate);
	if (IS_ERR(provider)) {
		dev_err(&pdev->dev, "registering provider failed\n");
			return PTR_ERR(provider);
	}

	if (!vphydev->isvphy) {
		/* For Versal */
		XHdmiphy1_Config *xgtphycfg =
			(XHdmiphy1_Config *)(&XHdmiphy1_ConfigTable[instance]);

		iomem1 = ioremap(0xF70E000C, 4);
		if (IS_ERR(iomem1))
			dev_err(vphydev->dev, "[Versal] - Error in iomem 5\n");
		XHdmiphy1_Out32((INTPTR)iomem1, 0xF9E8D7C6);
		dev_dbg(vphydev->dev, "To: 0x%08x \r\n", XHdmiphy1_In32((INTPTR)iomem1));
		iounmap(iomem1);

		if (xgtphycfg->TxSysPllClkSel == 7 || xgtphycfg->RxSysPllClkSel == 8) {
			iomem1 = ioremap(0xF70E3C4C, 4);
			if (IS_ERR(iomem1))
				dev_err(vphydev->dev, "[Versal] - Error in iomem 6\n");
			dev_dbg(vphydev->dev, "RX:HS1 RPLL IPS  From: 0x%08x ", XHdmiphy1_In32((INTPTR)iomem1));
			XHdmiphy1_Out32((INTPTR)iomem1, 0x03000810);
			dev_dbg(vphydev->dev, "To: 0x%08x \r\n", XHdmiphy1_In32((INTPTR)iomem1));
			iounmap(iomem1);
		} else {
			iomem1 = ioremap(0xF70E3C48, 4);
			if (IS_ERR(iomem1))
				dev_err(vphydev->dev, "[Versal] - Error in iomem 7\n");
			dev_dbg(vphydev->dev, "TX:HS1 LCPLL IPS From: 0x%08x ", XHdmiphy1_In32((INTPTR)iomem1));
			XHdmiphy1_Out32((INTPTR)iomem1, 0x03E00810);
			dev_dbg(vphydev->dev, "To: 0x%08x \r\n", XHdmiphy1_In32((INTPTR)iomem1));
			iounmap(iomem1);
		}
		/* Delay 50ms for GT to complete initialization */
		usleep_range(50000, 50000);
	}

	/* Initialize HDMI VPHY */
	if (vphydev->isvphy)
		Status = XVphy_Hdmi_CfgInitialize(&vphydev->xvphy, 0/*QuadID*/,
				&XVphy_ConfigTable[instance]);
	else
		Status = XHdmiphy1_Hdmi_CfgInitialize(&vphydev->xgtphy, 0/*QuadID*/,
				&XHdmiphy1_ConfigTable[instance]);
	if (Status != XST_SUCCESS) {
		dev_err(&pdev->dev, "HDMI VPHY initialization error\n");
		return ENODEV;
	}

	if (vphydev->isvphy)
		Data = XVphy_GetVersion(&vphydev->xvphy);
	else
		Data = XHdmiphy1_GetVersion(&vphydev->xgtphy);
	dev_info(vphydev->dev, "VPhy version : %02d.%02d (%04x)\n", ((Data >> 24) & 0xFF),
			((Data >> 16) & 0xFF), (Data & 0xFFFF));


	ret = devm_request_threaded_irq(&pdev->dev, vphydev->irq, xvphy_irq_handler, xvphy_irq_thread,
			IRQF_TRIGGER_HIGH /*IRQF_SHARED*/, "xilinx-vphy", vphydev/*dev_id*/);

	if (ret) {
		dev_err(&pdev->dev, "unable to request IRQ %d\n", vphydev->irq);
		return ret;
	}

	if (vphydev->isvphy) {
		dev_dbg(vphydev->dev,"config.DruIsPresent = %d\n", XVphy_ConfigTable[instance].DruIsPresent);
		if (vphydev->xvphy.Config.DruIsPresent == (TRUE)) {
			dev_dbg(vphydev->dev,"DRU reference clock frequency %0d Hz\n\r",
						XVphy_DruGetRefClkFreqHz(&vphydev->xvphy));
		}
	} else {
		dev_dbg(vphydev->dev,"config.DruIsPresent = %d\n", XHdmiphy1_ConfigTable[instance].DruIsPresent);
		if (vphydev->xgtphy.Config.DruIsPresent == (TRUE)) {
			dev_info(vphydev->dev,"DRU reference clock frequency %0d Hz\n",
						XHdmiphy1_DruGetRefClkFreqHz(&vphydev->xgtphy));
		}
	}

	dev_info(&pdev->dev, "probe successful\n");
	/* probe has succeeded for this instance, increment instance index */
	instance++;
	return 0;
}

static int __maybe_unused xvphy_pm_suspend(struct device *dev)
{
	struct xvphy_dev *vphydev = dev_get_drvdata(dev);
	dev_dbg(vphydev->dev, "Vphy suspend function called\n");
	xvphy_intr_disable(vphydev);
	return 0;
}

static int __maybe_unused xvphy_pm_resume(struct device *dev)
{
	struct xvphy_dev *vphydev = dev_get_drvdata(dev);
	dev_dbg(vphydev->dev, "Vphy resume function called\n");
	xvphy_intr_enable(vphydev);
	return 0;
}

static const struct dev_pm_ops xvphy_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xvphy_pm_suspend, xvphy_pm_resume)
};

static struct platform_driver xvphy_driver = {
	.probe = xvphy_probe,
	.driver = {
		.name = "xilinx-vphy",
		.of_match_table	= xvphy_of_match,
		.pm = &xvphy_pm_ops,
	},
};
module_platform_driver(xvphy_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Leon Woestenberg <leon@sidebranch.com>");
MODULE_DESCRIPTION("Xilinx Vphy / HDMI GT Controller  driver");

/* phy sub-directory is used as a place holder for all shared code for
   hdmi-rx and hdmi-tx driver. All shared API's need to be exported */

/* Configuration Tables for hdcp */
XHdcp1x_Config XHdcp1x_ConfigTable[XPAR_XHDCP_NUM_INSTANCES];
XTmrCtr_Config XTmrCtr_ConfigTable[XPAR_XTMRCTR_NUM_INSTANCES];
XHdcp22_Cipher_Config XHdcp22_Cipher_ConfigTable[XPAR_XHDCP22_CIPHER_NUM_INSTANCES];
XHdcp22_mmult_Config XHdcp22_mmult_ConfigTable[XPAR_XHDCP22_MMULT_NUM_INSTANCES];
XHdcp22_Rng_Config XHdcp22_Rng_ConfigTable[XPAR_XHDCP22_RNG_NUM_INSTANCES];
XHdcp22_Rx_Config XHdcp22_Rx_ConfigTable[XPAR_XHDCP22_RX_NUM_INSTANCES];
XHdcp22_Tx_Config XHdcp22_Tx_ConfigTable[XPAR_XHDCP22_TX_NUM_INSTANCES];


/* common functionality shared between RX and TX */
/* xvidc component */
EXPORT_SYMBOL_GPL(XVidC_ReportTiming);
EXPORT_SYMBOL_GPL(XVidC_SetVideoStream);
EXPORT_SYMBOL_GPL(XVidC_ReportStreamInfo);
EXPORT_SYMBOL_GPL(XVidC_Set3DVideoStream);
EXPORT_SYMBOL_GPL(XVidC_GetPixelClockHzByVmId);
EXPORT_SYMBOL_GPL(XVidC_GetVideoModeIdWBlanking);
EXPORT_SYMBOL_GPL(XVidC_GetVideoModeId);
EXPORT_SYMBOL_GPL(XVidC_GetPixelClockHzByHVFr);
EXPORT_SYMBOL_GPL(XVidC_ShowStreamInfo);

/* Configuration Tables for hdcp */
EXPORT_SYMBOL_GPL(XHdcp1x_ConfigTable);
EXPORT_SYMBOL_GPL(XTmrCtr_ConfigTable);
EXPORT_SYMBOL_GPL(XHdcp22_Cipher_ConfigTable);
EXPORT_SYMBOL_GPL(XHdcp22_mmult_ConfigTable);
EXPORT_SYMBOL_GPL(XHdcp22_Rng_ConfigTable);
EXPORT_SYMBOL_GPL(XHdcp22_Rx_ConfigTable);
EXPORT_SYMBOL_GPL(XHdcp22_Tx_ConfigTable);

/* Global API's for hdcp key */
EXPORT_SYMBOL_GPL(XHdcp22Cmn_Sha256Hash);
EXPORT_SYMBOL_GPL(XHdcp22Cmn_HmacSha256Hash);
EXPORT_SYMBOL_GPL(XHdcp22Cmn_Aes128Encrypt);
EXPORT_SYMBOL_GPL(aes256_done);
EXPORT_SYMBOL_GPL(aes256_init);
EXPORT_SYMBOL_GPL(aes256_decrypt_ecb);

/* Global API's for xhdcp1x */
EXPORT_SYMBOL_GPL(XHdcp1x_SetTopologyUpdate);
EXPORT_SYMBOL_GPL(XHdcp22Rx_LoadPublicCert);
EXPORT_SYMBOL_GPL(XHdcp22Rx_LoadLc128);
EXPORT_SYMBOL_GPL(XHdcp22Rx_IsEnabled);
EXPORT_SYMBOL_GPL(XHdcp22Rx_IsRepeater);
EXPORT_SYMBOL_GPL(XHdcp1x_GetVersion);
EXPORT_SYMBOL_GPL(XHdcp1x_CipherIntrHandler);
EXPORT_SYMBOL_GPL(XHdcp1x_SetTopologyField);
EXPORT_SYMBOL_GPL(XHdcp1x_IsAuthenticated);
EXPORT_SYMBOL_GPL(XHdcp1x_Info);
EXPORT_SYMBOL_GPL(XHdcp1x_SetRepeater);
EXPORT_SYMBOL_GPL(XHdcp1x_ProcessAKsv);
EXPORT_SYMBOL_GPL(XHdcp1x_SetTimerStart);
EXPORT_SYMBOL_GPL(XHdcp1x_SetTimerStop);
EXPORT_SYMBOL_GPL(XHdcp1x_IsRepeater);
EXPORT_SYMBOL_GPL(XHdcp1x_SelfTest);
EXPORT_SYMBOL_GPL(XHdcp1x_IsInWaitforready);
EXPORT_SYMBOL_GPL(XHdcp1x_SetKeySelect);
EXPORT_SYMBOL_GPL(XHdcp1x_Reset);
EXPORT_SYMBOL_GPL(XHdcp1x_SetTimerDelay);
EXPORT_SYMBOL_GPL(XHdcp1x_LookupConfig);
EXPORT_SYMBOL_GPL(XHdcp1x_SetTopology);
EXPORT_SYMBOL_GPL(XHdcp1x_IsEnabled);
EXPORT_SYMBOL_GPL(XHdcp1x_Disable);
EXPORT_SYMBOL_GPL(XHdcp1x_SetDebugLogMsg);
EXPORT_SYMBOL_GPL(XHdcp1x_SetTopologyKSVList);
EXPORT_SYMBOL_GPL(XHdcp1x_IsEncrypted);
EXPORT_SYMBOL_GPL(XHdcp1x_SetCallback);
EXPORT_SYMBOL_GPL(XHdcp1x_HandleTimeout);
EXPORT_SYMBOL_GPL(XHdcp1x_Poll);
EXPORT_SYMBOL_GPL(XHdcp1x_Enable);
EXPORT_SYMBOL_GPL(XHdcp1x_CfgInitialize);
EXPORT_SYMBOL_GPL(XHdcp1x_IsInProgress);
EXPORT_SYMBOL_GPL(XHdcp1x_SetHdmiMode);
EXPORT_SYMBOL_GPL(XHdcp1x_SetPhysicalState);
EXPORT_SYMBOL_GPL(XHdcp1x_IsInComputations);
EXPORT_SYMBOL_GPL(XHdcp1x_EnableBlank);
EXPORT_SYMBOL_GPL(XHdcp1x_GetTopologyField);
EXPORT_SYMBOL_GPL(XHdcp1x_DisableEncryption);
EXPORT_SYMBOL_GPL(XHdcp1x_GetTopology);
EXPORT_SYMBOL_GPL(XHdcp1x_GetTopologyKSVList);
EXPORT_SYMBOL_GPL(XHdcp1x_DisableBlank);
EXPORT_SYMBOL_GPL(XHdcp1x_EnableEncryption);
EXPORT_SYMBOL_GPL(XHdcp1x_IsDwnstrmCapable);

/* Global API's for XTmr */
EXPORT_SYMBOL_GPL(XTmrCtr_CfgInitialize);
EXPORT_SYMBOL_GPL(XTmrCtr_IsExpired);
EXPORT_SYMBOL_GPL(XTmrCtr_Start);
EXPORT_SYMBOL_GPL(XTmrCtr_InitHw);
EXPORT_SYMBOL_GPL(XTmrCtr_SetOptions);
EXPORT_SYMBOL_GPL(XTmrCtr_SetResetValue);
EXPORT_SYMBOL_GPL(XTmrCtr_SetHandler);
EXPORT_SYMBOL_GPL(XTmrCtr_InterruptHandler);
EXPORT_SYMBOL_GPL(XTmrCtr_Offsets);
EXPORT_SYMBOL_GPL(XTmrCtr_LookupConfig);
EXPORT_SYMBOL_GPL(XTmrCtr_GetOptions);
EXPORT_SYMBOL_GPL(XTmrCtr_Stop);

/* Global API's for xhdcp22Rx */
EXPORT_SYMBOL_GPL(XHdcp22Rx_IsAuthenticated);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetDdcError);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetCallback);
EXPORT_SYMBOL_GPL(XHdcp22Rx_Info);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetRepeater);
EXPORT_SYMBOL_GPL(XHdcp22Rx_LogReset);
EXPORT_SYMBOL_GPL(XHdcp22Rx_Reset);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetTopologyUpdate);
EXPORT_SYMBOL_GPL(XHdcp22Rx_Poll);
EXPORT_SYMBOL_GPL(XHdcp22Rx_LogShow);
EXPORT_SYMBOL_GPL(XHdcp22Rx_CfgInitialize);
EXPORT_SYMBOL_GPL(XHdcp22Rx_IsInProgress);
EXPORT_SYMBOL_GPL(XHdcp22Rx_GetTimer);
EXPORT_SYMBOL_GPL(XHdcp22Rx_Disable);
EXPORT_SYMBOL_GPL(XHdcp22Rx_LoadPrivateKey);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetTopologyField);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetReadMessageComplete);
EXPORT_SYMBOL_GPL(XHdcp22Rx_GetContentStreamType);
EXPORT_SYMBOL_GPL(XHdcp22Rx_LookupConfig);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetWriteMessageAvailable);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetTopology);
EXPORT_SYMBOL_GPL(XHdcp22Rx_Enable);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetTopologyReceiverIdList);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetLinkError);
EXPORT_SYMBOL_GPL(XHdcp22Rx_IsEncryptionEnabled);

/* Global API's for xhdcp22Tx */
EXPORT_SYMBOL_GPL(XHdcp22Tx_IsInProgress);
EXPORT_SYMBOL_GPL(XHdcp22Tx_GetTopologyReceiverIdList);
EXPORT_SYMBOL_GPL(XHdcp22Tx_LoadRevocationTable);
EXPORT_SYMBOL_GPL(XHdcp22Tx_IsEnabled);
EXPORT_SYMBOL_GPL(XHdcp22Tx_GetTopologyField);
EXPORT_SYMBOL_GPL(XHdcp22Tx_Reset);
EXPORT_SYMBOL_GPL(XHdcp22Tx_SetMessagePollingValue);
EXPORT_SYMBOL_GPL(XHdcp22Tx_Enable);
EXPORT_SYMBOL_GPL(XHdcp22Tx_GetTimer);
EXPORT_SYMBOL_GPL(XHdcp22Tx_Disable);
EXPORT_SYMBOL_GPL(XHdcp1x_Authenticate);
EXPORT_SYMBOL_GPL(XHdcp22Tx_DisableEncryption);
EXPORT_SYMBOL_GPL(XHdcp22Tx_LookupConfig);
EXPORT_SYMBOL_GPL(XHdcp22Tx_LogReset);
EXPORT_SYMBOL_GPL(XHdcp22Tx_IsDwnstrmCapable);
EXPORT_SYMBOL_GPL(XHdcp22Tx_IsEncryptionEnabled);
EXPORT_SYMBOL_GPL(XHdcp22Tx_SetRepeater);
EXPORT_SYMBOL_GPL(XHdcp22Tx_Authenticate);
EXPORT_SYMBOL_GPL(XHdcp22Tx_LoadLc128);
EXPORT_SYMBOL_GPL(XHdcp22Tx_CfgInitialize);
EXPORT_SYMBOL_GPL(XHdcp22Tx_Info);
EXPORT_SYMBOL_GPL(XHdcp22Tx_IsRepeater);
EXPORT_SYMBOL_GPL(XHdcp22Tx_IsAuthenticated);
EXPORT_SYMBOL_GPL(XHdcp22Tx_GetTopology);
EXPORT_SYMBOL_GPL(XHdcp22Tx_DisableBlank);
EXPORT_SYMBOL_GPL(XHdcp22Tx_GetVersion);
EXPORT_SYMBOL_GPL(XHdcp22Tx_SetCallback);
EXPORT_SYMBOL_GPL(XHdcp22Tx_SetContentStreamType);
EXPORT_SYMBOL_GPL(XHdcp22Tx_EnableBlank);
EXPORT_SYMBOL_GPL(XHdcp22Tx_LogShow);
EXPORT_SYMBOL_GPL(XHdcp22Tx_EnableEncryption);
EXPORT_SYMBOL_GPL(XHdcp22Tx_Poll);

/* Debug */
EXPORT_SYMBOL_GPL(XDebug_SetDebugBufPrintf);
EXPORT_SYMBOL_GPL(XDebug_SetDebugPrintf);

/* xhdmic and VSIF component */
EXPORT_SYMBOL_GPL(XV_HdmiC_VSIF_ParsePacket);
EXPORT_SYMBOL_GPL(XV_HdmiC_IFAspectRatio_To_XVidC);
EXPORT_SYMBOL_GPL(XV_HdmiC_ParseGCP);
EXPORT_SYMBOL_GPL(XVidC_GetVideoModeIdExtensive);
EXPORT_SYMBOL_GPL(XV_HdmiC_ParseAudioInfoFrame);
EXPORT_SYMBOL_GPL(XV_HdmiC_ParseAVIInfoFrame);
EXPORT_SYMBOL_GPL(XV_HdmiC_AudioIF_GeneratePacket);
EXPORT_SYMBOL_GPL(XV_HdmiC_XVidC_To_IfColorformat);
EXPORT_SYMBOL_GPL(XV_HdmiC_AVIIF_GeneratePacket);
EXPORT_SYMBOL_GPL(XV_HdmiC_VSIF_GeneratePacket);
EXPORT_SYMBOL_GPL(VicTable);
EXPORT_SYMBOL_GPL(XV_HdmiC_ParseDRMIF);
EXPORT_SYMBOL_GPL(XV_HdmiC_DRMIF_GeneratePacket);
