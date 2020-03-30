/*******************************************************************************
 *
 *
 * Copyright (C) 2015, 2016, 2017 Xilinx, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details
 *
*******************************************************************************/
/******************************************************************************/
/**
 *
 * @file xhdmiphy1.c
 *
 * Contains a minimal set of functions for the XHdmiphy1 driver that allow
 * access to all of the Video PHY core's functionality. See xhdmiphy1.h for a
 * detailed description of the driver.
 *
 * @note	None.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -----------------------------------------------
 *            dd/mm/yy
 * ----- ---- -------- -----------------------------------------------
 * 1.0   gm   10/12/18 Initial release.
 * </pre>
 *
*******************************************************************************/

/******************************* Include Files ********************************/

#include "xhdmiphy1.h"
#include "xhdmiphy1_i.h"

/************************** Constant Definitions *****************************/
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_WHITE   "\x1b[37m"
#define ANSI_COLOR_RESET   "\x1b[0m"


/**************************** Function Prototypes *****************************/

/**************************** Function Definitions ****************************/

/*****************************************************************************/
/**
* This function will reset the driver's logginc mechanism.
*
* @param	InstancePtr is a pointer to the XHdmiphy1 core instance.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdmiphy1_LogReset(XHdmiphy1 *InstancePtr)
{
#ifdef XV_HDMIPHY1_LOG_ENABLE
	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);

	InstancePtr->Log.HeadIndex = 0;
	InstancePtr->Log.TailIndex = 0;
#endif
}

#ifdef XV_HDMIPHY1_LOG_ENABLE
/*****************************************************************************/
/**
* This function will insert an event in the driver's logginc mechanism.
*
* @param	InstancePtr is a pointer to the XHdmiphy1 core instance.
* @param	Evt is the event type to log.
* @param	Data is the associated data for the event.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void XHdmiphy1_LogWrite(XHdmiphy1 *InstancePtr, XHdmiphy1_LogEvent Evt,
        u8 Data)
{
	u64 TimeUnit = 0;

	if (InstancePtr->LogWriteCallback) {
		TimeUnit = InstancePtr->LogWriteCallback(InstancePtr->LogWriteRef);
	}
	/* Verify arguments. */
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(Evt <= (XHDMIPHY1_LOG_EVT_DUMMY));
	Xil_AssertVoid(Data < 0xFF);

	/* Write data and event into log buffer */
	InstancePtr->Log.DataBuffer[InstancePtr->Log.HeadIndex] =
			(Data << 8) | Evt;
	InstancePtr->Log.TimeRecord[InstancePtr->Log.HeadIndex] = TimeUnit;

	/* Update head pointer if reached to end of the buffer */
	if (InstancePtr->Log.HeadIndex ==
			(u8)((sizeof(InstancePtr->Log.DataBuffer) /
            sizeof(InstancePtr->Log.DataBuffer[0])) - 1)) {
		/* Clear pointer */
		InstancePtr->Log.HeadIndex = 0;
	}
	else {
		/* Increment pointer */
		InstancePtr->Log.HeadIndex++;
	}

	/* Check tail pointer. When the two pointer are equal, then the buffer
	 * is full. In this case then increment the tail pointer as well to
	 * remove the oldest entry from the buffer. */
	if (InstancePtr->Log.TailIndex == InstancePtr->Log.HeadIndex) {
		if (InstancePtr->Log.TailIndex ==
			(u8)((sizeof(InstancePtr->Log.DataBuffer) /
            sizeof(InstancePtr->Log.DataBuffer[0])) - 1)) {
			InstancePtr->Log.TailIndex = 0;
		}
		else {
			InstancePtr->Log.TailIndex++;
		}
	}
}
#endif

/*****************************************************************************/
/**
* This function will read the last event from the log.
*
* @param	InstancePtr is a pointer to the XHdmiphy1 core instance.
*
* @return	The log data.
*
* @note		None.
*
******************************************************************************/
u16 XHdmiphy1_LogRead(XHdmiphy1 *InstancePtr)
{
#ifdef XV_HDMIPHY1_LOG_ENABLE
	u16 Log;

	/* Verify argument. */
	Xil_AssertNonvoid(InstancePtr != NULL);

	/* Check if there is any data in the log */
	if (InstancePtr->Log.TailIndex == InstancePtr->Log.HeadIndex) {
		Log = 0;
	}
	else {
		Log = InstancePtr->Log.DataBuffer[InstancePtr->Log.TailIndex];

		/* Increment tail pointer */
		if (InstancePtr->Log.TailIndex ==
			(u8)((sizeof(InstancePtr->Log.DataBuffer) /
            sizeof(InstancePtr->Log.DataBuffer[0])) - 1)) {
			InstancePtr->Log.TailIndex = 0;
		}
		else {
			InstancePtr->Log.TailIndex++;
		}
	}

	return Log;
#endif
}

/*****************************************************************************/
/**
* This function will print the entire log to the passed buffer.
*
* @param	InstancePtr is a pointer to the XVphy core instance.
* @param	buff Buffer to print to
* @param	buff_size size off buff passed
*
* @return	number of bytes written to buff.
*
* @note		None.
*
******************************************************************************/
int XHdmiphy1_LogShow(XHdmiphy1 *InstancePtr, char *buff, int buff_size)
{
	int strSize = 0;
#ifdef XV_HDMIPHY1_LOG_ENABLE
	u32 Log;
	u8 Evt;
	u8 Data;
	u64 TimeUnit;

	/* Verify argument. */
	Xil_AssertVoid(InstancePtr != NULL);

	strSize = scnprintf(buff, buff_size,
			"\r\n\n\nHDMIPHY log\r\n" \
			"------\r\n");

	/* Read time record */
	TimeUnit = InstancePtr->Log.TimeRecord[InstancePtr->Log.TailIndex];

	/* Read log data */
	Log = XHdmiphy1_LogRead(InstancePtr);

	while (Log != 0 && (buff_size - strSize) > 30 ) {
		/* Event */
		Evt = Log & 0xff;

		/* Data */
		Data = (Log >> 8) & 0xFF;

		switch (Evt) {
		case (XHDMIPHY1_LOG_EVT_NONE):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"GT log end\r\n-------\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_QPLL_EN):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"QPLL enable (%0d)\r\n", Data);
			break;
		case (XHDMIPHY1_LOG_EVT_QPLL_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"QPLL reset (%0d)\r\n", Data);
			break;
		case (XHDMIPHY1_LOG_EVT_CPLL_EN):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"CPLL enable (%0d)\r\n", Data);
			break;
		case (XHDMIPHY1_LOG_EVT_CPLL_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
				"CPLL reset (%0d)\r\n", Data);
			break;
		case (XHDMIPHY1_LOG_EVT_TXPLL_EN):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
				"TX MMCM enable (%0d)\r\n", Data);
			break;
		case (XHDMIPHY1_LOG_EVT_TXPLL_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
				"TX MMCM reset (%0d)\r\n", Data);
			break;
		case (XHDMIPHY1_LOG_EVT_RXPLL_EN):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
				"RX MMCM enable (%0d)\r\n", Data);
			break;
		case (XHDMIPHY1_LOG_EVT_RXPLL_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
				"RX MMCM reset (%0d)\r\n", Data);
			break;
		case (XHDMIPHY1_LOG_EVT_GTRX_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"GT RX reset (%0d)\r\n", Data);
			break;
		case (XHDMIPHY1_LOG_EVT_GTTX_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"GT TX reset (%0d)\r\n", Data);
			break;
		case (XHDMIPHY1_LOG_EVT_VID_TX_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Video TX reset (%0d)\r\n", Data);
			break;
		case (XHDMIPHY1_LOG_EVT_VID_RX_RST):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Video RX reset (%0d)\r\n", Data);
			break;
		case (XHDMIPHY1_LOG_EVT_TX_ALIGN):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX alignment done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX alignment start.\r\n.");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_TX_ALIGN_TMOUT):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX alignment watchdog timed out.\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_TX_TMR):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX timer event\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX timer load\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_RX_TMR):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RX timer event\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RX timer load\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_CPLL_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"CPLL reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"CPLL reconfig start\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_GT_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT reconfig start\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_GT_TX_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT TX reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT TX reconfig start\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_GT_RX_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT RX reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT RX reconfig start\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_QPLL_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"QPLL reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"QPLL reconfig start\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_INIT):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT init done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"GT init start\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_TXPLL_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX MMCM reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX MMCM reconfig start\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_RXPLL_RECONFIG):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RX MMCM reconfig done\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RX MMCM reconfig start\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_QPLL_LOCK):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"QPLL lock\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"QPLL lost lock\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_CPLL_LOCK):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"CPLL lock\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"CPLL lost lock\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_LCPLL_LOCK):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"LCPLL lock\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"LCPLL lost lock\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_RPLL_LOCK):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RPLL lock\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RPLL lost lock\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_RXPLL_LOCK):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RX MMCM lock\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RX MMCM lost lock\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_TXPLL_LOCK):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX MMCM lock\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"TX MMCM lost lock\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_TX_RST_DONE):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX reset done\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_RX_RST_DONE):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX reset done\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_TX_FREQ):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX frequency event\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_RX_FREQ):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX frequency event\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_DRU_EN):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RX DRU enable\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
						"RX DRU disable\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_TXGPO_RE):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX GPO Rising Edge Detected\r\n");
			} else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX MSTRESET Toggled\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_RXGPO_RE):
			if (Data == 1) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX GPO Rising Edge Detected\r\n");
			} else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX MSTRESET Toggled\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_FRL_RECONFIG):
			if (Data == XHDMIPHY1_DIR_RX) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX FRL Reconfig\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX FRL Reconfig\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_TMDS_RECONFIG):
			if (Data == XHDMIPHY1_DIR_RX) {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
					"RX TMDS Reconfig\r\n");
			}
			else {
				strSize += scnprintf(buff+strSize, buff_size-strSize,
					"TX TMDS Reconfig\r\n");
			}
			break;
		case (XHDMIPHY1_LOG_EVT_1PPC_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error! The HDMIPHY cannot support this video ");
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"format at PPC = 1\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_PPC_MSMTCH_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error! HDMI TX SS PPC value, doesn't match with"
					" HDMIPHY PPC value\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_VDCLK_HIGH_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error! Video PHY cannot"
					"support resolutions with video clock"
					" > 148.5 MHz.\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_NO_DRU):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Warning: No DRU instance. ");
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Low resolution video isn't supported in this "
					"design.\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_GT_QPLL_CFG_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error! QPLL config not found!\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_GT_CPLL_CFG_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error! CPLL config not found!\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_GT_LCPLL_CFG_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error! LCPLL config not found!\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_GT_RPLL_CFG_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error! RPLL config not found!\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_VD_NOT_SPRTD_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error: This video format "
					"is not supported by this device\r\n");
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Change to another format\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_MMCM_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error! MMCM config not found!\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_HDMI20_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error!  The Video PHY doesn't "
					"support HDMI 2.0 line rates\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_NO_QPLL_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error!  There's no QPLL instance in the design\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_DRU_CLK_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error: Wrong DRU REFCLK frequency detected\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_USRCLK_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error! User Clock frequency is more than 300 MHz\r\n");
			break;
		case (XHDMIPHY1_LOG_EVT_SPDGRDE_ERR):
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Error!  %s: Line rates > 8.0 "
					"Gbps are not supported by -1/-1LV devices"
					ANSI_COLOR_RESET "\r\n",
					(Data == XHDMIPHY1_DIR_RX) ? "RX" : "TX");
			break;
		default:
			strSize += scnprintf(buff+strSize, buff_size-strSize,
					"Unknown event %i\r\n", Evt);
			break;
		}

		if((buff_size - strSize) > 30) {
			/* Read log data */
			Log = XHdmiphy1_LogRead(InstancePtr);
		} else {
			Log = 0;
		}
	}
#else
	strSize += scnprintf(buff+strSize, buff_size-strSize,
			"\r\nINFO:: HDMIPHY Log Feature is Disabled \r\n");
#endif
    return strSize;
}
