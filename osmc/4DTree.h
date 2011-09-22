/*
 *  4DTree.h
 *  OSMapper
 *
 *  Created by Egor Leonenko on 11.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#ifndef _4D_TREE_H_
#define _4D_TREE_H_

#include "osm.h"
#include "utils.h"

typedef struct  {
    Offset offset;
    ZoomLevel minZoomLevel;
    ZoomLevel maxZoomLevel;
    Coordinate dimensions[4];
} Object4D;

typedef struct {
    Object4D info;
    
    Offset leftOffset;
    Offset rightOffset;
} Tree4DRecord;      

typedef struct ATree4D{
    struct ATree4D* left;
    struct ATree4D* right;
    char dimension;
    Object4D info;
} Tree4D;

Collection(Object4D, Objects4D)

void add4DObject(Objects4D* self, BBox bounds, Offset offset, ZoomLevel minZoomLevel, ZoomLevel maxZoomLevel);

Tree4D* index4DObjects(Objects4D* objects);
void write4DTree(Tree4D* self, FILE* file, WriteCallback write);
void free4DTree(Tree4D* self);
#endif
