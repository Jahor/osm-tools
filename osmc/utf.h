/*
 *  utf.h
 *  OSMapper
 *
 *  Created by Egor Leonenko on 8.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#ifndef _UTF_H_
#define _UTF_H_
typedef unsigned short	UTF16;	/* at least 16 bits */
typedef unsigned char	UTF8;	/* typically 8 bits */
#define UTF8_CAST (unsigned char*)

typedef enum {
	conversionOK, 		/* conversion successful */
	sourceExhausted,	/* partial character in source, but hit end */
	targetExhausted,	/* insuff. room in target for conversion */
	sourceIllegal		/* source sequence is illegal/malformed */
} ConversionResult;

typedef enum {
	strictConversion = 0,
	lenientConversion
} ConversionFlags;


UTF16* utf8to16(const UTF8* source);

ConversionResult convertUTF8toUTF16 (const UTF8** sourceStart, const UTF8* sourceEnd, 
                                     UTF16** targetStart, UTF16* targetEnd, ConversionFlags flags);

int utf8length(const UTF8* str);
int utf8size(const UTF8* str);
int utf8equal(const UTF8* str1, const UTF8* str2);
UTF8* utf8dup(const UTF8* source);
UTF8* utf8cat(UTF8* source, const UTF8* add);

int utf16length(const UTF16* str);
int utf16size(const UTF16* str);

#endif
