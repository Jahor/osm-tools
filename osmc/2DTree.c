/*
 *  2DTree.c
 *  OSMapper
 *
 *  Created by Egor Leonenko on 11.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#include "2DTree.h"
#include <stdlib.h>
#include <limits.h>

CollectionImplGeneric(Object2D, Objects2D, 100)

void add2DObject(Objects2D* self, OsmPoint point, Offset offset, ZoomLevel minZoomLevel, ZoomLevel maxZoomLevel) {
    if(self->capacity < self->count + 1) {
        self->capacity += 100;
        self->values = realloc(self->values, sizeof(Object2D)* self->capacity);
    }
    self->values[self->count].offset = offset;
    self->values[self->count].dimensions[0] = point.x;
    self->values[self->count].dimensions[1] = point.y;
    self->values[self->count].minZoomLevel = minZoomLevel;
    self->values[self->count].maxZoomLevel = maxZoomLevel;
    self->count++;
}

static int dimensionCompare(Object2D* object1, Object2D* object2, char dimension) {    
    Coordinate v1 = object1->dimensions[dimension];
    Coordinate v2 = object2->dimensions[dimension];
    return v1 - v2;
}

typedef int (*DimensionComarator)(const void* object1, const void* object2);

static int dimension1Compare(const void* object1, const void* object2) {    
    return dimensionCompare((Object2D*) object1, (Object2D*) object2, 0);
}

static int dimension2Compare(const void* object1, const void* object2) {    
    return dimensionCompare((Object2D*) object1, (Object2D*) object2, 1);
}

static DimensionComarator comparators[2] = {
    dimension1Compare, dimension2Compare
};


Tree2D* index2DObjectsInternal(Objects2D* objects, int level) {
    if(objects->count == 0) {
        return NULL;
    }
    Tree2D* self = calloc(sizeof(Tree2D), 1);
//    Coordinate dividor;
    self->dimension = level % 2;
    if(objects->count == 1) {
        self->info.offset = objects->values[0].offset;
        self->info.dimensions[0] = objects->values[0].dimensions[0];
        self->info.dimensions[1] = objects->values[0].dimensions[1];
        self->info.minZoomLevel = objects->values[0].minZoomLevel;
        self->info.maxZoomLevel = objects->values[0].maxZoomLevel;
        self->left = NULL;
        self->right = NULL;
    } else {
        qsort(objects->values, objects->count, sizeof(Object2D), comparators[self->dimension]);
        
        int medianIndex = objects->count / 2;
        
        self->info.offset = objects->values[medianIndex].offset;
        self->info.dimensions[0] = objects->values[medianIndex].dimensions[0];
        self->info.dimensions[1] = objects->values[medianIndex].dimensions[1];
        self->info.minZoomLevel = objects->values[medianIndex].minZoomLevel;
        self->info.maxZoomLevel = objects->values[medianIndex].maxZoomLevel;
//        dividor = self->info.dimensions[self->dimension];
        Objects2D leftObjects = {objects->values, medianIndex, medianIndex};
        
        self->left = index2DObjectsInternal(&leftObjects, level + 1);
        Objects2D rightObjects = {objects->values + medianIndex + 1, objects->count - medianIndex - 1, objects->count - medianIndex - 1};
        self->right = index2DObjectsInternal(&rightObjects, level + 1);
    }
    return self;
}

Tree2D* index2DObjects(Objects2D* objects) {
    return index2DObjectsInternal(objects, 0);
}

static int isLeaf(Tree2D* self) {
    return !self->left && !self->right;
}

static int fullCount(Tree2D* self) {
    if(isLeaf(self)) {
        return 1;
    } else {
        int count = 1;
        if(self->left) {
            count += fullCount(self->left);
        }
        if(self->right) {
            count += fullCount(self->right);
        }
        return count;
    }
}

void write2DTree(Tree2D* self, FILE* file) {
    if(self) {
        Tree2DRecord info;
        info.info.offset = self->info.offset;
        info.info.dimensions[0] = self->info.dimensions[0];
        info.info.dimensions[1] = self->info.dimensions[1];
        info.leftOffset = self->left ? 0 : OFFSET_NOT_DEFINED;
        info.info.minZoomLevel = self->info.minZoomLevel;
        info.info.maxZoomLevel = self->info.maxZoomLevel;
        info.rightOffset = self->right ? (self->left ? fullCount(self->left) : 0) * sizeof(Tree2DRecord) : OFFSET_NOT_DEFINED;
    
        fwrite(&info, sizeof(Tree2DRecord), 1, file);
        if(self->left) {
            write2DTree(self->left, file);
        }
        if(self->right) {
            write2DTree(self->right, file);
        }   
    }
}

void free2DTree(Tree2D* self) {
    if(self) {
        if(self->left) {
            free2DTree(self->left);
        }
        if(self->right) {
            free2DTree(self->right);
        }
        free(self);
    }
}
