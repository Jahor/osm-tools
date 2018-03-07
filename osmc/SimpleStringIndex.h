/*
 *  SimpleStringIndex.h
 *  OSMapper
 *
 *  Created by Egor Leonenko on 7.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#ifndef	_SIMPLE_STRING_INDEX_H_
#define	_SIMPLE_STRING_INDEX_H_

#include <stdio.h>
#include "utf.h"

#define SIMPLE_STRING_INDEX_LEAFS_COUNT_BITS 8
#define SIMPLE_STRING_INDEX_LEAFS_COUNT ((1<<(SIMPLE_STRING_INDEX_LEAFS_COUNT_BITS + 1)) - 1)
#define SIMPLE_STRING_INDEX_LEAFS_COUNT_MASK ((1 << SIMPLE_STRING_INDEX_LEAFS_COUNT_BITS) - 1)

typedef struct ASimpleStringIndexLeaf {
    struct ASimpleStringIndexLeaf* previous;
    struct ASimpleStringIndexLeaf* next;
    UTF8* value;
    int index;
} SimpleStringIndexLeaf;

typedef struct {
    SimpleStringIndexLeaf* leafs[SIMPLE_STRING_INDEX_LEAFS_COUNT];
    UTF8** values;
    int valuesCount;
    int valuesCapacity;
} SimpleStringIndex;

int simpleStringIndexOf(SimpleStringIndex* index, UTF8* key);
UTF8* simpleStringValuesAtIndex(SimpleStringIndex* self, int index);

void initSimpleStringIndex(SimpleStringIndex* index);
void initSimpleStringIndexFromFile(SimpleStringIndex* index, FILE* file);

void writeSimpleStringIndex(SimpleStringIndex* index, FILE* file);
#endif
