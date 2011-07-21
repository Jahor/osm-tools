/*
 *  2DTree.h
 *  OSMapper
 *
 *  Created by Egor Leonenko on 11.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#ifndef _2D_TREE_H_
#define _2D_TREE_H_

#include "MapperTypes.h"
#include <stdio.h>
#include "utils.h"

typedef struct  {
    Offset offset;
    ZoomLevel minZoomLevel;
    ZoomLevel maxZoomLevel;
    Coordinate dimensions[2];
} Object2D;

typedef struct {
    Object2D info;
    
    Offset leftOffset;
    Offset rightOffset;
} Tree2DRecord;      

typedef struct ATree2D {
    struct ATree2D* left;
    struct ATree2D* right;
    char dimension;
    Object2D info;
} Tree2D;

Collection(Object2D, Objects2D)

void add2DObject(Objects2D* self, OsmPoint point, Offset offset, ZoomLevel minZoomLevel, ZoomLevel maxZoomLevel);

Tree2D* index2DObjects(Objects2D* objects);
void write2DTree(Tree2D* self, FILE* file);
void free2DTree(Tree2D* self);
#endif
