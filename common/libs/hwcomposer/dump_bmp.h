#ifndef _DUMP_HWCOMPOSER_BMP_H_
#define _DUMP_HWCOMPOSER_BMP_H_

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <system/graphics.h>
#include<stdlib.h>
#include <cutils/log.h>

//LOCAL_SHARED_LIBRARIES := libcutils
#include <cutils/properties.h>
#include <hardware/hwcomposer.h>
#include "gralloc_priv.h"

#define BI_RGB          0
#define BI_BITFIELDS    3
#define MAX_DUMP_PATH_LENGTH 100
#define MAX_DUMP_FILENAME_LENGTH 100
typedef unsigned char BYTE, *PBYTE, *LPBYTE;
typedef unsigned short WORD, *PWORD, *LPWORD;
typedef unsigned long DWORD, *PDWORD, *LPDWORD;

typedef long LONG, *PLONG, *LPLONG;

typedef BYTE  U8;
typedef WORD  U16;
typedef DWORD U32;

#pragma pack()
enum {
    DUMP_AT_HWCOMPOSER_HWC_SET,
    DUMP_AT_HWCOMPOSER_HWC_PREPARE
};
typedef struct tagBITMAPFILEHEADER {
  DWORD bfSize;
  WORD  bfReserved1;
  WORD  bfReserved2;
  DWORD bfOffBits;
} BITMAPFILEHEADER;

typedef struct tagBITMAPINFOHEADER {
  DWORD biSize;
  LONG  biWidth;
  LONG  biHeight;
  WORD  biPlanes;
  WORD  biBitCount;
  DWORD biCompression;
  DWORD biSizeImage;
  LONG  biXPelsPerMeter;
  LONG  biYPelsPerMeter;
  DWORD biClrUsed;
  DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER;

typedef struct tagRGBQUAD {
    union {
        struct {
          DWORD rgbBlueMask;
          DWORD rgbGreenMask;
          DWORD rgbRedMask;
          DWORD rgbReservedMask;
        };
        struct {
          BYTE rgbBlue;
          BYTE rgbGreen;
          BYTE rgbRed;
          BYTE rgbReserved;
        } table[256];
    };
} RGBQUAD;

typedef struct tagBITMAPINFO {
  BITMAPFILEHEADER bmfHeader;
  BITMAPINFOHEADER bmiHeader;
} BITMAPINFO;
typedef enum
{
    HWCOMPOSER_DUMP_ORIGINAL_LAYERS = 0x01,
    HWCOMPOSER_DUMP_VIDEO_OVERLAY_FLAG = 0x2,
    HWCOMPOSER_DUMP_OSD_OVERLAY_FLAG = 0x4,
    HWCOMPOSER_DUMP_FRAMEBUFFER_FLAG = 0x8
} dump_type;

#endif
