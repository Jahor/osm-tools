//
//  Tree16.h
//  OSMapper
//
//  Created by Egor Leonenko on 22.2.09.
//  Copyright 2009 Egor Leonenko. All rights reserved.
//

#ifndef _TREE_16_H_
#define _TREE_16_H_
#include <stdio.h>

#define TREE_CHILDREN 16
#define BITS_COUNT 4 //((int)log2(TREE_CHILDREN))
#define MAX_LEVEL 9 //(sizeof(long)*8)/BITS_COUNT + 1

#define MASK 0xF //(TREE_CHILDREN - 1)

#define CACHE_LEVEL 4
#define CACHE_BITS_COUNT 16 //(BITS_COUNT*CACHE_LEVEL)
#define CACHE_SIZE 0x10000 //(TREE_CHILDREN * TREE_CHILDREN*TREE_CHILDREN * TREE_CHILDREN)
#define CACHE_MASK 0xFFFF //(TREE_CHILDREN * TREE_CHILDREN - 1)



typedef struct {
    long recordNumbers[TREE_CHILDREN];
} TreeRecord;

typedef struct ATree16 {
    long allChildrenCount;
    union {
        long offsets[TREE_CHILDREN];
        struct ATree16* children[TREE_CHILDREN];        
    } node;
} Tree16;

typedef struct {
    FILE* file;
    long root[CACHE_SIZE];
} Tree16OnFile;

void initTree16(Tree16* tree);
void addTree16Node(Tree16* tree, int Id, long offset);
void saveTree16ToFile(Tree16* tree, FILE* file, int level, long offset);

int isInTree16(Tree16* tree, int Id);

void freeTree16(Tree16* tree);

void freeTree16WithFile(Tree16OnFile* tree);
void initTree16WithFile(Tree16OnFile* self, FILE* aFile);
long findObjectOffset(Tree16OnFile* self, long Id);
#endif
