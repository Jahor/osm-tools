/*
 *  MapperTypes.c
 *  OSMapper
 *
 *  Created by Egor Leonenko on 12.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#include "MapperTypes.h"
#include <math.h>
#include <stdlib.h>

static const long int coordinateMultiplier = 10000000;

Coordinate coordianteFromDouble(double c) {
    return round(c * coordinateMultiplier);
}

double doubleFromCoordiante(Coordinate c) {
    return ((double)c) / coordinateMultiplier;
}

CollectionImplGeneric(MapperWayNode, MapperWayNodes, 11)
CollectionImplGeneric(OsmId, OsmIds, 5)
