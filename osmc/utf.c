/*
 *  utf.c
 *  OSMapper
 *
 *  Created by Egor Leonenko on 8.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#include "utf.h"
#include <stdlib.h>
#include <libxml/xmlstring.h>

typedef unsigned long	UTF32;	/* at least 32 bits */

static const int halfShift  = 10; /* used for shifting by 10 bits */

static const UTF32 halfBase = 0x0010000UL;
static const UTF32 halfMask = 0x3FFUL;

#define UNI_SUR_HIGH_START  (UTF32)0xD800
#define UNI_SUR_HIGH_END    (UTF32)0xDBFF
#define UNI_SUR_LOW_START   (UTF32)0xDC00
#define UNI_SUR_LOW_END     (UTF32)0xDFFF

#define UNI_REPLACEMENT_CHAR (UTF32)0x0000FFFD
#define UNI_MAX_BMP (UTF32)0x0000FFFF
#define UNI_MAX_UTF16 (UTF32)0x0010FFFF
#define UNI_MAX_UTF32 (UTF32)0x7FFFFFFF
#define UNI_MAX_LEGAL_UTF32 (UTF32)0x0010FFFF

/*
 * Index into the table below with the first byte of a UTF-8 sequence to
 * get the number of trailing bytes that are supposed to follow it.
 * Note that *legal* UTF-8 values can't have 4 or 5-bytes. The table is
 * left as-is for anyone who may want to do such conversion, which was
 * allowed in earlier algorithms.
 */
static const char trailingBytesForUTF8[256] = {
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};

/*
 * Magic values subtracted from a buffer value during UTF8 conversion.
 * This table contains as many values as there might be trailing bytes
 * in a UTF-8 sequence.
 */
static const UTF32 offsetsFromUTF8[6] = { 0x00000000UL, 0x00003080UL, 0x000E2080UL, 
0x03C82080UL, 0xFA082080UL, 0x82082080UL };

/*
 * Utility routine to tell whether a sequence of bytes is legal UTF-8.
 * This must be called with the length pre-determined by the first byte.
 * If not calling this from ConvertUTF8to*, then the length can be set by:
 *  length = trailingBytesForUTF8[*source]+1;
 * and the sequence is illegal right away if there aren't that many bytes
 * available.
 * If presented with a length > 4, this returns false.  The Unicode
 * definition of UTF-8 goes up to 4-byte sequences.
 */

static char isLegalUTF8(const UTF8 *source, int length) {
    UTF8 a;
    const UTF8 *srcptr = source+length;
    switch (length) {
        default: return 0;
            /* Everything else falls through when "true"... */
        case 4: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
        case 3: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
        case 2: if ((a = (*--srcptr)) > 0xBF) return 0;
            
            switch (*source) {
                    /* no fall-through in this inner switch */
                case 0xE0: if (a < 0xA0) return 0; break;
                case 0xED: if (a > 0x9F) return 0; break;
                case 0xF0: if (a < 0x90) return 0; break;
                case 0xF4: if (a > 0x8F) return 0; break;
                default:   if (a < 0x80) return 0;
            }
            
        case 1: if (*source >= 0x80 && *source < 0xC2) return 0;
    }
    if (*source > 0xF4) return 0;
    return 1;
}

/* --------------------------------------------------------------------- */
ConversionResult ConvertUTF8toUTF16 (const UTF8** sourceStart, const UTF8* sourceEnd, 
                                     UTF16** targetStart, UTF16* targetEnd, ConversionFlags flags) {
    ConversionResult result = conversionOK;
    const UTF8* source = *sourceStart;
    UTF16* target = *targetStart;
    while (source < sourceEnd) {
        UTF32 ch = 0;
        unsigned short extraBytesToRead = trailingBytesForUTF8[*source];
        if (source + extraBytesToRead >= sourceEnd) {
            result = sourceExhausted; break;
        }
        /* Do this check whether lenient or strict */
        if (! isLegalUTF8(source, extraBytesToRead+1)) {
            result = sourceIllegal;
            break;
        }
        /*
         * The cases all fall through. See "Note A" below.
         */
        switch (extraBytesToRead) {
            case 5: ch += *source++; ch <<= 6; /* remember, illegal UTF-8 */
            case 4: ch += *source++; ch <<= 6; /* remember, illegal UTF-8 */
            case 3: ch += *source++; ch <<= 6;
            case 2: ch += *source++; ch <<= 6;
            case 1: ch += *source++; ch <<= 6;
            case 0: ch += *source++;
        }
        ch -= offsetsFromUTF8[extraBytesToRead];
        
        if (target >= targetEnd) {
            source -= (extraBytesToRead+1); /* Back up source pointer! */
            result = targetExhausted; break;
        }
        if (ch <= UNI_MAX_BMP) { /* Target is a character <= 0xFFFF */
            /* UTF-16 surrogate values are illegal in UTF-32 */
            if (ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_LOW_END) {
                if (flags == strictConversion) {
                    source -= (extraBytesToRead+1); /* return to the illegal value itself */
                    result = sourceIllegal;
                    break;
                } else {
                    *target++ = UNI_REPLACEMENT_CHAR;
                }
            } else {
                *target++ = (UTF16)ch; /* normal case */
            }
        } else if (ch > UNI_MAX_UTF16) {
            if (flags == strictConversion) {
                result = sourceIllegal;
                source -= (extraBytesToRead+1); /* return to the start */
                break; /* Bail out; shouldn't continue */
            } else {
                *target++ = UNI_REPLACEMENT_CHAR;
            }
        } else {
            /* target is a character in range 0xFFFF - 0x10FFFF. */
            if (target + 1 >= targetEnd) {
                source -= (extraBytesToRead+1); /* Back up source pointer! */
                result = targetExhausted; break;
            }
            ch -= halfBase;
            *target++ = (UTF16)((ch >> halfShift) + UNI_SUR_HIGH_START);
            *target++ = (UTF16)((ch & halfMask) + UNI_SUR_LOW_START);
        }
    }
    *sourceStart = source;
    *targetStart = target;
    return result;
}

UTF8* utf8dup(const UTF8* source) {
    return xmlStrdup(source);
}
UTF8* utf8cat(UTF8* source, const UTF8* add) {
    return xmlStrcat(source, add);
}

int utf8equal(const UTF8* str1, const UTF8* str2) {
    return xmlStrEqual(str1, str2);
}

UTF16* utf8to16(const UTF8* source) {
    int length = utf8length(source);
    int size = utf8size(source);
    UTF16* buffer = calloc(sizeof(UTF16), length + 1);
    UTF16* result = buffer;
    ConvertUTF8toUTF16(&source, source + size, &buffer, buffer + length, strictConversion);
    return result;
}

int utf8length(const UTF8* str) {
    return xmlUTF8Strlen(str);
}

int utf8size(const UTF8* str) {
    return xmlUTF8Strsize(str, xmlUTF8Strlen(str));
}

int utf16length(const UTF16* str) {
    int length = 0;
    while(*str) {
        if (*str >= UNI_SUR_HIGH_START && *str <= UNI_SUR_LOW_END) {
            str++;
        }
        length++;
        str++;
    }
    return length;
}

int utf16size(const UTF16* str) {
    int size = 0;
    while(*str) {
        size++;
        str++;
    }
    return size;
}
