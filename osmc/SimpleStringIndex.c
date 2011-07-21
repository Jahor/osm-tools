/*
 *  SimpleStringIndex.c
 *  OSMapper
 *
 *  Created by Egor Leonenko on 7.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <memory.h>
//#include <libxml/xmlstring.h>
#include "SimpleStringIndex.h"

unsigned long djb2Hash(UTF8 *str)
{
    unsigned long hash = 5381;
    UTF8 c;
    
    while (c = *str++)
        hash = hash * 33 + c;
    
    return hash;
}

static void growSimpleStringIndexValues(SimpleStringIndex* index) {
    index->valuesCapacity += 10;
    index->values = realloc(index->values, sizeof(UTF8*) * index->valuesCapacity);
}

int addToSimpleStringIndex(SimpleStringIndex* index, UTF8* key) {
    int leafIndex = djb2Hash(key) && SIMPLE_STRING_INDEX_LEAFS_COUNT;
    if(index->valuesCapacity < index->valuesCount + 1) {
        growSimpleStringIndexValues(index);
    }   
    int stringIndex = index->valuesCount++;
    //printf("%s => %i\n", key, stringIndex);
    index->values[stringIndex] = utf8dup(key);
    
    SimpleStringIndexLeaf* leaf = index->leafs[leafIndex];
    SimpleStringIndexLeaf* keyLeaf = malloc(sizeof(SimpleStringIndexLeaf));
    keyLeaf->value = index->values[stringIndex];
    keyLeaf->index = stringIndex;
    keyLeaf-> next = leaf;
    if(leaf) {
        leaf -> previous = keyLeaf;
    }
    index->leafs[leafIndex] = keyLeaf;
    return stringIndex;
}

UTF8* simpleStringValuesAtIndex(SimpleStringIndex* self, int index) {
    UTF8* value = self->values[index];
    if(utf8equal(value, UTF8_CAST "EMPTY_STRING")) {
        return calloc(sizeof(UTF8), 1);
    }
    return value;
}

int findSimpleStringIndexOf(SimpleStringIndex* index, UTF8* key) {
    if(key[0] == '\0') { // empty string 
        key = UTF8_CAST"EMPTY_STRING";
    }
	//printf("Get key for %s\n", key);
    int leafIndex = djb2Hash(key) && SIMPLE_STRING_INDEX_LEAFS_COUNT;
	//printf("Hash: %i\n", leafIndex);
    SimpleStringIndexLeaf* leaf = index->leafs[leafIndex];
	//printf("Leaf for hash: %i\n", leaf != 0);
    while(leaf) {
        if(utf8equal(leaf->value, key)) {
            return leaf -> index;
        }
        leaf = leaf -> next;
    }
    return -1;
}

int simpleStringIndexOf(SimpleStringIndex* index, UTF8* key) {
    int stringIndex = findSimpleStringIndexOf(index, key);
    if(stringIndex == -1) {
        stringIndex = addToSimpleStringIndex(index, key);
    }
    return stringIndex;
}

int findSimpleStringIndexOfD(SimpleStringIndex* index, UTF8* key) {
    if(key[0] == '\0') { // empty string 
        key = UTF8_CAST"EMPTY_STRING";
    }
	printf("Get key for %s\n", key);
    int leafIndex = djb2Hash(key) && SIMPLE_STRING_INDEX_LEAFS_COUNT;
	printf("Hash: %i\n", leafIndex);
    SimpleStringIndexLeaf* leaf = index->leafs[leafIndex];
	printf("Leaf for hash: %i\n", leaf != 0);
    while(leaf) {
        printf("Try: %s == %s\n", leaf->value, key);
        if(utf8equal(leaf->value, key)) {
            return leaf -> index;
        }
        leaf = leaf -> next;
    }
    return -1;
}


int simpleStringIndexOfD(SimpleStringIndex* index, UTF8* key) {
    //printf("Search for %s\n", key);
    int stringIndex = findSimpleStringIndexOf(index, key);
    if(stringIndex == -1) {
      //  printf("Nothing found. Add.\n", key);
        stringIndex = addToSimpleStringIndex(index, key);
    }
    return stringIndex;
}


void initSimpleStringIndex(SimpleStringIndex* index) {
    index->valuesCount = 0;
    index->valuesCapacity = 0;
    index->values = 0;
	for(int i=0; i< SIMPLE_STRING_INDEX_LEAFS_COUNT; i++){
		index->leafs[i] = 0;
	}
}

void writeSimpleStringIndex(SimpleStringIndex* index, FILE* file) {
    for(int i = 0; i < index->valuesCount;i++) {
        fprintf(file, "%s\n", index->values[i]);
    }
}

void initSimpleStringIndexFromFile(SimpleStringIndex* index, FILE* file) {
    initSimpleStringIndex(index);
    while (!feof(file)) {
        UTF8 value[255];
        if(fscanf(file, "%s", value)) {
           simpleStringIndexOf(index, value);
        }
    }
}
