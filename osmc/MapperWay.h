/*
 *  MapperWay.h
 *  OSMapper
 *
 *  Created by Egor Leonenko on 12.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#ifndef _MAPPER_WAY_H_
#define _MAPPER_WAY_H_

#include "MapperTypes.h"
#include "MapperAttribute.h"

#define WAY_NODES_COUNT 11
#define WAY_ATTRIBUTES_COUNT 2

typedef struct {
    OsmId id;
    WayPartRole role;
    BBox bbox;
    MapperClassId class;
    OsmId groupId;
} MapperWayInfo;

typedef struct {
    MapperWayInfo info;
    
    MapperPartAttribute attributes[WAY_ATTRIBUTES_COUNT];
    MapperWayNode nodes[WAY_NODES_COUNT];
} MapperWay;

#endif
