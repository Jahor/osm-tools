/*
 *  MapperPoint.h
 *  OSMapper
 *
 *  Created by Egor Leonenko on 12.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#ifndef _MAPPER_POINT_H_
#define _MAPPER_POINT_H_

#include "MapperTypes.h"
#include "MapperAttribute.h"

#define POINT_ATTRIBUTES_COUNT 2

typedef struct {
    OsmId id;
    Coordinate x;
    Coordinate y;
    MapperClassId class;    
} MapperPointInfo;

typedef struct {
    MapperPointInfo info;
    MapperAttribute attributes[POINT_ATTRIBUTES_COUNT];
} MapperPoint;
#endif
