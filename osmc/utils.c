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

char* fullFileName(const char* name, const char* directory) {
    char* fullFileName = calloc(sizeof(char), strlen(name) + strlen(directory) + 2);
    strcpy(fullFileName, directory);
    strcat(fullFileName, "/");
    strcat(fullFileName, name);
    return fullFileName;
}

FILE* openFile(const char* name, const char* outputDirectory, const char* mode) {
    char* fullName = fullFileName(name, outputDirectory);
    FILE* result = fopen(fullName, mode);
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
