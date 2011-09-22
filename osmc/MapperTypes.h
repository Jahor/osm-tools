/*
 *  MapperTypes.h
 *  OSMapper
 *
 *  Created by Egor Leonenko on 12.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#ifndef _MAPPER_TYPES_H_
#define _MAPPER_TYPES_H_

#include "collections.h"
#include <stdint.h>
#include <math.h> 

typedef int32_t Coordinate;
typedef struct {
    Coordinate x;
    Coordinate y;
} OsmPoint;

typedef struct {
    OsmPoint min;
    OsmPoint max;
} BBox;

#define COORDINATE_MULTIPLIER 10000000L

#define coordianteFromDouble(c) ((Coordinate)round(c * COORDINATE_MULTIPLIER))
#define doubleFromCoordiante(c) (((double)c) / COORDINATE_MULTIPLIER)

//inline Coordinate coordianteFromDouble(double c);
//inline double doubleFromCoordiante(Coordinate c);

typedef uint32_t OsmId;
Collection(OsmId, OsmIds)
#define atoosmid atol

typedef uint32_t MapperIndexedValue;

typedef MapperIndexedValue MapperClassId;

typedef enum {
    CommonPart
} WayPartRole;

typedef enum {
    OuterAreaPart,
    InnerAreaPart
} AreaPartRole;

typedef struct {
    OsmId id;
    OsmPoint location;
} MapperWayNode;

Collection(MapperWayNode, MapperWayNodes)
void ensureMapperWayNodesCapacityForNNewElements(MapperWayNodes* , int count);

#endif
