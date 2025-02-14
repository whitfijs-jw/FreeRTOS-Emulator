/*
 * AsyncIOSerial.c
 *
 *  Created on: 9 Apr 2010
 *      Author: William Davy
 */

#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "AsyncIOSerial.h"
/*---------------------------------------------------------------------------*/

/* See 'man termios' for more details on configuring the serial port. */

long lAsyncIOSerialOpen( const char *pcDevice, int *piDeviceDescriptor )
{
int iSerialDevice = 0;
struct termios xTerminalSettings;
long lReturn = pdFALSE;

	/* Open the port that was passed in. */
	iSerialDevice = open( pcDevice, O_RDWR | O_NOCTTY | O_NONBLOCK );
	if ( iSerialDevice < 0 )
	{
		lReturn = pdFALSE;
	}
	else
	{
		/* Grab a copy of the current terminal settings. */
		int ret = tcgetattr( iSerialDevice, &xTerminalSettings );
		if ( 0 == ret )
		{
			/* Configure the port to the Hard-coded values. */
			cfmakeraw( &xTerminalSettings );
			cfsetspeed( &xTerminalSettings, B38400 );
			/* Set the terminal settings. */
			lReturn = ( 0 == tcsetattr(iSerialDevice, TCSANOW, &xTerminalSettings) );

			/* Pass out the device descriptor for subsequent calls to AsyncIORegisterCallback() */
			*piDeviceDescriptor = iSerialDevice;
		}
		else
			printf("tcgetattr err: %s\n", strerror(errno));
	}

	return lReturn;
}
 /*---------------------------------------------------------------------------*/

/* Define a callback function which is called when data is available. */
void vAsyncSerialIODataAvailableISR( int iFileDescriptor, void *pContext )
{
BaseType_t xHigherPriorityTaskWoken = pdFALSE;
ssize_t iReadResult = -1;
unsigned char ucRx;

	/* This handler only processes a single byte/character at a time. */
	iReadResult = read( iFileDescriptor, &ucRx, 1 );
	if ( 1 == iReadResult )
	{
		if ( NULL != pContext )
		{
			/* Send the received byte to the queue. */
			if ( pdTRUE != xQueueSendFromISR( (xQueueHandle)pContext, &ucRx, &xHigherPriorityTaskWoken ) )
			{
				/* the queue is full. */
			}
		}
	}
	portEND_SWITCHING_ISR( xHigherPriorityTaskWoken );
}
/*---------------------------------------------------------------------------*/
