/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <utils/Log.h>
#include "sensor.h"
#include "jpeg_exif_header.h"
#include "sensor_drv_u.h"
#include "sensor_raw.h"
#include "sensor_ov5647_raw_param.c"

#define ov5647_I2C_ADDR_W        0x36
#define ov5647_I2C_ADDR_R         0x36

#define OV5647_MIN_FRAME_LEN_PRV  0x4a0

LOCAL uint32_t _ov5647_GetResolutionTrimTab(uint32_t param);
LOCAL uint32_t _ov5647_PowerOn(uint32_t power_on);
LOCAL uint32_t _ov5647_Identify(uint32_t param);
LOCAL uint32_t _ov5647_BeforeSnapshot(uint32_t param);
LOCAL uint32_t _ov5647_after_snapshot(uint32_t param);
LOCAL uint32_t _ov5647_StreamOn(uint32_t param);
LOCAL uint32_t _ov5647_StreamOff(uint32_t param);
LOCAL uint32_t _ov5647_write_exposure(uint32_t param);
LOCAL uint32_t _ov5647_write_gain(uint32_t param);
LOCAL uint32_t _ov5647_write_af(uint32_t param);
LOCAL uint32_t _ov5647_flash(uint32_t param);
LOCAL uint32_t _ov5647_ReadGain(uint32_t*  gain_ptr);
LOCAL uint32_t _ov5647_SetEV(uint32_t param);
LOCAL uint32_t _ov5647_ExtFunc(uint32_t ctl_param);
LOCAL uint32_t _dw9174_SRCInit(uint32_t mode);


static uint32_t g_flash_mode_en = 0;
static uint32_t s_ov5647_gain = 0;

LOCAL const SENSOR_REG_T ov5647_com_mipi_raw[] = {
	//@@5.1.1 Initialization (Global Setting)
	//Slave_ID=0x6c//
	{0x0100, 0x00}, // software standby
	{0x0103, 0x01}, // software reset
	//delay(5ms)
	{SENSOR_WRITE_DELAY, 0x0a},
	//s1 4//
	{0x370c, 0x03}, // analog control
	{0x5000, 0x06}, // lens off, bpc on, wpc on
	{0x5003, 0x08}, // buf_en
	{0x5a00, 0x08},
	{0x3000, 0xff}, // D[9:8] output
	{0x3001, 0xff}, // D[7:0] output
	{0x3002, 0xff}, // Vsync, Href, PCLK, Frex, Strobe, SDA, GPIO1, GPIO0 output
	{0x301d, 0xf0},
	{0x3a18, 0x00}, // gain ceiling = 15.5x
	{0x3a19, 0xf8}, // gain ceiling
	{0x3c01, 0x80}, // band detection manual mode
	{0x3b07, 0x0c}, // strobe frex mode
	// analog control
	{0x3630, 0x2e},
	{0x3632, 0xe2},
	{0x3633, 0x23},
	{0x3634, 0x44},
	{0x3620, 0x64},
	{0x3621, 0xe0},
	{0x3600, 0x37},
	{0x3704, 0xa0},
	{0x3703, 0x5a},
	{0x3715, 0x78},
	{0x3717, 0x01},
	{0x3731, 0x02},
	{0x370b, 0x60},
	{0x3705, 0x1a},
	{0x3f05, 0x02},
	{0x3f06, 0x10},
	{0x3f01, 0x0a},
	// AG/AE target
	{0x3a0f, 0x58}, // stable in high
	{0x3a10, 0x50}, // stable in low
	{0x3a1b, 0x58}, // stable out high
	{0x3a1e, 0x50}, // stable out low
	{0x3a11, 0x60}, // fast zone high
	{0x3a1f, 0x28}, // fast zone low
	{0x4001, 0x02}, // BLC start line
	{0x4000, 0x09}, // BLC enable
	{0x3000, 0x00}, // D[9:8] input
	{0x3001, 0x00}, // D[7:0] input
	{0x3002, 0x00}, // Vsync, Href, PCLK, Frex, Strobe, SDA, GPIO1, GPIO0 input
	{0x3017, 0xe0}, // MIPI PHY
	{0x301c, 0xfc},
	{0x3636, 0x06}, // analog control
	{0x3016, 0x08}, // MIPI pad enable
	{0x3827, 0xec},
	{0x3018, 0x44}, // MIPI 2 lane, MIPI enable
	{0x3035, 0x21}, // PLL
	{0x3106, 0xf5}, // PLL
	{0x3034, 0x1a}, // PLL
	{0x301c, 0xf8},
	{0x3503, 0x03}, // Gain has no latch delay, AGC manual, AEC manual
	{0x3501, 0x10}, // exposure[15:8]
	{0x3502, 0x80}, // exposure[7:0]
	{0x350a, 0x00}, // gain[9:8]
	{0x350b, 0x7f}, // gain[7:0]
	{0x5001, 0x01}, // AWB on
	{0x5180, 0x08}, // AWB manual gain enable
	{0x5186, 0x04}, // manual red gain high
	{0x5187, 0x00}, // manual red gain low
	{0x5188, 0x04}, // manual green gain high
	{0x5189, 0x00}, // manual green gain low
	{0x518a, 0x04}, // manual blue gain high
	{0x518b, 0x00}, // manual blue gain low
	{0x5000, 0x06} // lenc off, bpc on, wpc on
};

LOCAL const SENSOR_REG_T ov5647_1280X960_mipi_raw[] = {
	//@@5.1.2 Preview 1280x960 30fps 24M MCLK 2lane 280Mbps/lane
	//100 99 1280 960
	//100 98 1 0
	//102 84 1 ffff
	//102 3601 2ee
	//{0x0100, 0x00}, // software standby
	//pll
	{0x3034, 0x1a}, // PLL
	{0x3035, 0x21}, // PLL
	{0x3106, 0xf5}, // PLL
	{0x3036, 0x46}, // PLL
	{0x303c, 0x11}, // PLL
	{0x3821, 0x07}, // ISP mirror on, Sensor mirror on, bin on
	{0x3820, 0x41}, // ISP flip off, Sensor flip off, bin on
	{0x3612, 0x59}, // analog control
	{0x3618, 0x00}, // analog control
	{0x380c, 0x07}, // HTS = 1896
	{0x380d, 0x68}, // HTS
	{0x380e, 0x04}, // VTS = 984
	{0x380f, 0xa0}, // VTS
	{0x3814, 0x31}, // X INC
	{0x3815, 0x31}, // Y INC
	{0x3708, 0x64}, // analog control
	{0x3709, 0x52}, // analog control
	{0x3808, 0x05}, // X OUTPUT SIZE = 1280
	{0x3809, 0x00}, // X OUTPUT SIZE
	{0x380a, 0x03}, // Y OUTPUT SIZE = 960
	{0x380b, 0xc0}, // Y OUTPUT SIZE
	{0x3800, 0x00}, // X Start
	{0x3801, 0x18}, // X Start
	{0x3802, 0x00}, // Y Start
	{0x3803, 0x0e}, // Y Start
	{0x3804, 0x0a}, // X End
	{0x3805, 0x27}, // X End
	{0x3806, 0x07}, // Y End
	{0x3807, 0x95}, // Y End
	// banding filter
	{0x3a08, 0x01}, // B50
	{0x3a09, 0x27}, // B50
	{0x3a0a, 0x00}, // B60
	{0x3a0b, 0xf6}, // B60
	{0x3a0d, 0x04}, // B50 max
	{0x3a0e, 0x03}, // B60 max
	{0x4004, 0x02}, // black line number
	{0x4510, 0x04},
	{0x4837, 0x10}, // MIPI pclk period
	//{0x0100, 0x01}, // wake up from software standby
};

LOCAL const SENSOR_REG_T ov5647_2592x1944_mipi_raw[] = {
	//@@5.1.4 Capture 2592x1944 15fps 24M MCLK 2lane 408Mbps/lane
	//100 99 2592 1944
	//100 98 1 0
	//102 84 1 ffff
	//102 3601 2ee
	{0x0100, 0x00}, // software standby
	{0x3035, 0x21}, // PLL
	{0x3036, 0x66}, // PLL
	{0x303c, 0x11}, // PLL
	{0x3821, 0x06}, // ISP mirror on, Sensor mirror on
	{0x3820, 0x00}, // ISP flip off, Sensor flip off
	{0x3612, 0x5b}, // analog control
	{0x3618, 0x04}, // analog control
	{0x380c, 0x0a}, // HTS = 2752
	{0x380d, 0xc0}, // HTS
	{0x380e, 0x07}, // VTS = 1974
	{0x380f, 0xb6}, // VTS
	{0x3814, 0x11}, // X INC
	{0x3815, 0x11}, // X INC
	{0x3708, 0x64}, // analog control
	{0x3709, 0x12}, // analog control
	{0x3808, 0x0a}, // X OUTPUT SIZE = 2592
	{0x3809, 0x20}, // X OUTPUT SIZE
	{0x380a, 0x07}, // Y OUTPUT SIZE = 1944
	{0x380b, 0x98}, // Y OUTPUT SIZE
	{0x3800, 0x00}, // X Start
	{0x3801, 0x0c}, // X Start
	{0x3802, 0x00}, // Y Start
	{0x3803, 0x02}, // Y Start
	{0x3804, 0x0a}, // X End
	{0x3805, 0x33}, // X End
	{0x3806, 0x07}, // Y End
	{0x3807, 0xa1}, // Y End
	// Banding filter
	{0x3a08, 0x01}, // B50
	{0x3a09, 0x28}, // B50
	{0x3a0a, 0x00}, // B60
	{0x3a0b, 0xf6}, // B60
	{0x3a0d, 0x07}, // B60 max
	{0x3a0e, 0x06}, // B50 max
	{0x4004, 0x04}, // black line number
	//{0x4837, 0x19}, // MIPI pclk period
	{0x4837, 0x0a}, // MIPI pclk period
	//{0x0100, 0x01}, // wake up from software standby
};


LOCAL SENSOR_REG_TAB_INFO_T s_ov5647_resolution_Tab_RAW[] = {
	{ADDR_AND_LEN_OF_ARRAY(ov5647_com_mipi_raw), 0, 0, 12, SENSOR_IMAGE_FORMAT_RAW},
	{ADDR_AND_LEN_OF_ARRAY(ov5647_1280X960_mipi_raw), 1280, 960, 24, SENSOR_IMAGE_FORMAT_RAW},
	{ADDR_AND_LEN_OF_ARRAY(ov5647_2592x1944_mipi_raw), 2592, 1944, 24, SENSOR_IMAGE_FORMAT_RAW},
	{PNULL, 0, 0, 0, 0, 0},
	{PNULL, 0, 0, 0, 0, 0},
	{PNULL, 0, 0, 0, 0, 0},
	{PNULL, 0, 0, 0, 0, 0},
	{PNULL, 0, 0, 0, 0, 0},
	{PNULL, 0, 0, 0, 0, 0}
};

LOCAL SENSOR_TRIM_T s_ov5647_Resolution_Trim_Tab[] = {
	{0, 0, 0, 0, 0, 0},
	{0, 0, 1280, 960, 337, 560},
	{0, 0, 2592, 1944, 331, 816},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0}
};

static struct sensor_raw_info s_ov5647_mipi_raw_info={
	&s_ov5647_version_info,
	&s_ov5647_tune_info,
	&s_ov5647_fix_info,
	&s_ov5647_cali_info,
};

struct sensor_raw_info* s_ov5647_mipi_raw_info_ptr=&s_ov5647_mipi_raw_info;

LOCAL SENSOR_IOCTL_FUNC_TAB_T s_ov5647_ioctl_func_tab = {
	PNULL,
	_ov5647_PowerOn,
	PNULL,
	_ov5647_Identify,

	PNULL,		// write register
	PNULL,		// read  register
	PNULL,
	_ov5647_GetResolutionTrimTab,

	// External
	PNULL,
	PNULL,
	PNULL,

	PNULL, 		//_ov5647_set_brightness,
	PNULL, 		// _ov5647_set_contrast,
	PNULL,
	PNULL,		//_ov5647_set_saturation,

	PNULL, 		//_ov5647_set_work_mode,
	PNULL, 		//_ov5647_set_image_effect,

	_ov5647_BeforeSnapshot,
	_ov5647_after_snapshot,
	_ov5647_flash,
	PNULL,
	_ov5647_write_exposure,
	PNULL,
	_ov5647_write_gain,
	PNULL,
	PNULL,
	_ov5647_write_af,
	PNULL,
	PNULL, 		//_ov5647_set_awb,
	PNULL,
	PNULL,
	PNULL, 		//_ov5647_set_ev,
	PNULL,
	PNULL,
	PNULL,
	PNULL, 		//_ov5647_GetExifInfo,
	_ov5647_ExtFunc,
	PNULL, 		//_ov5647_set_anti_flicker,
	PNULL, 		//_ov5647_set_video_mode,
	PNULL, 		//pick_jpeg_stream
	PNULL, 		//meter_mode
	PNULL, 		//get_status
	_ov5647_StreamOn,
	_ov5647_StreamOff,
};


SENSOR_INFO_T g_ov5647_mipi_raw_info = {
	ov5647_I2C_ADDR_W,	// salve i2c write address
	ov5647_I2C_ADDR_R,	// salve i2c read address

	SENSOR_I2C_REG_16BIT | SENSOR_I2C_REG_8BIT,	// bit0: 0: i2c register value is 8 bit, 1: i2c register value is 16 bit
	// bit1: 0: i2c register addr  is 8 bit, 1: i2c register addr  is 16 bit
	// other bit: reseved
	SENSOR_HW_SIGNAL_PCLK_N | SENSOR_HW_SIGNAL_VSYNC_N | SENSOR_HW_SIGNAL_HSYNC_P,	// bit0: 0:negative; 1:positive -> polarily of pixel clock
	// bit2: 0:negative; 1:positive -> polarily of horizontal synchronization signal
	// bit4: 0:negative; 1:positive -> polarily of vertical synchronization signal
	// other bit: reseved

	// preview mode
	SENSOR_ENVIROMENT_NORMAL | SENSOR_ENVIROMENT_NIGHT,

	// image effect
	SENSOR_IMAGE_EFFECT_NORMAL |
	    SENSOR_IMAGE_EFFECT_BLACKWHITE |
	    SENSOR_IMAGE_EFFECT_RED |
	    SENSOR_IMAGE_EFFECT_GREEN |
	    SENSOR_IMAGE_EFFECT_BLUE |
	    SENSOR_IMAGE_EFFECT_YELLOW |
	    SENSOR_IMAGE_EFFECT_NEGATIVE | SENSOR_IMAGE_EFFECT_CANVAS,

	// while balance mode
	0,

	7,			// bit[0:7]: count of step in brightness, contrast, sharpness, saturation
	// bit[8:31] reseved

	SENSOR_LOW_PULSE_RESET,	// reset pulse level
	50,			// reset pulse width(ms)

	SENSOR_HIGH_LEVEL_PWDN,	// 1: high level valid; 0: low level valid

	1,			// count of identify code
	{{0x0A, 0x56},		// supply two code to identify sensor.
	 {0x0B, 0x47}},		// for Example: index = 0-> Device id, index = 1 -> version id

	SENSOR_AVDD_2800MV,	// voltage of avdd

	2592,			// max width of source image
	1944,			// max height of source image
	"ov5647",			// name of sensor

	SENSOR_IMAGE_FORMAT_RAW,	// define in SENSOR_IMAGE_FORMAT_E enum,SENSOR_IMAGE_FORMAT_MAX
	// if set to SENSOR_IMAGE_FORMAT_MAX here, image format depent on SENSOR_REG_TAB_INFO_T

	SENSOR_IMAGE_PATTERN_RAWRGB_GB,// pattern of input image form sensor;

	s_ov5647_resolution_Tab_RAW,	// point to resolution table information structure
	&s_ov5647_ioctl_func_tab,	// point to ioctl function table
	&s_ov5647_mipi_raw_info,		// information and table about Rawrgb sensor
	NULL,			//&g_ov5640_ext_info,                // extend information about sensor
	SENSOR_AVDD_1800MV,	// iovdd
	SENSOR_AVDD_1500MV,	// dvdd
	3,			// skip frame num before preview
	3,			// skip frame num before capture
	0,			// deci frame num during preview
	0,			// deci frame num during video preview

	0,
	0,
	0,
	0,
	0,
	{SENSOR_INTERFACE_TYPE_CSI2, 2, 10, 0},
	PNULL,
	3,			// skip frame num while change setting
};

LOCAL struct sensor_raw_info* Sensor_GetContext(void)
{
	return s_ov5647_mipi_raw_info_ptr;
}

LOCAL uint32_t Sensor_InitRawTuneInfo(void)
{
	uint32_t rtn=0x00;
	struct sensor_raw_info* raw_sensor_ptr=Sensor_GetContext();
	struct sensor_raw_tune_info* sensor_ptr=raw_sensor_ptr->tune_ptr;

	raw_sensor_ptr->version_info->version_id=0x00000000;
	raw_sensor_ptr->version_info->srtuct_size=sizeof(struct sensor_raw_info);

	//bypass
	sensor_ptr->version_id=0x00000000;
	sensor_ptr->blc_bypass=0x00;
	sensor_ptr->nlc_bypass=0x01;
	sensor_ptr->lnc_bypass=0x00;
	sensor_ptr->ae_bypass=0x00;
	sensor_ptr->awb_bypass=0x00;
	sensor_ptr->bpc_bypass=0x00;
	sensor_ptr->denoise_bypass=0x00;
	sensor_ptr->grgb_bypass=0x00;
	sensor_ptr->cmc_bypass=0x00;
	sensor_ptr->gamma_bypass=0x00;
	sensor_ptr->uvdiv_bypass=0x00;
	sensor_ptr->pref_bypass=0x00;
	sensor_ptr->bright_bypass=0x00;
	sensor_ptr->contrast_bypass=0x00;
	sensor_ptr->hist_bypass=0x01;
	sensor_ptr->auto_contrast_bypass=0x01;
	sensor_ptr->af_bypass=0x00;
	sensor_ptr->edge_bypass=0x01;
	sensor_ptr->fcs_bypass=0x00;
	sensor_ptr->css_bypass=0x01;
	sensor_ptr->saturation_bypass=0x01;
	sensor_ptr->hdr_bypass=0x01;
	sensor_ptr->glb_gain_bypass=0x01;
	sensor_ptr->chn_gain_bypass=0x01;

	//blc
	sensor_ptr->blc.mode=0x00;
	sensor_ptr->blc.offset[0].r=0x0f;
	sensor_ptr->blc.offset[0].gr=0x0f;
	sensor_ptr->blc.offset[0].gb=0x0f;
	sensor_ptr->blc.offset[0].b=0x0f;
	//nlc
	sensor_ptr->nlc.r_node[0]=0;
	sensor_ptr->nlc.r_node[1]=16;
	sensor_ptr->nlc.r_node[2]=32;
	sensor_ptr->nlc.r_node[3]=64;
	sensor_ptr->nlc.r_node[4]=96;
	sensor_ptr->nlc.r_node[5]=128;
	sensor_ptr->nlc.r_node[6]=160;
	sensor_ptr->nlc.r_node[7]=192;
	sensor_ptr->nlc.r_node[8]=224;
	sensor_ptr->nlc.r_node[9]=256;
	sensor_ptr->nlc.r_node[10]=288;
	sensor_ptr->nlc.r_node[11]=320;
	sensor_ptr->nlc.r_node[12]=384;
	sensor_ptr->nlc.r_node[13]=448;
	sensor_ptr->nlc.r_node[14]=512;
	sensor_ptr->nlc.r_node[15]=576;
	sensor_ptr->nlc.r_node[16]=640;
	sensor_ptr->nlc.r_node[17]=672;
	sensor_ptr->nlc.r_node[18]=704;
	sensor_ptr->nlc.r_node[19]=736;
	sensor_ptr->nlc.r_node[20]=768;
	sensor_ptr->nlc.r_node[21]=800;
	sensor_ptr->nlc.r_node[22]=832;
	sensor_ptr->nlc.r_node[23]=864;
	sensor_ptr->nlc.r_node[24]=896;
	sensor_ptr->nlc.r_node[25]=928;
	sensor_ptr->nlc.r_node[26]=960;
	sensor_ptr->nlc.r_node[27]=992;
	sensor_ptr->nlc.r_node[28]=1023;

	sensor_ptr->nlc.g_node[0]=0;
	sensor_ptr->nlc.g_node[1]=16;
	sensor_ptr->nlc.g_node[2]=32;
	sensor_ptr->nlc.g_node[3]=64;
	sensor_ptr->nlc.g_node[4]=96;
	sensor_ptr->nlc.g_node[5]=128;
	sensor_ptr->nlc.g_node[6]=160;
	sensor_ptr->nlc.g_node[7]=192;
	sensor_ptr->nlc.g_node[8]=224;
	sensor_ptr->nlc.g_node[9]=256;
	sensor_ptr->nlc.g_node[10]=288;
	sensor_ptr->nlc.g_node[11]=320;
	sensor_ptr->nlc.g_node[12]=384;
	sensor_ptr->nlc.g_node[13]=448;
	sensor_ptr->nlc.g_node[14]=512;
	sensor_ptr->nlc.g_node[15]=576;
	sensor_ptr->nlc.g_node[16]=640;
	sensor_ptr->nlc.g_node[17]=672;
	sensor_ptr->nlc.g_node[18]=704;
	sensor_ptr->nlc.g_node[19]=736;
	sensor_ptr->nlc.g_node[20]=768;
	sensor_ptr->nlc.g_node[21]=800;
	sensor_ptr->nlc.g_node[22]=832;
	sensor_ptr->nlc.g_node[23]=864;
	sensor_ptr->nlc.g_node[24]=896;
	sensor_ptr->nlc.g_node[25]=928;
	sensor_ptr->nlc.g_node[26]=960;
	sensor_ptr->nlc.g_node[27]=992;
	sensor_ptr->nlc.g_node[28]=1023;

	sensor_ptr->nlc.b_node[0]=0;
	sensor_ptr->nlc.b_node[1]=16;
	sensor_ptr->nlc.b_node[2]=32;
	sensor_ptr->nlc.b_node[3]=64;
	sensor_ptr->nlc.b_node[4]=96;
	sensor_ptr->nlc.b_node[5]=128;
	sensor_ptr->nlc.b_node[6]=160;
	sensor_ptr->nlc.b_node[7]=192;
	sensor_ptr->nlc.b_node[8]=224;
	sensor_ptr->nlc.b_node[9]=256;
	sensor_ptr->nlc.b_node[10]=288;
	sensor_ptr->nlc.b_node[11]=320;
	sensor_ptr->nlc.b_node[12]=384;
	sensor_ptr->nlc.b_node[13]=448;
	sensor_ptr->nlc.b_node[14]=512;
	sensor_ptr->nlc.b_node[15]=576;
	sensor_ptr->nlc.b_node[16]=640;
	sensor_ptr->nlc.b_node[17]=672;
	sensor_ptr->nlc.b_node[18]=704;
	sensor_ptr->nlc.b_node[19]=736;
	sensor_ptr->nlc.b_node[20]=768;
	sensor_ptr->nlc.b_node[21]=800;
	sensor_ptr->nlc.b_node[22]=832;
	sensor_ptr->nlc.b_node[23]=864;
	sensor_ptr->nlc.b_node[24]=896;
	sensor_ptr->nlc.b_node[25]=928;
	sensor_ptr->nlc.b_node[26]=960;
	sensor_ptr->nlc.b_node[27]=992;
	sensor_ptr->nlc.b_node[28]=1023;

	sensor_ptr->nlc.l_node[0]=0;
	sensor_ptr->nlc.l_node[1]=16;
	sensor_ptr->nlc.l_node[2]=32;
	sensor_ptr->nlc.l_node[3]=64;
	sensor_ptr->nlc.l_node[4]=96;
	sensor_ptr->nlc.l_node[5]=128;
	sensor_ptr->nlc.l_node[6]=160;
	sensor_ptr->nlc.l_node[7]=192;
	sensor_ptr->nlc.l_node[8]=224;
	sensor_ptr->nlc.l_node[9]=256;
	sensor_ptr->nlc.l_node[10]=288;
	sensor_ptr->nlc.l_node[11]=320;
	sensor_ptr->nlc.l_node[12]=384;
	sensor_ptr->nlc.l_node[13]=448;
	sensor_ptr->nlc.l_node[14]=512;
	sensor_ptr->nlc.l_node[15]=576;
	sensor_ptr->nlc.l_node[16]=640;
	sensor_ptr->nlc.l_node[17]=672;
	sensor_ptr->nlc.l_node[18]=704;
	sensor_ptr->nlc.l_node[19]=736;
	sensor_ptr->nlc.l_node[20]=768;
	sensor_ptr->nlc.l_node[21]=800;
	sensor_ptr->nlc.l_node[22]=832;
	sensor_ptr->nlc.l_node[23]=864;
	sensor_ptr->nlc.l_node[24]=896;
	sensor_ptr->nlc.l_node[25]=928;
	sensor_ptr->nlc.l_node[26]=960;
	sensor_ptr->nlc.l_node[27]=992;
	sensor_ptr->nlc.l_node[28]=1023;

	//ae
	sensor_ptr->ae.skip_frame=0x01;
	sensor_ptr->ae.normal_fix_fps=0;
	sensor_ptr->ae.night_fix_fps=0;
	sensor_ptr->ae.video_fps=0x1e;
	sensor_ptr->ae.target_lum=60;
	sensor_ptr->ae.target_zone=8;
	sensor_ptr->ae.quick_mode=1;
	sensor_ptr->ae.smart=0;
	sensor_ptr->ae.smart_rotio=255;
	sensor_ptr->ae.ev[0]=0xe8;
	sensor_ptr->ae.ev[1]=0xf0;
	sensor_ptr->ae.ev[2]=0xf8;
	sensor_ptr->ae.ev[3]=0x00;
	sensor_ptr->ae.ev[4]=0x08;
	sensor_ptr->ae.ev[5]=0x10;
	sensor_ptr->ae.ev[6]=0x18;
	sensor_ptr->ae.ev[7]=0x00;
	sensor_ptr->ae.ev[8]=0x00;
	sensor_ptr->ae.ev[9]=0x00;
	sensor_ptr->ae.ev[10]=0x00;
	sensor_ptr->ae.ev[11]=0x00;
	sensor_ptr->ae.ev[12]=0x00;
	sensor_ptr->ae.ev[13]=0x00;
	sensor_ptr->ae.ev[14]=0x00;
	sensor_ptr->ae.ev[15]=0x00;

	//awb
	sensor_ptr->awb.win_start.x=0x00;
	sensor_ptr->awb.win_start.y=0x00;
	sensor_ptr->awb.win_size.w=40;
	sensor_ptr->awb.win_size.h=30;
	sensor_ptr->awb.r_gain[0]=0x1b0;
	sensor_ptr->awb.g_gain[0]=0xff;
	sensor_ptr->awb.b_gain[0]=0x180;
	sensor_ptr->awb.r_gain[1]=0x120;
	sensor_ptr->awb.g_gain[1]=0xff;
	sensor_ptr->awb.b_gain[1]=0x300;
	sensor_ptr->awb.r_gain[2]=0xff;
	sensor_ptr->awb.g_gain[2]=0xff;
	sensor_ptr->awb.b_gain[2]=0xff;
	sensor_ptr->awb.r_gain[3]=0xff;
	sensor_ptr->awb.g_gain[3]=0xff;
	sensor_ptr->awb.b_gain[3]=0xff;
	sensor_ptr->awb.r_gain[4]=0x120;
	sensor_ptr->awb.g_gain[4]=0xff;
	sensor_ptr->awb.b_gain[4]=0x200;
	sensor_ptr->awb.r_gain[5]=0x1c0;
	sensor_ptr->awb.g_gain[5]=0xff;
	sensor_ptr->awb.b_gain[5]=0x140;
	sensor_ptr->awb.r_gain[6]=0x280;
	sensor_ptr->awb.g_gain[6]=0xff;
	sensor_ptr->awb.b_gain[6]=0x130;
	sensor_ptr->awb.r_gain[7]=0xff;
	sensor_ptr->awb.g_gain[7]=0xff;
	sensor_ptr->awb.b_gain[7]=0xff;
	sensor_ptr->awb.r_gain[8]=0xff;
	sensor_ptr->awb.g_gain[8]=0xff;
	sensor_ptr->awb.b_gain[8]=0xff;
	sensor_ptr->awb.target_zone=0x40;

	/*awb cali*/
	sensor_ptr->awb.quick_mode=1;

	/*awb win*/
	sensor_ptr->awb.win[0].x=117;
	sensor_ptr->awb.win[0].yt=234;
	sensor_ptr->awb.win[0].yb=210;

	sensor_ptr->awb.win[1].x=118;
	sensor_ptr->awb.win[1].yt=257;
	sensor_ptr->awb.win[1].yb=180;

	sensor_ptr->awb.win[2].x=127;
	sensor_ptr->awb.win[2].yt=265;
	sensor_ptr->awb.win[2].yb=158;

	sensor_ptr->awb.win[3].x=151;
	sensor_ptr->awb.win[3].yt=272;
	sensor_ptr->awb.win[3].yb=130;

	sensor_ptr->awb.win[4].x=167;
	sensor_ptr->awb.win[4].yt=258;
	sensor_ptr->awb.win[4].yb=121;

	sensor_ptr->awb.win[5].x=170;
	sensor_ptr->awb.win[5].yt=230;
	sensor_ptr->awb.win[5].yb=120;

	sensor_ptr->awb.win[6].x=173;
	sensor_ptr->awb.win[6].yt=194;
	sensor_ptr->awb.win[6].yb=122;

	sensor_ptr->awb.win[7].x=178;
	sensor_ptr->awb.win[7].yt=182;
	sensor_ptr->awb.win[7].yb=125;

	sensor_ptr->awb.win[8].x=186;
	sensor_ptr->awb.win[8].yt=173;
	sensor_ptr->awb.win[8].yb=131;

	sensor_ptr->awb.win[9].x=195;
	sensor_ptr->awb.win[9].yt=175;
	sensor_ptr->awb.win[9].yb=137;

	sensor_ptr->awb.win[10].x=203;
	sensor_ptr->awb.win[10].yt=174;
	sensor_ptr->awb.win[10].yb=143;

	sensor_ptr->awb.win[11].x=211;
	sensor_ptr->awb.win[11].yt=172;
	sensor_ptr->awb.win[11].yb=143;

	sensor_ptr->awb.win[12].x=219;
	sensor_ptr->awb.win[12].yt=169;
	sensor_ptr->awb.win[12].yb=136;

	sensor_ptr->awb.win[13].x=225;
	sensor_ptr->awb.win[13].yt=163;
	sensor_ptr->awb.win[13].yb=122;

	sensor_ptr->awb.win[14].x=240;
	sensor_ptr->awb.win[14].yt=151;
	sensor_ptr->awb.win[14].yb=109;

	sensor_ptr->awb.win[15].x=253;
	sensor_ptr->awb.win[15].yt=145;
	sensor_ptr->awb.win[15].yb=102;

	sensor_ptr->awb.win[16].x=266;
	sensor_ptr->awb.win[16].yt=141;
	sensor_ptr->awb.win[16].yb=95;

	sensor_ptr->awb.win[17].x=278;
	sensor_ptr->awb.win[17].yt=134;
	sensor_ptr->awb.win[17].yb=89;

	sensor_ptr->awb.win[18].x=289;
	sensor_ptr->awb.win[18].yt=125;
	sensor_ptr->awb.win[18].yb=87;

	sensor_ptr->awb.win[19].x=310;
	sensor_ptr->awb.win[19].yt=113;
	sensor_ptr->awb.win[19].yb=93;

	//bpc
	sensor_ptr->bpc.flat_thr=80;
	sensor_ptr->bpc.std_thr=20;
	sensor_ptr->bpc.texture_thr=2;

	// denoise
	sensor_ptr->denoise.write_back=0x02;
	sensor_ptr->denoise.r_thr=0x08;
	sensor_ptr->denoise.g_thr=0x08;
	sensor_ptr->denoise.b_thr=0x08;

	sensor_ptr->denoise.diswei[0]=255;
	sensor_ptr->denoise.diswei[1]=253;
	sensor_ptr->denoise.diswei[2]=251;
	sensor_ptr->denoise.diswei[3]=249;
	sensor_ptr->denoise.diswei[4]=247;
	sensor_ptr->denoise.diswei[5]=245;
	sensor_ptr->denoise.diswei[6]=243;
	sensor_ptr->denoise.diswei[7]=241;
	sensor_ptr->denoise.diswei[8]=239;
	sensor_ptr->denoise.diswei[9]=237;
	sensor_ptr->denoise.diswei[10]=235;
	sensor_ptr->denoise.diswei[11]=234;
	sensor_ptr->denoise.diswei[12]=232;
	sensor_ptr->denoise.diswei[13]=230;
	sensor_ptr->denoise.diswei[14]=228;
	sensor_ptr->denoise.diswei[15]=226;
	sensor_ptr->denoise.diswei[16]=225;
	sensor_ptr->denoise.diswei[17]=223;
	sensor_ptr->denoise.diswei[18]=221;

	sensor_ptr->denoise.ranwei[0]=255;
	sensor_ptr->denoise.ranwei[1]=252;
	sensor_ptr->denoise.ranwei[2]=243;
	sensor_ptr->denoise.ranwei[3]=230;
	sensor_ptr->denoise.ranwei[4]=213;
	sensor_ptr->denoise.ranwei[5]=193;
	sensor_ptr->denoise.ranwei[6]=170;
	sensor_ptr->denoise.ranwei[7]=147;
	sensor_ptr->denoise.ranwei[8]=125;
	sensor_ptr->denoise.ranwei[9]=103;
	sensor_ptr->denoise.ranwei[10]=83;
	sensor_ptr->denoise.ranwei[11]=66;
	sensor_ptr->denoise.ranwei[12]=51;
	sensor_ptr->denoise.ranwei[13]=38;
	sensor_ptr->denoise.ranwei[14]=28;
	sensor_ptr->denoise.ranwei[15]=20;
	sensor_ptr->denoise.ranwei[16]=14;
	sensor_ptr->denoise.ranwei[17]=10;
	sensor_ptr->denoise.ranwei[18]=6;
	sensor_ptr->denoise.ranwei[19]=4;
	sensor_ptr->denoise.ranwei[20]=2;
	sensor_ptr->denoise.ranwei[21]=1;
	sensor_ptr->denoise.ranwei[22]=0;
	sensor_ptr->denoise.ranwei[23]=0;
	sensor_ptr->denoise.ranwei[24]=0;
	sensor_ptr->denoise.ranwei[25]=0;
	sensor_ptr->denoise.ranwei[26]=0;
	sensor_ptr->denoise.ranwei[27]=0;
	sensor_ptr->denoise.ranwei[28]=0;
	sensor_ptr->denoise.ranwei[29]=0;
	sensor_ptr->denoise.ranwei[30]=0;

	//GrGb
	sensor_ptr->grgb.edge_thr=26;
	sensor_ptr->grgb.diff_thr=80;
	//cfa
	sensor_ptr->cfa.edge_thr=0x1a;
	sensor_ptr->cfa.diff_thr=0x00;
	//cmc
	sensor_ptr->cmc.matrix[0][0]=0x6cf;
	sensor_ptr->cmc.matrix[0][1]=0x3d4c;
	sensor_ptr->cmc.matrix[0][2]=0x3fe6;
	sensor_ptr->cmc.matrix[0][3]=0x3e87;
	sensor_ptr->cmc.matrix[0][4]=0x582;
	sensor_ptr->cmc.matrix[0][5]=0x3ff7;
	sensor_ptr->cmc.matrix[0][6]=0x3ff4;
	sensor_ptr->cmc.matrix[0][7]=0x3dba;
	sensor_ptr->cmc.matrix[0][8]=0x653;

	//Gamma
	sensor_ptr->gamma.axis[0][0]=0;
	sensor_ptr->gamma.axis[0][1]=8;
	sensor_ptr->gamma.axis[0][2]=16;
	sensor_ptr->gamma.axis[0][3]=24;
	sensor_ptr->gamma.axis[0][4]=32;
	sensor_ptr->gamma.axis[0][5]=48;
	sensor_ptr->gamma.axis[0][6]=64;
	sensor_ptr->gamma.axis[0][7]=80;
	sensor_ptr->gamma.axis[0][8]=96;
	sensor_ptr->gamma.axis[0][9]=128;
	sensor_ptr->gamma.axis[0][10]=160;
	sensor_ptr->gamma.axis[0][11]=192;
	sensor_ptr->gamma.axis[0][12]=224;
	sensor_ptr->gamma.axis[0][13]=256;
	sensor_ptr->gamma.axis[0][14]=288;
	sensor_ptr->gamma.axis[0][15]=320;
	sensor_ptr->gamma.axis[0][16]=384;
	sensor_ptr->gamma.axis[0][17]=448;
	sensor_ptr->gamma.axis[0][18]=512;
	sensor_ptr->gamma.axis[0][19]=576;
	sensor_ptr->gamma.axis[0][20]=640;
	sensor_ptr->gamma.axis[0][21]=768;
	sensor_ptr->gamma.axis[0][22]=832;
	sensor_ptr->gamma.axis[0][23]=896;
	sensor_ptr->gamma.axis[0][24]=960;
	sensor_ptr->gamma.axis[0][25]=1023;

	sensor_ptr->gamma.axis[1][0]=0x00;
	sensor_ptr->gamma.axis[1][1]=0x05;
	sensor_ptr->gamma.axis[1][2]=0x09;
	sensor_ptr->gamma.axis[1][3]=0x0e;
	sensor_ptr->gamma.axis[1][4]=0x13;
	sensor_ptr->gamma.axis[1][5]=0x1f;
	sensor_ptr->gamma.axis[1][6]=0x2a;
	sensor_ptr->gamma.axis[1][7]=0x36;
	sensor_ptr->gamma.axis[1][8]=0x40;
	sensor_ptr->gamma.axis[1][9]=0x58;
	sensor_ptr->gamma.axis[1][10]=0x68;
	sensor_ptr->gamma.axis[1][11]=0x76;
	sensor_ptr->gamma.axis[1][12]=0x84;
	sensor_ptr->gamma.axis[1][13]=0x8f;
	sensor_ptr->gamma.axis[1][14]=0x98;
	sensor_ptr->gamma.axis[1][15]=0xa0;
	sensor_ptr->gamma.axis[1][16]=0xb0;
	sensor_ptr->gamma.axis[1][17]=0xbd;
	sensor_ptr->gamma.axis[1][18]=0xc6;
	sensor_ptr->gamma.axis[1][19]=0xcf;
	sensor_ptr->gamma.axis[1][20]=0xd8;
	sensor_ptr->gamma.axis[1][21]=0xe4;
	sensor_ptr->gamma.axis[1][22]=0xea;
	sensor_ptr->gamma.axis[1][23]=0xf0;
	sensor_ptr->gamma.axis[1][24]=0xf6;
	sensor_ptr->gamma.axis[1][25]=0xff;

	//uv div
	sensor_ptr->uv_div.thrd[0]=220;
	sensor_ptr->uv_div.thrd[1]=212;
	sensor_ptr->uv_div.thrd[2]=204;
	sensor_ptr->uv_div.thrd[3]=196;
	sensor_ptr->uv_div.thrd[4]=188;
	sensor_ptr->uv_div.thrd[5]=180;
	sensor_ptr->uv_div.thrd[6]=172;

	//pref
	sensor_ptr->pref.write_back=0x00;
	sensor_ptr->pref.y_thr=0x04;
	sensor_ptr->pref.u_thr=0x04;
	sensor_ptr->pref.v_thr=0x04;
	//bright
	sensor_ptr->bright.factor[0]=0xd0;
	sensor_ptr->bright.factor[1]=0xe0;
	sensor_ptr->bright.factor[2]=0xf0;
	sensor_ptr->bright.factor[3]=0x00;
	sensor_ptr->bright.factor[4]=0x10;
	sensor_ptr->bright.factor[5]=0x20;
	sensor_ptr->bright.factor[6]=0x30;
	sensor_ptr->bright.factor[7]=0x00;
	sensor_ptr->bright.factor[8]=0x00;
	sensor_ptr->bright.factor[9]=0x00;
	sensor_ptr->bright.factor[10]=0x00;
	sensor_ptr->bright.factor[11]=0x00;
	sensor_ptr->bright.factor[12]=0x00;
	sensor_ptr->bright.factor[13]=0x00;
	sensor_ptr->bright.factor[14]=0x00;
	sensor_ptr->bright.factor[15]=0x00;
	//contrast
	sensor_ptr->contrast.factor[0]=0x10;
	sensor_ptr->contrast.factor[1]=0x20;
	sensor_ptr->contrast.factor[2]=0x30;
	sensor_ptr->contrast.factor[3]=0x40;
	sensor_ptr->contrast.factor[4]=0x50;
	sensor_ptr->contrast.factor[5]=0x60;
	sensor_ptr->contrast.factor[6]=0x70;
	sensor_ptr->contrast.factor[7]=0x40;
	sensor_ptr->contrast.factor[8]=0x40;
	sensor_ptr->contrast.factor[9]=0x40;
	sensor_ptr->contrast.factor[10]=0x40;
	sensor_ptr->contrast.factor[11]=0x40;
	sensor_ptr->contrast.factor[12]=0x40;
	sensor_ptr->contrast.factor[13]=0x40;
	sensor_ptr->contrast.factor[14]=0x40;
	sensor_ptr->contrast.factor[15]=0x40;
	//hist
	sensor_ptr->hist.mode;
	sensor_ptr->hist.low_ratio;
	sensor_ptr->hist.high_ratio;
	//auto contrast
	sensor_ptr->auto_contrast.mode;
	//saturation
	sensor_ptr->saturation.factor[0]=0x40;
	sensor_ptr->saturation.factor[1]=0x40;
	sensor_ptr->saturation.factor[2]=0x40;
	sensor_ptr->saturation.factor[3]=0x40;
	sensor_ptr->saturation.factor[4]=0x40;
	sensor_ptr->saturation.factor[5]=0x40;
	sensor_ptr->saturation.factor[6]=0x40;
	sensor_ptr->saturation.factor[7]=0x40;
	sensor_ptr->saturation.factor[8]=0x40;
	sensor_ptr->saturation.factor[9]=0x40;
	sensor_ptr->saturation.factor[10]=0x40;
	sensor_ptr->saturation.factor[11]=0x40;
	sensor_ptr->saturation.factor[12]=0x40;
	sensor_ptr->saturation.factor[13]=0x40;
	sensor_ptr->saturation.factor[14]=0x40;
	sensor_ptr->saturation.factor[15]=0x40;

	//af info
	sensor_ptr->af.max_step=1024;
	sensor_ptr->af.stab_period=10;

	//edge
	sensor_ptr->edge.info[0].detail_thr=0x03;
	sensor_ptr->edge.info[0].smooth_thr=0x05;
	sensor_ptr->edge.info[0].strength=10;
	sensor_ptr->edge.info[1].detail_thr=0x03;
	sensor_ptr->edge.info[1].smooth_thr=0x05;
	sensor_ptr->edge.info[1].strength=10;
	sensor_ptr->edge.info[2].detail_thr=0x03;
	sensor_ptr->edge.info[2].smooth_thr=0x05;
	sensor_ptr->edge.info[2].strength=10;
	sensor_ptr->edge.info[3].detail_thr=0x03;
	sensor_ptr->edge.info[3].smooth_thr=0x05;
	sensor_ptr->edge.info[3].strength=10;
	sensor_ptr->edge.info[4].detail_thr=0x03;
	sensor_ptr->edge.info[4].smooth_thr=0x05;
	sensor_ptr->edge.info[4].strength=10;
	sensor_ptr->edge.info[5].detail_thr=0x03;
	sensor_ptr->edge.info[5].smooth_thr=0x05;
	sensor_ptr->edge.info[5].strength=10;

	//emboss
	sensor_ptr->emboss.step=0x00;
	//global gain
	sensor_ptr->global.gain=0x40;
	//chn gain
	sensor_ptr->chn.r_gain=0x40;
	sensor_ptr->chn.g_gain=0x40;
	sensor_ptr->chn.b_gain=0x40;
	sensor_ptr->chn.r_offset=0x00;
	sensor_ptr->chn.r_offset=0x00;
	sensor_ptr->chn.r_offset=0x00;

	return rtn;
}


LOCAL uint32_t _ov5647_GetResolutionTrimTab(uint32_t param)
{
	SENSOR_PRINT("0x%x", (uint32_t)s_ov5647_Resolution_Trim_Tab);
	return (uint32_t) s_ov5647_Resolution_Trim_Tab;
}
LOCAL uint32_t _ov5647_PowerOn(uint32_t power_on)
{
	SENSOR_AVDD_VAL_E dvdd_val = g_ov5647_mipi_raw_info.dvdd_val;
	SENSOR_AVDD_VAL_E avdd_val = g_ov5647_mipi_raw_info.avdd_val;
	SENSOR_AVDD_VAL_E iovdd_val = g_ov5647_mipi_raw_info.iovdd_val;
	BOOLEAN power_down = g_ov5647_mipi_raw_info.power_down_level;
	BOOLEAN reset_level = g_ov5647_mipi_raw_info.reset_pulse_level;
	//uint32_t reset_width=g_ov5647_yuv_info.reset_pulse_width;

	if (SENSOR_TRUE == power_on) {
		Sensor_PowerDown(power_on);//active is lowlevel
		usleep(12*1000);
		// Open power
		Sensor_SetMonitorVoltage(SENSOR_AVDD_2800MV);
		Sensor_SetVoltage(dvdd_val, avdd_val, iovdd_val);
		usleep(20*1000);
		//_dw9174_SRCInit(2);
		Sensor_SetMCLK(SENSOR_DEFALUT_MCLK);
		usleep(10*1000);
		Sensor_PowerDown(!power_down);
		// Reset sensor
		Sensor_Reset(reset_level);

	} else {
		Sensor_PowerDown(power_on);
		Sensor_SetMCLK(SENSOR_DISABLE_MCLK);
		Sensor_SetVoltage(SENSOR_AVDD_CLOSED, SENSOR_AVDD_CLOSED, SENSOR_AVDD_CLOSED);
		Sensor_SetMonitorVoltage(SENSOR_AVDD_CLOSED);

	}
	SENSOR_PRINT("SENSOR_OV5647: _ov5647_Power_On(1:on, 0:off): %d", power_on);
	return SENSOR_SUCCESS;
}

LOCAL uint32_t _ov5647_Identify(uint32_t param)
{
#define ov5647_PID_VALUE    0x56
#define ov5647_PID_ADDR     0x300A
#define ov5647_VER_VALUE    0x47
#define ov5647_VER_ADDR     0x300B

	uint8_t pid_value = 0x00;
	uint8_t ver_value = 0x00;
	uint32_t ret_value = SENSOR_FAIL;

	SENSOR_PRINT("SENSOR_OV5647: mipi raw identify\n");

	pid_value = Sensor_ReadReg(ov5647_PID_ADDR);

	if (ov5647_PID_VALUE == pid_value) {
		ver_value = Sensor_ReadReg(ov5647_VER_ADDR);
		SENSOR_PRINT("SENSOR_OV5647: Identify: PID = %x, VER = %x", pid_value, ver_value);
		if (ov5647_VER_VALUE == ver_value) {
			Sensor_InitRawTuneInfo();
			ret_value = SENSOR_SUCCESS;
			SENSOR_PRINT("SENSOR_OV5647: this is ov5640 sensor !");
		} else {
			SENSOR_PRINT
			    ("SENSOR_OV5647: Identify this is OV%x%x sensor !", pid_value, ver_value);
		}
	} else {
		SENSOR_PRINT("SENSOR_OV5647: identify fail,pid_value=%d", pid_value);
	}

	return ret_value;
}

LOCAL uint32_t _ov5647_write_exposure(uint32_t param)
{
	uint32_t ret_value = SENSOR_SUCCESS;
	uint16_t expsure_line=0x00;
	uint16_t dummy_line=0x00;
	uint16_t frame_len=0x00;
	uint16_t frame_len_cur=0x00;
	uint16_t value=0x00;
	uint16_t value0=0x00;
	uint16_t value1=0x00;
	uint16_t value2=0x00;

	expsure_line=param&0xffff;
	dummy_line=(param>>0x10)&0xffff;

	SENSOR_PRINT("SENSOR_OV5647: write_exposure line:%d, dummy:%d", expsure_line, dummy_line);
	frame_len = ((expsure_line+4)> OV5647_MIN_FRAME_LEN_PRV) ? (expsure_line+4) : OV5647_MIN_FRAME_LEN_PRV;

	frame_len_cur = (Sensor_ReadReg(0x380e)&0xff)<<8;
	frame_len_cur |= Sensor_ReadReg(0x380f)&0xff;

	if(frame_len_cur != frame_len){
		value=(frame_len)&0xff;
		ret_value = Sensor_WriteReg(0x380f, value);
		value=(frame_len>>0x08)&0xff;
		ret_value = Sensor_WriteReg(0x380e, value);
	}

	value=(expsure_line<<0x04)&0xff;
	ret_value = Sensor_WriteReg(0x3502, value);
	value=(expsure_line>>0x04)&0xff;
	ret_value = Sensor_WriteReg(0x3501, value);
	value=(expsure_line>>0x0c)&0x0f;
	ret_value = Sensor_WriteReg(0x3500, value);

	return ret_value;
}

LOCAL uint32_t _ov5647_write_gain(uint32_t param)
{
	uint32_t ret_value = SENSOR_SUCCESS;
	uint16_t value=0x00;
	uint32_t real_gain = 0;

	real_gain = ((param&0xf)+16)*(((param>>4)&0x01)+1)*(((param>>5)&0x01)+1)*(((param>>6)&0x01)+1)*(((param>>7)&0x01)+1);
	real_gain = real_gain*(((param>>8)&0x01)+1)*(((param>>9)&0x01)+1)*(((param>>10)&0x01)+1)*(((param>>11)&0x01)+1);

	SENSOR_PRINT("SENSOR_OV5647: real_gain:0x%x, param: 0x%x", real_gain, param);

	value = real_gain&0xff;
	ret_value = Sensor_WriteReg(0x350b, value);/*0-7*/
	value = (real_gain>>0x08)&0x03;
	ret_value = Sensor_WriteReg(0x350a, value);/*8-9*/

	return ret_value;
}


LOCAL uint32_t _ov5647_write_af(uint32_t param)
{
#define DW9714_VCM_SLAVE_ADDR (0x18>>1)

	uint32_t ret_value = SENSOR_SUCCESS;
	uint8_t cmd_val[2] = {0x00};
	uint16_t  slave_addr = 0;
	uint16_t cmd_len = 0;

	SENSOR_PRINT("SENSOR_OV5647: _write_af %d", param);

	slave_addr = DW9714_VCM_SLAVE_ADDR;
	cmd_val[0] = (param&0xfff0)>>4;
	cmd_val[1] = ((param&0x0f)<<4)|0x09;
	cmd_len = 2;
	ret_value = Sensor_WriteI2C(slave_addr,(uint8_t*)&cmd_val[0], cmd_len);

	SENSOR_PRINT("SENSOR_OV5647: _write_af, ret =  %d, MSL:%x, LSL:%x\n", ret_value, cmd_val[0], cmd_val[1]);

	return ret_value;
}

LOCAL uint32_t _ov5647_ReadGain(uint32_t*  gain_ptr)
{
	uint32_t rtn = SENSOR_SUCCESS;
	uint16_t value=0x00;
	uint32_t gain = 0;

	value = Sensor_ReadReg(0x350b);/*0-7*/
	gain = value&0xff;
	value = Sensor_ReadReg(0x350a);/*8*/
	gain |= (value<<0x08)&0x300;

	s_ov5647_gain=gain;
	if (gain_ptr) {
		*gain_ptr = gain;
	}

	SENSOR_PRINT("SENSOR: _ov5647_ReadGain gain: 0x%x", s_ov5647_gain);

	return rtn;
}

LOCAL uint32_t _ov5647_SetEV(uint32_t param)
{
	uint32_t rtn = SENSOR_SUCCESS;
	SENSOR_EXT_FUN_T_PTR ext_ptr = (SENSOR_EXT_FUN_T_PTR) param;
	uint16_t value=0x00;
	uint32_t gain = s_ov5647_gain;
	uint32_t ev = ext_ptr->param;

	SENSOR_PRINT("SENSOR: _ov5647_SetEV param: 0x%x", ev);

	gain=(gain*ext_ptr->param)>>0x06;

	value = gain&0xff;
	Sensor_WriteReg(0x350b, value);/*0-7*/
	value = (gain>>0x08)&0x03;
	Sensor_WriteReg(0x350a, value);/*8-9*/

	return rtn;
}

LOCAL uint32_t _ov5647_ExtFunc(uint32_t ctl_param)
{
	uint32_t rtn = SENSOR_SUCCESS;
	SENSOR_EXT_FUN_PARAM_T_PTR ext_ptr = (SENSOR_EXT_FUN_PARAM_T_PTR) ctl_param;

	switch (ext_ptr->cmd) {
		//case SENSOR_EXT_EV:
		case 10:
			rtn = _ov5647_SetEV(ctl_param);
			break;
		default:
			break;
	}

	return rtn;
}

LOCAL uint32_t _ov5647_BeforeSnapshot(uint32_t param)
{
	uint8_t ret_l, ret_m, ret_h;
	uint32_t capture_exposure, preview_maxline;
	uint32_t capture_maxline, preview_exposure;
	uint32_t gain = 0, value = 0;
	uint32_t prv_linetime=s_ov5647_Resolution_Trim_Tab[SENSOR_MODE_PREVIEW_ONE].line_time;
	uint32_t cap_linetime = s_ov5647_Resolution_Trim_Tab[(param&0xffff)].line_time;
	param = param & 0xffff;

	SENSOR_PRINT("SENSOR_OV5647: BeforeSnapshot moe: %d",param);

	if (SENSOR_MODE_PREVIEW_ONE >= param){
		_ov5647_ReadGain(0x00);
		SENSOR_PRINT("SENSOR_OV5647: prvmode equal to capmode");
		return SENSOR_SUCCESS;
	}

	ret_h = (uint8_t) Sensor_ReadReg(0x3500);
	ret_m = (uint8_t) Sensor_ReadReg(0x3501);
	ret_l = (uint8_t) Sensor_ReadReg(0x3502);
	preview_exposure = ((ret_h&0x0f) << 12) + (ret_m << 4) + ((ret_l >> 4)&0x0f);

	ret_h = (uint8_t) Sensor_ReadReg(0x380e);
	ret_l = (uint8_t) Sensor_ReadReg(0x380f);
	preview_maxline = (ret_h << 8) + ret_l;

	_ov5647_ReadGain(&gain);
	Sensor_SetMode(param);

	if (prv_linetime == cap_linetime) {
		SENSOR_PRINT("SENSOR_OV5647: prvline equal to capline");
		return SENSOR_SUCCESS;
	}

	ret_h = (uint8_t) Sensor_ReadReg(0x380e);
	ret_l = (uint8_t) Sensor_ReadReg(0x380f);
	capture_maxline = (ret_h << 8) + ret_l;
	capture_exposure = preview_exposure *prv_linetime / cap_linetime ;

	if(0 == capture_exposure){
		capture_exposure = 1;
	}

	capture_exposure = capture_exposure *2;
	gain=gain/2;

	gain=gain*360/300;


	if(capture_exposure > (capture_maxline - 4)){
		capture_maxline = capture_exposure + 4;
		ret_l = (unsigned char)(capture_maxline&0x0ff);
		ret_h = (unsigned char)((capture_maxline >> 8)&0xff);
		Sensor_WriteReg(0x380e, ret_h);
		Sensor_WriteReg(0x380f, ret_l);
	}
	ret_l = (unsigned char)((capture_exposure&0x0f) << 4);
	ret_m = (unsigned char)((capture_exposure&0xfff) >> 4);
	ret_h = (unsigned char)(capture_exposure >> 12);

	Sensor_WriteReg(0x3502, ret_l);
	Sensor_WriteReg(0x3501, ret_m);
	Sensor_WriteReg(0x3500, ret_h);

	value = gain&0xff;
	Sensor_WriteReg(0x350b, value);/*0-7*/
	value = (gain>>0x08)&0x03;
	Sensor_WriteReg(0x350a, value);/*8-9*/
	s_ov5647_gain = gain;

	Sensor_SetSensorExifInfo(SENSOR_EXIF_CTRL_EXPOSURETIME, capture_exposure);

	return SENSOR_SUCCESS;
}

LOCAL uint32_t _ov5647_after_snapshot(uint32_t param)
{
	SENSOR_PRINT("SENSOR_OV5647: after_snapshot mode:%d", param);
	Sensor_SetMode(param);
	return SENSOR_SUCCESS;
}

LOCAL uint32_t _ov5647_flash(uint32_t param)
{
	SENSOR_PRINT("Start:param=%d", param);

	/* enable flash, disable in _ov5647_BeforeSnapshot */
	g_flash_mode_en = param;
	Sensor_SetFlash(param);
	SENSOR_PRINT_HIGH("end");
	return SENSOR_SUCCESS;
}

LOCAL uint32_t _ov5647_StreamOn(uint32_t param)
{
	SENSOR_PRINT("SENSOR_OV5647: StreamOn");

	Sensor_WriteReg(0x0100, 0x01);
	return 0;
}

LOCAL uint32_t _ov5647_StreamOff(uint32_t param)
{
	SENSOR_PRINT("SENSOR_OV5647: StreamOff");

	Sensor_WriteReg(0x0100, 0x00);

	return 0;
}

LOCAL uint32_t _dw9174_SRCInit(uint32_t mode)
{
#define DW9714_VCM_SLAVE_ADDR (0x18>>1)

	uint8_t cmd_val[6] = {0x00};
	uint16_t  slave_addr = 0;
	uint16_t cmd_len = 0;
	uint32_t ret_value = SENSOR_SUCCESS;
	slave_addr = DW9714_VCM_SLAVE_ADDR;

	switch (mode) {
		case 1:
		break;

		case 2:
		{
			cmd_val[0] = 0xec;
			cmd_val[1] = 0xa3;
			cmd_val[2] = 0xf2;
			cmd_val[3] = 0x00;
			cmd_val[4] = 0xdc;
			cmd_val[5] = 0x51;
			cmd_len = 6;
			Sensor_WriteI2C(slave_addr, (uint8_t*)&cmd_val[0], cmd_len);
		}
		break;

		case 3:
		break;

	}

	return ret_value;
}
