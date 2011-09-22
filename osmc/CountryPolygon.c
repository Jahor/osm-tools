/*
 *  CountryPolygon.c
 *  OSMapper
 *
 *  Created by Egor Leonenko on 8.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#include "CountryPolygon.h"
#include <math.h>
#include <string.h>
#include <memory.h>
#include <stdlib.h>
#include <float.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include "utils.h"

typedef enum {
    POINT_ON_LEFT,
    POINT_ON_RIGHT,
    POINT_BEHIND,
    POINT_BEYOND,
    POINT_IS_ORIGIN,
    POINT_IS_DESTINATION,
    POINT_IS_BETWEEN
} POINT_LINE_POSITION;

typedef enum {
    TOUCHING,
    CROSSING,
    INESSENTIAL
} EDGE_INTERSECTION_TYPE;

OsmPoint OsmPointMake(double x, double y)
{
    return OsmPointMakeRaw(coordianteFromDouble(x), coordianteFromDouble(y));
}

OsmPoint OsmPointMakeRaw(Coordinate x, Coordinate y)
{
    OsmPoint p;
    p.x = x;
    p.y = y;
    return p;
}


LineSegment LineSegmentMake(OsmPoint p0, OsmPoint p1)
{
    LineSegment s;
    s.p1 = p1;
    s.p0 = p0;
    return s;
}

POINT_LINE_POSITION classifyPoint(Coordinate x, Coordinate y, Coordinate p0x, Coordinate p0y, Coordinate p1x, Coordinate p1y)
{
    if (p0x == x && p0y == y)
        return POINT_IS_ORIGIN;
    if (p1x == x && p1y == y)
        return POINT_IS_DESTINATION;    
    
    Coordinate ax = p1x - p0x;
    Coordinate ay = p1y - p0y;
    Coordinate bx = x - p0x;
    Coordinate by = y - p0y;
    
    Coordinate sa = ax * by - bx * ay;
    if (sa > 0)
        return POINT_ON_LEFT;
    if (sa < 0)
        return POINT_ON_RIGHT;
    if ((ax * bx < 0) || (ay * by < 0))
        return POINT_BEHIND;
    if (sqrt(ax*ax + ay*ay) < sqrt(bx*bx + by*by))
        return POINT_BEYOND;
    
    return POINT_IS_BETWEEN;
}

EDGE_INTERSECTION_TYPE edgeType(Coordinate ax, Coordinate ay, LineSegment* segment) {
    Coordinate p0x = segment->p0.x;
    Coordinate p0y = segment->p0.y;
    Coordinate p1x = segment->p1.x;
    Coordinate p1y = segment->p1.y;
    switch (classifyPoint(ax, ay, p0x, p0y, p1x, p1y)) {
        case POINT_ON_LEFT:
            return ((p0y<ay)&&(ay<=p1y)) ? CROSSING : INESSENTIAL; 
        case POINT_ON_RIGHT:
            return ((p1y<ay)&&(ay<=p0y)) ? CROSSING : INESSENTIAL; 
        case POINT_IS_BETWEEN:
        case POINT_IS_ORIGIN:
        case POINT_IS_DESTINATION:
            return TOUCHING;
        default:
            return INESSENTIAL;
    }
}

POINT_POLYGON_POSITION isPointInPolygon(Coordinate x, Coordinate y, CountryPolygon* polygon) {
    
    if(polygon->segmentsCount == 0) {
        return INSIDE;
    }
    
    if(x < polygon->bbox.min.x || y < polygon->bbox.min.y || x > polygon->bbox.max.x || y > polygon->bbox.max.y) {
        return OUTSIDE;
    }
    
    int parity = 0;
    for(int s=0; s < polygon->segmentsCount; s++) {
        switch (edgeType(x,y, polygon->segments + s)) {
            case TOUCHING:
                return BOUNDARY;
            case CROSSING:
                parity = 1 - parity;
                break;
            case INESSENTIAL:
                break;
        }
    }
    return (parity ? INSIDE : OUTSIDE);
}

void readPolygon(const char* fileName, CountryPolygon* polygon) {
    FILE* file = fopen(fileName, "r+");
    char* name = calloc(sizeof(char), 255);
    fscanf(file, "%s", name);
    polygon->name = calloc(sizeof(char), strlen(name));
    strcpy((char*)polygon->name, name);
    int state = 1;
    char end[100];
    double lat, lon;
    OsmPoint firstPolygonPoint;
    OsmPoint lastReadPolygonPoint;
    int capacity = 0;
    polygon->segmentsCount = 0;
    double minX = DBL_MAX;
    double minY = DBL_MAX;
    double maxX = DBL_MIN;
    double maxY = DBL_MIN;
        
    polygon->segments = malloc(sizeof(LineSegment)* capacity);
    while(!feof(file)) {
        if((state >= 2 && state <= 5) && fscanf(file, "%lf %lf", &lon, &lat) == 2) {
            if(lat < minX) {
                minX = lat;
            }
            if(lat > maxX) {
                maxX = lat;
            }
            
            if(lon < minY) {
                minY = lon;
            }
            if(lon > maxY) {
                maxY = lon;
            }
            OsmPoint justReadPolygonPoint = OsmPointMake(lat, lon);
            if(state == 2 || state == 4) {
                firstPolygonPoint = justReadPolygonPoint;
                state++;
            } else {
                if(capacity < polygon->segmentsCount + 1) {
                    capacity += 10;
                    polygon->segments = realloc(polygon->segments, sizeof(LineSegment)* capacity);
                }
                polygon->segments[polygon->segmentsCount++] = LineSegmentMake(lastReadPolygonPoint, justReadPolygonPoint);
            }
            lastReadPolygonPoint = justReadPolygonPoint;
        } else if(fscanf(file, "%s", end)) {
            if(strcmp(end, "END") == 0) {
                if(state >= 2 && state <= 5) {
                    if(lastReadPolygonPoint.x != firstPolygonPoint.x || lastReadPolygonPoint.y != firstPolygonPoint.y) {
                        if(capacity < polygon->segmentsCount + 1) {
                            capacity += 10;
                            polygon->segments = realloc(polygon->segments, sizeof(LineSegment)* capacity);
                        }
                        polygon->segments[polygon->segmentsCount++] = LineSegmentMake(lastReadPolygonPoint, firstPolygonPoint);                        
                    }

                    state = 1;
                } else if(state == 1) {
                    state = 0;
                }
            } else if (state == 1) {
                if(strlen(end) > 2 &&  end[0] == '!') {
                    state = 4;
                } else {
                    state = 2;
                }
            }
        }
    }
    
    polygon->bbox.min.x = coordianteFromDouble(minX);
    polygon->bbox.max.x = coordianteFromDouble(maxX);
    polygon->bbox.min.y = coordianteFromDouble(minY);
    polygon->bbox.max.y = coordianteFromDouble(maxY);    
    
    polygon->segments = realloc(polygon->segments, polygon->segmentsCount);

    fclose(file);
    printf("Polygon read.\n");
}

CountryPolygon* readPolygons(const char* polygonsDirectory, int* count, const char* defaultName) {
    CountryPolygon *polygons = NULL;
    *count = 0;
    if(polygonsDirectory) {
        DIR *dir = opendir(polygonsDirectory);
        if(dir)
        {
            int capacity = 0;
            struct dirent *ent;
            while((ent = readdir(dir)) != NULL)
            { 
                if(strcmp(".", ent->d_name)!=0 && strcmp("..", ent->d_name)!=0) {
                    if(capacity < *count + 1) {
                        capacity += 10;
                        polygons = realloc(polygons, sizeof(CountryPolygon) * capacity);
                    }
                    char* polygonFileName = fullFileName(ent->d_name, polygonsDirectory);
                    readPolygon(polygonFileName, polygons+(*count));
                    free(polygonFileName);
                    (*count)++;
                }
            }
        }
        else
        {
            fprintf(stderr, "Error opening directory\n");
            return NULL;
        }
    } else {
        polygons = malloc(sizeof(CountryPolygon));
        polygons[0].name = defaultName;
        polygons[0].segmentsCount = 0;
        *count = 1;
    }    
    return polygons;
}
