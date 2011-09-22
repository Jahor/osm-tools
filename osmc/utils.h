/*
 *  utils.h
 *  OSMapper
 *
 *  Created by Egor Leonenko on 9.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdio.h>
#include <stdint.h>

typedef uint32_t Offset;
typedef uint16_t ZoomLevel;
#define OFFSET_NOT_DEFINED ((uint32_t)-1)
#define MAX_ZOOM_LEVEL 18
#define MIN_ZOOM_LEVEL 0

#define NO_COMPRESS 0
#define DO_COMPRESS 1
#define AUTO_COMPRESS 2

typedef int (*ReadCallback) (void * context, void * buffer, int len);
typedef int (*WriteCallback) (void * context, void * buffer, int len);
typedef int (*CloseCallback) (void * context);

WriteCallback getWrite(char compress);
ReadCallback getRead(char compress);
CloseCallback getClose(char compress);

char* fullFileName(const char* name, const char* directory);
FILE* openFile(const char* name, const char* outputDirectory, const char* mode, char compressed);

int mapFile(const char * inPathName, size_t record_size, void ** outDataPtr, size_t* outDataLength);
void unmapFile(void * dataPtr, size_t record_size, size_t recordsCount);

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

#endif
