/*
 *  mapper.c
 *  OSMapper
 *
 *  Created by Egor Leonenko on 11.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#include "mapper.h"
#include "utf.h"
#include "utils.h"
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <fcntl.h>

#include <errno.h>
#include <sys/stat.h>

#ifndef M_PI
#define M_PI        3.14159265358979323846264338327950288   /* pi */
#endif


Coordinate mercatorX(Coordinate lon) {
    return lon;
}

Coordinate mercatorY(Coordinate lat) {
    return coordianteFromDouble(180.0/M_PI * log(tan(M_PI/4.0+doubleFromCoordiante(lat)*(M_PI/180.0)/2.0)));
}

void freeMapperPolygon(MapperPolygon* polygon) {
    //clearTags(&(polygon->tags));
    clearMapperWayNodes(&(polygon->wayNodes));
}

CollectionImplCustomElementFree(MapperPolygon, MapperPolygons, 5)
CollectionImplAdd(MapperPolygon, MapperPolygons)

static MapperAttributes pointAttributes = {NULL, 0, 0};
static MapperAttribute emptyPointAttribute = {0, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}};

int writeMapperPointTags(FILE* file, int *current) {
    int tagsLeft = pointAttributes.count - *current;
    if( tagsLeft > POINT_ATTRIBUTES_COUNT) {
        fwrite(pointAttributes.values + *current, sizeof(MapperAttribute), POINT_ATTRIBUTES_COUNT, file);
        *current= *current + POINT_ATTRIBUTES_COUNT;
    } else {
        if(tagsLeft) {
            fwrite(pointAttributes.values + *current, sizeof(MapperAttribute), tagsLeft, file);
        }
        for(int i=0; i < POINT_ATTRIBUTES_COUNT - tagsLeft; i++) {
            fwrite(&emptyPointAttribute, sizeof(MapperAttribute), 1, file);
        }
        *current= pointAttributes.count;
    }
    return pointAttributes.count > *current;
}


long int writeMapperPoint(FILE* file, OsmId id, Coordinate x, Coordinate y, PlainTags* tags, MapperClassId class, SimpleStringIndex* attributesIndex) {
    //printf("Write to file %i\n", file);
    MapperPointInfo point;
    point.id = id;
    point.class = class;
    point.x = x;
    point.y = y;
    //printf("Convert tags\n", file);
    mapperAttributesFromTags(&pointAttributes, tags, attributesIndex);
    
    int currentTag = 0;
    char tagsLeft = 1;
    int offset = 0;
    //printf("writing...\n");
    while(tagsLeft) {
        fwrite(&(point), sizeof(MapperPointInfo), 1, file);
        tagsLeft = writeMapperPointTags(file, &currentTag);
        offset += sizeof(MapperPoint);
    }
    removeAllMapperAttributes(&pointAttributes);
    //printf("Done\n", file);
    return offset;
}

static MapperPartAttributes partAttributes = { NULL, 0, 0};
static MapperPartAttribute emptyPartAttribute = {0,{0, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}};

int writeMapperPartTags(FILE* file, int *current, int maxCount) {
    int tagsLeft = partAttributes.count - *current;
    if( tagsLeft > maxCount) {
        fwrite(partAttributes.values + *current, sizeof(MapperPartAttribute), maxCount, file);
        *current= *current + maxCount;
    } else {
        if(tagsLeft) {
            fwrite(partAttributes.values + *current, sizeof(MapperPartAttribute), tagsLeft, file);
        }
        for(int i=0; i < maxCount - tagsLeft; i++) {
            fwrite(&emptyPartAttribute, sizeof(MapperPartAttribute), 1, file);
        }
        *current= partAttributes.count;
    }
    return partAttributes.count > *current;    
}

static MapperWayNode emptyWayNode = {{0,0},0};

int writeMapperWayNodes(FILE* file, MapperWayNodes* nodes, int *current, int maxCount) {
    int nodesLeft = min(nodes->count - *current, maxCount);
    int nodesWritten = fwrite(nodes->values + *current, sizeof(MapperWayNode), nodesLeft, file);
    *current += nodesWritten;
    for(int w=0; w< maxCount - nodesWritten; w++) {
        fwrite(&emptyWayNode, sizeof(MapperWayNode), 1, file);
    }
    return nodes->count > *current;
}

long int writeMapperWay(FILE* file, OsmId id, OsmId groupId, MapperWayNodes* nodes, BBox bbox, PlainTags* tags, MapperClassId class, SimpleStringIndex* attributesIndex) {    
    MapperWayInfo way;
    way.id = id;
    way.groupId = groupId;
    way.bbox = bbox;
    way.role = CommonPart;
    way.class = class;
    
    int currentTag = 0;
    int currentNode = 0;
    char tagsLeft = 1;
    char nodesLeft = 1;
    long int offset = 0;
    //printf("Convert tags...\n"); 
    mapperPartAttributesFromTags(&partAttributes, 0, tags, attributesIndex);
    //printf("Write...\n");
    while(tagsLeft || nodesLeft) {
        fwrite(&(way), sizeof(MapperWayInfo), 1, file);
        tagsLeft = writeMapperPartTags(file, &currentTag, WAY_ATTRIBUTES_COUNT);
        nodesLeft = writeMapperWayNodes(file, nodes, &currentNode, WAY_NODES_COUNT);
        offset += sizeof(MapperWay);
    }
    //printf("Done.\n");
    removeAllMapperPartAttributes(&partAttributes);
    return offset;
}

long int writeMapperArea(FILE* file, OsmId id, MapperPolygons* polygons, BBox bbox, PlainTags* tags, MapperClassId class, SimpleStringIndex* attributesIndex) {
    
    MapperAreaInfo area;
    area.id = id;
    area.class = class;
    area.bbox = bbox;
    
    int currentTag = 0;
    int currentNode = 0;
    char tagsLeft = 1;
    char nodesLeft = 1;
    long int offset = 0;
    
    mapperPartAttributesFromTags(&partAttributes, 0, tags, attributesIndex);
    
    MapperPolygon* currentPolygon = polygons->values;
    int currentPolygonIndex = 0;
    
    while(nodesLeft || tagsLeft) {
        fwrite(&area, sizeof(MapperAreaInfo), 1, file);
        fwrite(&(currentPolygon->info), sizeof(MapperPolygonInfo), 1, file);
        tagsLeft = writeMapperPartTags(file, &currentTag, AREA_ATTRIBUTES_COUNT);
        nodesLeft = writeMapperWayNodes(file, &(currentPolygon->wayNodes), &currentNode, AREA_NODES_COUNT);
        if(!nodesLeft) {
            if(currentPolygonIndex < polygons->count - 1) {
                currentPolygonIndex++;
                currentPolygon++;
                nodesLeft = 1;
                currentNode = 0;
            }
        }
        offset += sizeof(MapperArea);
    }
    removeAllMapperPartAttributes(&partAttributes);
    return offset;
}

void initMapperWriter(MapperWriter* self, const char* outputDirectory) {
    self->dbPath = strdup(outputDirectory);
    
    if(-1 == mkdir(outputDirectory, S_IRWXU) && errno != EEXIST) {  
        fprintf(stderr, "Error creating directory %s: %i\n", outputDirectory, errno);        
    }
    
    self->pointsFile = openFile("points", outputDirectory, "w+");
    self->waysFile = openFile("ways", outputDirectory, "w+");
    self->areasFile = openFile("areas", outputDirectory, "w+");	
    //self->pointsIndexFile = openFile("points.idx", outputDirectory, "w+");
    self->pointsLocationIndexFile = openFile("points.lidx", outputDirectory, "w+");
    //self->waysIndexFile = openFile("ways.idx", outputDirectory, "w+");
    self->waysLocationIndexFile = openFile("ways.lidx", outputDirectory, "w+");
    //self->areasIndexFile = openFile("areas.idx", outputDirectory, "w+");
    self->areasLocationIndexFile = openFile("areas.lidx", outputDirectory, "w+");
    
    initSimpleStringIndex(&(self->attributesIndex));
    simpleStringIndexOf(&(self->attributesIndex), UTF8_CAST "UNUSED");
    simpleStringIndexOf(&(self->attributesIndex), UTF8_CAST "CONTINUATION");
    simpleStringIndexOf(&(self->attributesIndex), UTF8_CAST "EMPTY_STRING");
    initSimpleStringIndex(&(self->typesIndex));
    simpleStringIndexOf(&(self->typesIndex), UTF8_CAST "UNUSED");
    
    initObjects2D(&(self->pointsLocations));
    initObjects4D(&(self->areasLocations));
    initObjects4D(&(self->waysLocations));    
}

void updateBounds(BBox* result, Coordinate x, Coordinate y) {
    if(x > result->max.x) {
        result->max.x = x;
    }
    if(x < result->min.x) {
        result->min.x = x;
    }
    if(y > result->max.y) {
        result->max.y = y;
    }
    if(y < result->min.y) {
        result->min.y = y;
    }
}

ZoomLevel getMinimalPointLevel(Node* node) {
    UTF8* placeType;
    if(placeType = valueForKey(&(node->tags), UTF8_CAST "place")) {
        if(utf8equal(placeType, UTF8_CAST "city")) {
            return 5;
        }
        if(utf8equal(placeType, UTF8_CAST "town")) {
            return 7;
        }
        if(utf8equal(placeType, UTF8_CAST "hamlet")) {
            return 11;
        }
    }
    return 14;
}

ZoomLevel getMaximalPointLevel(Node* node) {
    UTF8* placeType;
    if(placeType = valueForKey(&(node->tags), UTF8_CAST "place")) {
        if(utf8equal(placeType, UTF8_CAST "city")) {
            return 11;
        }
        if(utf8equal(placeType, UTF8_CAST "town")) {
            return 12;
        }
        if(utf8equal(placeType, UTF8_CAST "hamlet")) {
            return 14;
        }
    }
    return MAX_ZOOM_LEVEL;
}

static int pointsZoomCount[MAX_ZOOM_LEVEL - MIN_ZOOM_LEVEL + 1];

void writePoint(MapperWriter* self, UTF8* class, Node* node) {
    //printf("Writing point...\n");
    Coordinate x = mercatorX(node->info.lon);
    Coordinate y = mercatorY(node->info.lat);
    //printf("Adding to location index\n");
    ZoomLevel minZoomLevel = getMinimalPointLevel(node);
    ZoomLevel maxZoomLevel = getMaximalPointLevel(node);
    for(int z = minZoomLevel; z <= maxZoomLevel; z++) {
        pointsZoomCount[z]++;
    }
    add2DObject(&(self->pointsLocations), OsmPointMakeRaw(x, y), self->pointsOffset, minZoomLevel, maxZoomLevel);
    //printf("Adding to index\n");
    addTree16Node(&(self->pointsIndex), node->info.id, self->pointsOffset);
    //printf("Update bounds\n");
    updateBounds(&(self->mapInformation.bounds), x, y);
    //printf("Write to file\n");
    self->pointsOffset += writeMapperPoint(self->pointsFile, node->info.id, x, y, &(node->tags), simpleStringIndexOf(&(self->typesIndex), class), &(self->attributesIndex));
    //printf("Done.\n");
}

BBox* enlargeNodesBBox(BBox* result, MapperWayNodes* nodes) {
    for(int n = 0; n < nodes->count; n++) {
//        printf("    +(%i, %i)\n", nodes->values[n].location.x, nodes->values[n].location.y);
        updateBounds(result, nodes->values[n].location.x, nodes->values[n].location.y); 
    }
    return result;
}

BBox nodesBBox(MapperWayNodes* nodes) {
    BBox result = { {INT32_MAX, INT32_MAX}, {INT32_MIN, INT32_MIN} };
    return *enlargeNodesBBox(&result, nodes);
}

static MapperWayNodes wayNodes = {NULL, 0, 0};

void convertNodesInfoToMapperWayNodes(NodesInfo* infos, MapperWayNodes* nodes) {
    ensureMapperWayNodesCapacityForNNewElements(nodes, infos->count);
    for(int i=0;i<infos->count;i++) {
        nodes->values[i].id = infos->values[i].id;
        //printf("[%i, %i]\n", infos->values[i].lon, infos->values[i].lat);
        nodes->values[i].location.x = mercatorX(infos->values[i].lon);
        nodes->values[i].location.y = mercatorY(infos->values[i].lat);
    }
    nodes->count = infos->count;
}

ZoomLevel getMinimalWayLevel(Way* way) {
    UTF8* highwayType;
    if(highwayType = valueForKey(&(way->tags), UTF8_CAST "highway")) {
        if(utf8equal(highwayType, UTF8_CAST "trunk")) {
            return 4;
        }
        if(utf8equal(highwayType, UTF8_CAST "motorway") || utf8equal(highwayType, UTF8_CAST "trunk_link")) {
            return 5;
        }
        if(utf8equal(highwayType, UTF8_CAST "primary") || utf8equal(highwayType, UTF8_CAST "motorway_link") ) {
            return 7;
        }
        if(utf8equal(highwayType, UTF8_CAST "secondary") || utf8equal(highwayType, UTF8_CAST "primary_link") ) {
            return 9;
        }
        if(utf8equal(highwayType, UTF8_CAST "service")) {
            return 12;
        }        
        return 10;
    }
    if(utf8equal(valueForKey(&(way->tags), UTF8_CAST "boundary"), UTF8_CAST "administrative")) {
        UTF8* adminLevelString = valueForKey(&(way->tags), UTF8_CAST "admin_level");
        if(adminLevelString) {
            int adminLevel = atoi((const char*) adminLevelString);
            switch (adminLevel) {
                case 1:
                    return MIN_ZOOM_LEVEL;
                case 2:
                    return MIN_ZOOM_LEVEL;
                case 3:
                    return MIN_ZOOM_LEVEL;
                case 4:
                    return MIN_ZOOM_LEVEL;
                case 5:
                    return 4;
                case 6:
                    return 4;
                case 7:
                    return 6;
                case 8:
                    return 6;
                case 9:
                    return 8;
                case 10:
                    return 8;
                default:
                    if(adminLevel > 10) {
                        return 9;
                    }
            }
        }        
    }
    return 11;
}

ZoomLevel getMaximalWayLevel(Way* way) {
    return MAX_ZOOM_LEVEL;
}

static int waysZoomCount[MAX_ZOOM_LEVEL - MIN_ZOOM_LEVEL + 1];

void writeWay(MapperWriter* self, UTF8* class, OsmId groupId, Way* way) {
    //printf("Converting nodes...\n");
    convertNodesInfoToMapperWayNodes(&(way->wayNodes), &wayNodes);
    //printf("Calculating bbox...\n");
    BBox wayBox = nodesBBox(&wayNodes);
    //printf("Enlarge map...\n");
    enlargeNodesBBox(&(self->mapInformation.bounds), &wayNodes);
    //printf("Location Index...\n");
    ZoomLevel minZoomLevel = getMinimalWayLevel(way);
    ZoomLevel maxZoomLevel = getMaximalWayLevel(way);
    for(int z = minZoomLevel; z <= maxZoomLevel; z++) {
        waysZoomCount[z]++;
    }
    
    add4DObject(&(self->waysLocations), wayBox, self->waysOffset, minZoomLevel, maxZoomLevel);
    //printf("Index...\n");
    addTree16Node(&(self->waysIndex), way->info.id, self->waysOffset);
    //printf("Write...\n");
    self->waysOffset += writeMapperWay(self->waysFile, way->info.id, groupId, &wayNodes, wayBox, &(way->tags), simpleStringIndexOf(&(self->typesIndex), class), &(self->attributesIndex));
}

ZoomLevel getMinimalAreaLevel(PlainTags* tags, MapperPolygons* polygons) {
    if(valueForKey(tags, UTF8_CAST "building")) {
        return 12;
    }
    if(valueForKey(tags, UTF8_CAST "sport")) {
        return 14;
    }
    if(valueForKey(tags, UTF8_CAST "water")) {
        return 4;
    }
    if(valueForKey(tags, UTF8_CAST "landuse")) {
        return 4;
    }
    if(valueForKey(tags, UTF8_CAST "natural")) {
        return 4;
    }
    return 10;
}

ZoomLevel getMaximalAreaLevel(PlainTags* tags, MapperPolygons* polygons) {
    return MAX_ZOOM_LEVEL;
}

static int areasZoomCount[MAX_ZOOM_LEVEL - MIN_ZOOM_LEVEL + 1];

void writeArea(MapperWriter* self, UTF8* class, OsmId id, PlainTags* tags, MapperPolygons* polygons) {
    BBox areaBox = { {INT_MAX, INT_MAX}, {INT_MIN, INT_MIN} };
    for(int p = 0; p < polygons->count; p++){
        enlargeNodesBBox(&areaBox, &(polygons->values[p].wayNodes));
        enlargeNodesBBox(&(self->mapInformation.bounds), &(polygons->values[p].wayNodes));
    }
    //printf("Area %i: [(%i,%i), (%i,%i)]\n", id, areaBox.min.x, areaBox.min.y, areaBox.max.x, areaBox.max.y);
    ZoomLevel minZoomLevel = getMinimalAreaLevel(tags, polygons);
    ZoomLevel maxZoomLevel = getMaximalAreaLevel(tags, polygons);
    for(int z = minZoomLevel; z <= maxZoomLevel; z++) {
        areasZoomCount[z]++;
    }
    
    add4DObject(&(self->areasLocations), areaBox, self->areasOffset, minZoomLevel, maxZoomLevel);
    addTree16Node(&(self->areasIndex), id, self->areasOffset);
    self->areasOffset += writeMapperArea(self->areasFile, id, polygons, areaBox, tags, simpleStringIndexOf(&(self->typesIndex), class), &(self->attributesIndex));
}

void writeMapInformation(MapperWriter* self, MapInformation* mapInformation) {
    self->mapInformation.bounds = mapInformation->bounds;
    self->mapInformation.id = mapInformation->id;
    self->mapInformation.name = utf8dup(mapInformation->name);
    self->mapInformation.nameLength = utf8size(mapInformation->name);
    FILE* file = openFile("info", self->dbPath, "w+");
    fwrite(&(self->mapInformation), sizeof(MapInformation) - sizeof(UTF8*), 1, file);
    fwrite(self->mapInformation.name, sizeof(UTF8), self->mapInformation.nameLength, file);
    fclose(file);
}

void closeMapperWriter(MapperWriter* self) {
    //printf("Write attributes index...\n");
    FILE* attributesIndexFile = openFile("attributes", self->dbPath, "w+");
    writeSimpleStringIndex(&(self->attributesIndex), attributesIndexFile);
    fclose(attributesIndexFile);
    //printf("Write types index...\n");
    FILE* typesIndexFile = openFile("types", self->dbPath, "w+");
    writeSimpleStringIndex(&(self->typesIndex), typesIndexFile);
    fclose(typesIndexFile);

    //printf("Write points index...\n");
    //saveTree16ToFile(&(self->pointsIndex), self->pointsIndexFile, 0, 0);
    //printf("Create point location index...\n");
    Tree2D* pointsLocationTree = index2DObjects(&(self->pointsLocations));
    //printf("Write point location index...\n");
    write2DTree(pointsLocationTree, self->pointsLocationIndexFile);
    free2DTree(pointsLocationTree);
    
    //printf("Write ways index...\n");    
    //saveTree16ToFile(&(self->waysIndex), self->waysIndexFile, 0, 0);
    //printf("Create ways location index...\n");
    Tree4D* locationTree = index4DObjects(&(self->waysLocations));
    //printf("Write ways location index %i...\n", locationTree);
    write4DTree(locationTree, self->waysLocationIndexFile);
    free4DTree(locationTree);

    //printf("Write areas index...\n");
    //saveTree16ToFile(&(self->areasIndex), self->areasIndexFile, 0, 0);
    //printf("Create areas location index...\n");    
    locationTree = index4DObjects(&(self->areasLocations));
    //printf("Write areas location index...\n");    
    write4DTree(locationTree, self->areasLocationIndexFile);
    free4DTree(locationTree);
    //printf("Closed.\n");    
}


void initMapperConverter(MapperConverter* self, OsmDbReader* reader, const char* outputDirectory) {
    self->writer = calloc(sizeof(MapperWriter), 1);
    initMultipolygonRelations(&(self->multipolygons));
    initMapperWriter(self->writer, outputDirectory);
    self->reader = reader;
}


CollectionImplGeneric(PMultipolygonRelation, MultipolygonRelations, 100)

void prepareIndicies(MapperConverter* self) {
    Relation* relation;
    printf("Preparing indicies...\n");
    while(relation = nextRelation(self->reader)) {
        if(utf8equal(valueForKey(&(relation->tags), UTF8_CAST "type"), UTF8_CAST "multipolygon")) {
            //printf("Relation %i: %s with %i members\n", relation->info.id, valueForKey(&(relation->tags), UTF8_CAST "type"), relation->relationMembers.count);
            MultipolygonRelation* multipolygon = malloc(sizeof(MultipolygonRelation));
            multipolygon->converted = 0;
            multipolygon->relationId = relation->info.id;
            initOsmIds(&(multipolygon->outers));
            initOsmIds(&(multipolygon->inners));
            //printf("Relation %i:\n", relation->info.id);
            for(int m=0; m < relation->relationMembers.count; m++) {
                if(relation->relationMembers.values[m].type == OSM_ENTITY_WAY) {
                    if(utf8equal(relation->relationMembers.values[m].role, UTF8_CAST "outer") || utf8equal(relation->relationMembers.values[m].role, UTF8_CAST "")) {
                        addToOsmIds(&(multipolygon->outers), relation->relationMembers.values[m].ref);
              //          printf("  Outer %i\n",relation->relationMembers.values[m].ref);
                    } else if(utf8equal(relation->relationMembers.values[m].role, UTF8_CAST "inner")) {
                        addToOsmIds(&(multipolygon->inners), relation->relationMembers.values[m].ref);
                //        printf("  Inner %i\n",relation->relationMembers.values[m].ref);
                    } else {
                        fprintf(stderr, "Relation %i. Invalid role %s in multipolygon relation.\n", relation->info.id, relation->relationMembers.values[m].role);
                    }
                } else {
                    fprintf(stderr, "Relation %i. Not ways in multipolygon relation, but %i.\n", relation->info.id, relation->relationMembers.values[m].type);
                }
            }/*
            for(int i=0;i<multipolygon->inners.count;i++) {
                printf("  Inner:: %i\n", multipolygon->inners.values[i]);
            }*/
            addToMultipolygonRelations(&(self->multipolygons), multipolygon);
            //printf("Relation Done\n");
        }
    }
    printf("Done.\n");
}

UTF8* pointClassByTags(PlainTags* tags) {
    if(valueForKey(tags, UTF8_CAST "amenity")) {
        return UTF8_CAST "Amenity"; 
    } else if(valueForKey(tags, UTF8_CAST "shop")) {
        return UTF8_CAST "Shop"; 
    } else if(valueForKey(tags, UTF8_CAST "tourism")) {
        return UTF8_CAST "Tourism"; 
    } else if(valueForKey(tags, UTF8_CAST "historic")) {
        return UTF8_CAST "Historic"; 
    } else if(valueForKey(tags, UTF8_CAST "power")) {
        return UTF8_CAST "Power"; 
    } else if(valueForKey(tags, UTF8_CAST "place")) {
        return UTF8_CAST "Place"; 
    } else if(utf8equal(valueForKey(tags, UTF8_CAST "highway"), UTF8_CAST "traffic_signals")) {
        return UTF8_CAST "TrafficSignals"; 
    } else if(valueForKey(tags, UTF8_CAST "crossing") 
              || utf8equal(valueForKey(tags, UTF8_CAST "highway"), UTF8_CAST "crossing") 
              || utf8equal(valueForKey(tags, UTF8_CAST "railway"), UTF8_CAST "crossing")) {
        return UTF8_CAST "Crossing"; 
    }

    return NULL;
}

void convertNodes(MapperConverter* self) {    
    Node* node;
    printf("Converting nodes...\n");
    int totalCount = 0;
    int pointsCount = 0;
    while(node = nextNode(self->reader)) {
        //printf("Converting node...\n");
        if(node->tags.count > 0) {
            //            printf("Determine class...\n");
            UTF8* className = pointClassByTags(&(node->tags));
            if(className) {
                //                printf("Class: %s\n", className);
                writePoint(self->writer, className, node);
                pointsCount++;
            }
        }
        //        printf("Converted...\n");
        totalCount++;
    }
    printf("%i of %i nodes converted.\n", pointsCount, totalCount);
}

UTF8* wayClassByTags(PlainTags* tags) {
    if (utf8equal(valueForKey(tags, UTF8_CAST"power"), UTF8_CAST "line")) {
        return UTF8_CAST "PowerWay";
    } else if (valueForKey(tags, UTF8_CAST"boundary") ) {
        return UTF8_CAST "Boundary";
    } else if (valueForKey(tags, UTF8_CAST"highway")) {
        return UTF8_CAST "Highway";
    } else if (valueForKey(tags, UTF8_CAST"railway")) {
        return UTF8_CAST "Railway";
    } else {
        UTF8* waterway = valueForKey(tags, UTF8_CAST"waterway");
        if (waterway && !utf8equal(waterway, UTF8_CAST"riverbank")) {
            return UTF8_CAST "Waterway";
        } 
    }  
    return NULL;
}

UTF8* areaClassByTags(PlainTags* tags) {
    if(valueForKey(tags, UTF8_CAST "building")) {
        return UTF8_CAST "Building";
    } else if(valueForKey(tags, UTF8_CAST "landuse")) {
        return UTF8_CAST "Landuse";
    } else if(valueForKey(tags, UTF8_CAST "leisure")) {
        return UTF8_CAST "Leisure";
    } else if(utf8equal(valueForKey(tags, UTF8_CAST "waterway"), UTF8_CAST "riverbank")) {
        return UTF8_CAST "Water";
    } else if(valueForKey(tags, UTF8_CAST "sport")) {
        return UTF8_CAST "Sport";
    } else if(valueForKey(tags, UTF8_CAST "natural")) {
        return UTF8_CAST "Natural";
    } else if(valueForKey(tags, UTF8_CAST "power")) {
        return UTF8_CAST "PowerArea";
    } else if(tags->count > 0) {
        return UTF8_CAST "Area";
    }
    return NULL;
}

void convertWays(MapperConverter* self) {
    printf("Converting ways...\n");
    Way* way;
    int totalCount = 0;
    int waysCount = 0;
    int areasCount = 0;
    while (way = nextWay(self->reader)) {
        if(way->tags.count > 0 && way->wayNodes.count > 0) {
            //printf("Converting way %i with tags && nodes\n", way->info.id);
            int cycled = way->wayNodes.count >= 3 && way->wayNodes.values[0].id == way->wayNodes.values[way->wayNodes.count-1].id;
            //printf("  Way(%i) is %s\n",way->wayNodes.count, cycled ? "cycled" : "not cycled");
  

            int area = utf8equal(valueForKey(&(way->tags), UTF8_CAST "area"), UTF8_CAST "yes");
            int saved = 0;
            
            if(!area) {
                UTF8* wayClassName = wayClassByTags(&(way->tags));
                if(wayClassName) {
                    //printf("Way is %s\n", wayClassName);
                    writeWay(self->writer, wayClassName, way->info.id, way);                        
                    //printf("Way Written\n");
                    waysCount++;
                    saved = 1;
                }
            }
            if(!saved && cycled) {
                //printf("  Seems to be area.\n");
                int relationsFound = 0;
                UTF8* areaClassName = areaClassByTags(&(way->tags));
                if(areaClassName) {
                    //printf("Area is %s\n", areaClassName);
                    MapperPolygon mainPolygon;
                    mainPolygon.info.id = way->info.id;
                    mainPolygon.info.role = OuterAreaPart;
                    mainPolygon.tags = way->tags;
                    //printf("Converting wayNodes...\n");
                    initMapperWayNodes(&(mainPolygon.wayNodes));
                    convertNodesInfoToMapperWayNodes(&(way->wayNodes), &(mainPolygon.wayNodes));
                    //printf("Search for multipolygons...\n"); 
                    PMultipolygonRelation* multipolygon = self->multipolygons.values;
                    int mcount = self->multipolygons.count;
                    for(int m = 0; m < mcount; m++, multipolygon++) {
                        //printf("%i\n", self->multipolygons.values[m]->relationId);
                        OsmId* outer = multipolygon[0]->outers.values;
                        int ocount = multipolygon[0]->outers.count;
                        for(int o=0;o < ocount ; o++, outer++) {
                            //printf("  %i\n", self->multipolygons.values[m]->outers.values[o]);
                            if(!multipolygon[0]->converted && *outer == way->info.id) {
                                //printf("Multipolygon found...\n");
                                multipolygon[0]->converted = 1;
                                relationsFound = 1;
                                
                                MapperPolygons* polygons = malloc(sizeof(MapperPolygons));
                                initMapperPolygons(polygons);
                                
                                addToMapperPolygons(polygons, mainPolygon);
                                
                                for(int oo=0; oo < ocount; oo++) {
                                    if(oo != o) {
                                        Way* otherOuterWay = wayWithId(self->reader, multipolygon[0]->outers.values[oo]);
                                        if(otherOuterWay) {
                                            MapperPolygon otherPolygon;
                                            otherPolygon.info.id = otherOuterWay->info.id;
                                            otherPolygon.info.role = OuterAreaPart;
                                            otherPolygon.tags = otherOuterWay->tags;
                                            initMapperWayNodes(&(otherPolygon.wayNodes));
                                            convertNodesInfoToMapperWayNodes(&(otherOuterWay->wayNodes), &(otherPolygon.wayNodes));
                                            addToMapperPolygons(polygons, otherPolygon);
                                            clearNodesInfo(&(otherOuterWay->wayNodes));
                                            clearPlainTags(&(otherOuterWay->tags));
                                            
                                            free(otherOuterWay);
                                        } else {
                                            fprintf(stderr, "Multipolygon %i. Invalid reference to outer way %i in multipolygon relation.\n", self->multipolygons.values[m]->relationId, self->multipolygons.values[m]->outers.values[oo]);
                                        }
                                    }
                                }
                                
                                for(int i=0; i < multipolygon[0]->inners.count; i++) {
                                    Way* otherInnerWay = wayWithId(self->reader, multipolygon[0]->inners.values[i]);
                                    if(otherInnerWay) {
                                        MapperPolygon otherPolygon;
                                        otherPolygon.info.id = otherInnerWay->info.id;
                                        otherPolygon.info.role = InnerAreaPart;
                                        otherPolygon.tags = otherInnerWay->tags;
                                        initMapperWayNodes(&(otherPolygon.wayNodes));
                                        convertNodesInfoToMapperWayNodes(&(otherInnerWay->wayNodes), &(otherPolygon.wayNodes));
                                        addToMapperPolygons(polygons, otherPolygon);
                                        clearNodesInfo(&(otherInnerWay->wayNodes));
                                        clearPlainTags(&(otherInnerWay->tags));
                                        free(otherInnerWay);
                                    } else {
                                        fprintf(stderr, "Multipolygon %i. Invalid reference to inner way %i in multipolygon relation.\n", self->multipolygons.values[m]->relationId, self->multipolygons.values[m]->outers.values[i]);
                                    }
                                }
                                
                                writeArea(self->writer, areaClassName, way->info.id, &(way->tags), polygons);
                                
                                polygons->values[0].wayNodes.values = NULL;
                                freeMapperPolygons(polygons);
                                areasCount++;
                            }
                        }
                    }
                    
                    if(!relationsFound) {
                        areasCount++;
                        MapperPolygons* polygons = malloc(sizeof(MapperPolygons));
                        initMapperPolygons(polygons);
                        
                        addToMapperPolygons(polygons, mainPolygon);

                        writeArea(self->writer, areaClassName, way->info.id, &(way->tags), polygons);
                        freeMapperPolygons(polygons);
                    } else {
                        clearMapperWayNodes(&(mainPolygon.wayNodes));
                    }
                }
            }
        }
        totalCount++;
       // printf("End way.\n");
    }
    printf("%i of %i ways converted.\n", waysCount, totalCount);
    printf("%i of %i areas converted.\n", areasCount, totalCount);
    printf("\n             Statistics on zoom levels\n");
    printf("---------------------------------------------------------\n");
    printf("| Level |   Points  |   Ways   |   Areas   ||   Total   |\n");
    printf("|-------|-----------|----------|-----------||-----------|\n");
    for(int z = MIN_ZOOM_LEVEL; z <= MAX_ZOOM_LEVEL; z++) {
        printf("| %5i | %9i | %8i | %9i || %9i |\n", z, pointsZoomCount[z], waysZoomCount[z], areasZoomCount[z],
               pointsZoomCount[z]+ waysZoomCount[z]+ areasZoomCount[z]);
    }
    printf("---------------------------------------------------------\n");
}

void convertToMapper(MapperConverter* self) {
    prepareIndicies(self);
    convertNodes(self);
    convertWays(self);
    closeMapperWriter(self->writer);
}



#pragma mark Reader

void initMapperReader(MapperReader* self, const char* mapDirectory) {
    self->dbPath = strdup(mapDirectory);
        
    self->pointsFile = openFile("points", mapDirectory, "r+");
    self->waysFile = openFile("ways", mapDirectory, "r+");
    self->areasFile = openFile("areas", mapDirectory, "r+");	
    self->pointsLocationIndexFile = openFile("points.lidx", mapDirectory, "r+");
    self->waysLocationIndexFile = openFile("ways.lidx", mapDirectory, "r+");
    self->areasLocationIndexFile = openFile("areas.lidx", mapDirectory, "r+");
    FILE* attributesIndexFile = openFile("attributes", mapDirectory, "r+");
    initSimpleStringIndexFromFile(&(self->attributesIndex), attributesIndexFile);
    fclose(attributesIndexFile);
    FILE* typesIndexFile = openFile("types", mapDirectory, "r+");
    initSimpleStringIndexFromFile(&(self->typesIndex), attributesIndexFile);    
    fclose(typesIndexFile);
    //TODO read map information
}

void closeMapperReader(MapperReader* self) {
    fclose(self->pointsFile);
    fclose(self->waysFile);
    fclose(self->areasFile);
    fclose(self->pointsLocationIndexFile);
    fclose(self->waysLocationIndexFile);
    fclose(self->areasLocationIndexFile);
    free(self->dbPath);
}
