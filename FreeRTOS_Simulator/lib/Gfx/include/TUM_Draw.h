/**
 * @file TUM_Draw.h
 * @author Alex Hoffman
 * @date 27 Auguest 2019
 * @brief A SDL2 based library to implement work queue based drawing of graphical
 * elements. Allows for drawing using SDL2 from multiple threads.
 *
 * @mainpage FreeRTOS Simulator Graphical Library
 */

#ifndef __TUM_DRAW_H__
#define __TUM_DRAW_H__

#include "FreeRTOS.h"
#include "semphr.h"

#define DEFAULT_FONT_SIZE  15 

#define DEFAULT_FONT        "IBMPlexSans-Medium.ttf"
#define FONTS_LOCATION       "/../resources/fonts/"
#define FONT_LOCATION       FONTS_LOCATION DEFAULT_FONT

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define ASPECT_RATIO SCREEN_WIDTH/SCREEN_HEIGHT

#define mainGENERIC_PRIORITY	( tskIDLE_PRIORITY )
#define mainGENERIC_STACK_SIZE  ( ( unsigned short ) 2560 )

#define Red     0xFF0000
#define Green   0x00FF00
#define Blue    0x0000FF
#define Yellow  0xFFFF00
#define Aqua    0x00FFFF
#define Fuchsia 0xFF00FF
#define White   0xFFFFFF
#define Black   0x000000

extern SemaphoreHandle_t DisplayReady;

typedef struct coord {
	unsigned short x;
	unsigned short y;
} coord_t;

char *tumGetErrorMessage(void);

void vInitDrawing(char *path);
void vExitDrawing(void);
void vDrawUpdateScreen(void);

signed char tumDrawClear(unsigned int colour);
signed char tumDrawEllipse(signed short x, signed short y, signed short rx,
		signed short ry, unsigned int colour);
signed char tumDrawArc(signed short x, signed short y, signed short radius,
		signed short start, signed short end, unsigned int colour);
signed char tumDrawText(char *str, signed short x, signed short y,
		unsigned int colour);
void tumGetTextSize(char *str, unsigned int *width, unsigned int *height);
signed char tumDrawBox(signed short x, signed short y, signed short w,
		signed short h, unsigned int colour);
signed char tumDrawFilledBox(signed short x, signed short y, signed short w,
		signed short h, unsigned int colour);
signed char tumDrawCircle(signed short x, signed short y, signed short radius,
		unsigned int colour);
signed char tumDrawLine(signed short x1, signed short y1, signed short x2,
		signed short y2, unsigned char thickness, unsigned int colour);
signed char tumDrawPoly(coord_t *points, int n, unsigned int colour);
signed char tumDrawTriangle(coord_t *points, unsigned int colour);
signed char tumDrawImage(char *filename, signed short x, signed short y);
signed char tumDrawArrow(unsigned short x1, unsigned short y1,
		unsigned short x2, unsigned short y2, unsigned short head_length,
		unsigned char thickness, unsigned int colour);

#endif
