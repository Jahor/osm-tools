/*
 *  CountryPolygon.h
 *  OSMapper
 *
 *  Created by Egor Leonenko on 8.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#ifndef _COUNTRY_POLYGON_H_
#define _COUNTRY_POLYGON_H_

#include <stdio.h>
#include "MapperTypes.h"

typedef struct {
    OsmPoint p0;
    OsmPoint p1;
} LineSegment;

typedef struct {
    const char* name;
    LineSegment* segments;
    int segmentsCount;
    BBox bbox;
} CountryPolygon;

typedef enum {
    OUTSIDE = 0,
    INSIDE,
    BOUNDARY
} POINT_POLYGON_POSITION;


OsmPoint OsmPointMake(double x, double y);
OsmPoint OsmPointMakeRaw(Coordinate x, Coordinate y);
POINT_POLYGON_POSITION isPointInPolygon(Coordinate x, Coordinate y, CountryPolygon* polygon);
void readPolygon(const char* fileName, CountryPolygon* polygon);
CountryPolygon* readPolygons(const char* polygonsDirectory, int* count, const char* defaultName);
#endif
