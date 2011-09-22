/*
 *  mapper.h
 *  OSMapper
 *
 *  Created by Egor Leonenko on 11.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#include "osm.h"
#include "Tree16.h"
#include "SimpleStringIndex.h"
#include "4DTree.h"
#include "2DTree.h"
#include "collections.h"
#include "MapperArea.h"
#include "MapperWay.h"
#include "MapperPoint.h"
#include "utils.h"

#pragma mark Common
typedef struct {
    MapperPolygonInfo info;
    PlainTags tags;
    MapperWayNodes wayNodes;
} MapperPolygon;

Collection(MapperPolygon, MapperPolygons)

typedef struct {
    OsmId id;
    BBox bounds;
    int nameLength;
    UTF8* name;
} MapInformation;

#pragma mark Writer    

typedef struct {
    char* dbPath;
    FILE* pointsFile;
    FILE* waysFile;
    FILE* areasFile;
    
    //FILE* pointsIndexFile;
    //FILE* waysIndexFile;
    //FILE* areasIndexFile;
    
    FILE* pointsLocationIndexFile;
    FILE* waysLocationIndexFile;
    FILE* areasLocationIndexFile;
    
    MapInformation mapInformation;
	
	char compressed;
	WriteCallback write;
    
    SimpleStringIndex attributesIndex;
    SimpleStringIndex typesIndex; 
    
    Objects2D pointsLocations;
    Objects4D waysLocations;
    Objects4D areasLocations;
    
    int pointsOffset;
    int waysOffset;
    int areasOffset;
    
    Tree16 pointsIndex;
    Tree16 waysIndex;
    Tree16 areasIndex;
} MapperWriter;

void initMapperWriter(MapperWriter* self, const char* outputDirectory, char compress);

void closeMapperWriter(MapperWriter* self);

#pragma mark Converter

typedef struct {
    OsmIds outers;
    OsmIds inners;
    int converted;
    OsmId relationId;
} MultipolygonRelation, *PMultipolygonRelation;
    
Collection(PMultipolygonRelation, MultipolygonRelations)

typedef struct {
    OsmDbReader* reader;
    MapperWriter* writer;
    BBox bounds;
    MultipolygonRelations multipolygons;
} MapperConverter;

void initMapperConverter(MapperConverter* self, OsmDbReader* reader, const char* outputDirectory, char compress);
void convertToMapper(MapperConverter* self);

#pragma mark Reader
typedef struct {
    char* dbPath;
    FILE* pointsFile;
    FILE* waysFile;
    FILE* areasFile;
        
    FILE* pointsLocationIndexFile;
    FILE* waysLocationIndexFile;
    FILE* areasLocationIndexFile;
    
	CloseCallback* close;
	ReadCallback* read;
	
    MapInformation mapInformation;
    
    SimpleStringIndex attributesIndex;
    SimpleStringIndex typesIndex;
} MapperReader;


void initMapperReader(MapperReader* self, const char* mapDirectory);

void closeMapperReader(MapperReader* self);

//MapperPoints* getNodesInRect(MapperReader* self, BBox bounds);
//MapperWays* getWaysInRect(MapperReader* self, BBox bounds);
//MapperAreas* getAreasInRect(MapperReader* self, BBox bounds);
