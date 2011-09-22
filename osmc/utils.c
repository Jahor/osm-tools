/*
 *  utils.c
 *  OSMapper
 *
 *  Created by Egor Leonenko on 9.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>
#include <zlib.h>

char* fullFileNameInternal(const char* name, const char* directory, char compress) {
    char* fullFileName = calloc(sizeof(char), strlen(name) + strlen(directory) + (compress == DO_COMPRESS ? 3 : 0) + 2);
    strcpy(fullFileName, directory);
    strcat(fullFileName, "/");
    strcat(fullFileName, name);
	if (compress == DO_COMPRESS) {
		strcat(fullFileName, ".gz");
	}
    return fullFileName;
}

char* fullFileName(const char* name, const char* directory) {
	return fullFileNameInternal(name, directory, NO_COMPRESS);
}

int fwriter (FILE* file, void* buf, unsigned len) {
	return fwrite(buf, len, 1, file);
}

int freader (FILE* file, void* buf, unsigned len) {
	return fread(buf, len, 1, file);
}

WriteCallback getWrite(char compress) {
	if (compress) {
		return (WriteCallback)gzwrite;
	} else {
		return (WriteCallback)fwriter;
	}
}

ReadCallback getRead(char compress) {
	if (compress) {
		return (ReadCallback)gzread;
	} else {
		return (ReadCallback)freader;
	}
}

CloseCallback getClose(char compress) {
	if (compress) {
		return (CloseCallback)gzclose;
	} else {
		return (CloseCallback)fclose;
	}
}

FILE* openFile(const char* name, const char* outputDirectory, const char* mode, char compressed) {
    char* fullName;
	FILE* result;
	switch (compressed) {
		case DO_COMPRESS:
			fullName = fullFileNameInternal(name, outputDirectory, DO_COMPRESS);
			result = gzopen(fullName, mode);			
			break;
		case AUTO_COMPRESS:
			fullName = fullFileNameInternal(name, outputDirectory, DO_COMPRESS);
			if (access(fullName, F_OK) != -1) {
				result = gzopen(fullName, mode);
			} else {
				free(fullName);
				fullName = fullFileNameInternal(name, outputDirectory, NO_COMPRESS);
				result = fopen(fullName, mode);
			}
			break;
		default:
			fullName = fullFileNameInternal(name, outputDirectory, NO_COMPRESS);
			result = fopen(fullName, mode);
			break;
	}
    free(fullName);
    return result;
}

void unmapFile(void * dataPtr, size_t record_size, size_t recordsCount) {
    munmap(dataPtr, record_size * recordsCount);
}

int mapFile(const char * inPathName, size_t record_size, void ** outDataPtr, size_t* outDataLength)
{
    int outError;
    int fileDescriptor;
    struct stat statInfo;
    
    // Return safe values on error.
    outError = 0;
    *outDataPtr = NULL;
    *outDataLength = 0;
    
    // Open the file.
    fileDescriptor = open(inPathName, O_RDONLY, 0 );
    if (fileDescriptor < 0) {
        outError = errno;
    } else  {
        // We now know the file exists. Retrieve the file size.
        if (fstat( fileDescriptor, &statInfo ) != 0) {
            outError = errno;
        } else  {
            // Map the file into a read-only memory region.
            *outDataPtr = mmap(NULL,
                               statInfo.st_size,
                               PROT_READ,
                               MAP_PRIVATE,
                               fileDescriptor,
                               0);
            if( *outDataPtr == MAP_FAILED )
            {
                outError = errno;
            }
            else
            {
                // On success, return the size of the mapped file.
                *outDataLength = statInfo.st_size / record_size;
            }
        }
        
        // Now close the file. The kernel doesn’t use our file descriptor.
        close( fileDescriptor );
    }
    
    return outError;
}
