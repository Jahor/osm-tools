/*
 *  4DTree.c
 *  OSMapper
 *
 *  Created by Egor Leonenko on 11.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#include "4DTree.h"
#include <stdlib.h>
#include <limits.h>

void initObjects4D(Objects4D* self) {
    self->values = NULL;
    self->count = 0;
    self->capacity = 0;
}

void add4DObject(Objects4D* self, BBox bounds, Offset offset, ZoomLevel minZoomLevel, ZoomLevel maxZoomLevel) {
    if(self->capacity < self->count + 1) {
        self->capacity += 100;
        self->values = realloc(self->values, sizeof(Object4D)* self->capacity);
    }
    self->values[self->count].offset = offset;
    self->values[self->count].dimensions[0] = bounds.min.x;
    self->values[self->count].dimensions[1] = bounds.min.y;
    self->values[self->count].dimensions[2] = bounds.max.x;
    self->values[self->count].dimensions[3] = bounds.max.y;
    self->values[self->count].minZoomLevel = minZoomLevel;
    self->values[self->count].maxZoomLevel = maxZoomLevel;
    self->count++;
}

static int dimensionCompare(Object4D* object1, Object4D* object2, char dimension) {    
    Coordinate v1 = object1->dimensions[dimension];
    Coordinate v2 = object2->dimensions[dimension];
    return v1 - v2;
}

typedef int (*DimensionComarator)(const void* object1, const void* object2);

static int dimension1Compare(const void* object1, const void* object2) {    
    return dimensionCompare((Object4D*) object1, (Object4D*) object2, 0);
}

static int dimension2Compare(const void* object1, const void* object2) {    
    return dimensionCompare((Object4D*) object1, (Object4D*) object2, 1);
}

static int dimension3Compare(const void* object1, const void* object2) {    
    return dimensionCompare((Object4D*) object1, (Object4D*) object2, 2);
}

static int dimension4Compare(const void* object1, const void* object2) {    
    return dimensionCompare((Object4D*) object1, (Object4D*) object2, 3);
}

static DimensionComarator comparators[4] = {
    dimension1Compare, dimension2Compare, dimension3Compare, dimension4Compare
};

Tree4D* index4DObjectsInternal(Objects4D* objects, int level) {
    //printf("Level %i\n", level);
    if(objects->count == 0) {
        //printf("  Empty\n");
        return NULL;
    }
    Tree4D* self = calloc(sizeof(Tree4D), 1);
    //Coordinate dividor;
    self->dimension = level % 4;
    if(objects->count == 1) {
       // printf("  Single object with offset %i \n",objects->values[0].offset);
        self->info.offset = objects->values[0].offset;
        self->info.dimensions[0] = objects->values[0].dimensions[0];
        self->info.dimensions[1] = objects->values[0].dimensions[1];
        self->info.dimensions[2] = objects->values[0].dimensions[2];
        self->info.dimensions[3] = objects->values[0].dimensions[3];
        self->info.minZoomLevel = objects->values[0].minZoomLevel;
        self->info.maxZoomLevel = objects->values[0].maxZoomLevel;
        self->left = NULL;
        self->right = NULL;
    } else {
        qsort(objects->values, objects->count, sizeof(Object4D), comparators[self->dimension]);

        int medianIndex = objects->count / 2;
        //printf("  %i objects divided by %i. Object with offset %i\n", objects->count, medianIndex, objects->values[medianIndex].offset);
        self->info.offset = objects->values[medianIndex].offset;
        self->info.dimensions[0] = objects->values[medianIndex].dimensions[0];
        self->info.dimensions[1] = objects->values[medianIndex].dimensions[1];
        self->info.dimensions[2] = objects->values[medianIndex].dimensions[2];
        self->info.dimensions[3] = objects->values[medianIndex].dimensions[3];
        self->info.minZoomLevel = objects->values[medianIndex].minZoomLevel;
        self->info.maxZoomLevel = objects->values[medianIndex].maxZoomLevel;
        //dividor = self->info.dimensions[self->dimension];
        Objects4D leftObjects = {objects->values, medianIndex, medianIndex};
        
        self->left = index4DObjectsInternal(&leftObjects, level + 1);
        Objects4D rightObjects = {objects->values + medianIndex + 1, objects->count - medianIndex - 1, objects->count - medianIndex - 1};
        self->right = index4DObjectsInternal(&rightObjects, level + 1);
    }
    return self;
}

Tree4D* index4DObjects(Objects4D* objects) {
    return index4DObjectsInternal(objects, 0);
}

static int isLeaf(Tree4D* self) {
    return !self->left && !self->right;
}

static int fullCount(Tree4D* self) {
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

void write4DTree(Tree4D* self, FILE* file) {
    if(self) {
        Tree4DRecord info;
        info.info.offset = self->info.offset;
        info.info.dimensions[0] = self->info.dimensions[0];
        info.info.dimensions[1] = self->info.dimensions[1];
        info.info.dimensions[2] = self->info.dimensions[2];
        info.info.dimensions[3] = self->info.dimensions[3];
        info.info.minZoomLevel = self->info.minZoomLevel;
        info.info.maxZoomLevel = self->info.maxZoomLevel;
        info.leftOffset = self->left ? 0 : OFFSET_NOT_DEFINED;
        info.rightOffset = self->right ? (self->left ? fullCount(self->left) : 0) * sizeof(Tree4DRecord) : OFFSET_NOT_DEFINED;
        
        fwrite(&info, sizeof(Tree4DRecord), 1, file);
        if(self->left) {
            write4DTree(self->left, file);
        }
        if(self->right) {
            write4DTree(self->right, file);
        }
    }
} 

void free4DTree(Tree4D* self) {
    if(self) {
        if(self->left) {
            free4DTree(self->left);
        }
        if(self->right) {
            free4DTree(self->right);
        }
        free(self);   
    }
}
