/*
 *  MapperArea.h
 *  OSMapper
 *
 *  Created by Egor Leonenko on 13.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#ifndef _MAPPER_AREA_H_
#define _MAPPER_AREA_H_

#include "MapperTypes.h"
#include "MapperAttribute.h"

#define AREA_NODES_COUNT 11
#define AREA_ATTRIBUTES_COUNT 2

typedef struct {
    OsmId id;
    BBox bbox;
    MapperClassId class;
} MapperAreaInfo;

typedef struct {
    OsmId id;
	struct {
		AreaPartRole role: 2;
		char nodesCount: 6;
	}; 
} MapperPolygonInfo;

typedef struct {
    MapperAreaInfo area;
    MapperPolygonInfo polygon;
    
    MapperPartAttribute attributes[AREA_ATTRIBUTES_COUNT];
    MapperWayNode nodes[AREA_NODES_COUNT];
} MapperArea;

#endif
