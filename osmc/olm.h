/*
 *  olm.h
 *  OSMapper
 *
 *  File contains function to work with osm in sqlite.
 *
 *  Created by Egor Leonenko on 9.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */
#include "osm.h"
#include "Tree16.h"
#include <sqlite3.h>

typedef struct {
    CountryPolygon* polygon;
    sqlite3* db;
    sqlite3_stmt* insertNodeStatement;
    sqlite3_stmt* insertWayNodeStatement;
    sqlite3_stmt* insertWayStatement;
    sqlite3_stmt* insertRelationStatement;
    sqlite3_stmt* insertRelationMemberStatement;
    
    sqlite3_stmt* insertNodeTagStatement;
    sqlite3_stmt* insertWayTagStatement;
    sqlite3_stmt* insertRelationTagStatement;
    
    sqlite3_stmt* updateNodeStatement;
    sqlite3_stmt* updateWayStatement;
    sqlite3_stmt* updateRelationStatement;
    
    sqlite3_stmt* deleteNodeStatement;
    sqlite3_stmt* deleteWayStatement;
    sqlite3_stmt* deleteRelationStatement;
    
    sqlite3_stmt* deleteWayNodesStatement;
    sqlite3_stmt* deleteRelationMembersStatement;
    
    sqlite3_stmt* deleteNodeTagsStatement;
    sqlite3_stmt* deleteWayTagsStatement;
    sqlite3_stmt* deleteRelationTagsStatement;
    
    sqlite3_stmt* nodeExistsStatement;
    sqlite3_stmt* wayExistsStatement;
    sqlite3_stmt* relationExistsStatement;
    
    Tree16 nodesIndex;
    Tree16 waysIndex;
    Tree16 relationsIndex;
    
    ExistCheck ifNodeExists;
    ExistCheck ifWayExists;
    ExistCheck ifRelationExists;
} LCountry;

typedef struct {
    OsmStreamReader reader;
    
    WayInfo way;
    NodeInfo node;
    RelationInfo relation;
    
    RelationChanges relations;
    
    PlainTags tags;
        
    RelationMembers relationMembers;
    
    WayNodes wayNodes;
    
    OsmEntityType currentEntityType;
    LCountry* countries;
    int countriesCount;
} osm2olm;

typedef struct {
    osm2olm base;
} osd2olm;

typedef struct {
    sqlite3* db;
    sqlite3_stmt* nodeStatement;
    sqlite3_stmt* nodeTagsStatement;
    
    sqlite3_stmt* wayStatement;
    sqlite3_stmt* wayTagsStatement;
    sqlite3_stmt* wayNodesStatement;
    
    sqlite3_stmt* relationStatement;
    sqlite3_stmt* relationTagsStatement;
    sqlite3_stmt* relationMembersStatement;
    
    sqlite3_stmt* wayWithIdStatement;
    sqlite3_stmt* nodeWithIdStatement;
    sqlite3_stmt* relationWithIdStatement;
        
    Node currentNode;
    Way currentWay;
    Relation currentRelation;
} olm;

#pragma mark osm2olm
void initOsm2OlmWithOutputDirectory(osm2olm* self, const char* outputDirectory, CountryPolygon* polygons, int polygonsCount);
void convertOsm2OlmFromFile(osm2olm* self, const char *filename);
void convertOsm2OlmFromStdin(osm2olm* self);
void closeOsm2Olm(osm2olm* self);

#pragma mark osd2olm
void initOsd2Olm(osd2olm* self, const char* file, CountryPolygon* polygon, char fullMemory);
void convertOsd2OlmFromFile(osd2olm* self, const char *filename);
void convertOsd2OlmFromStdin(osd2olm* self);
void closeOsd2Olm(osd2olm* self);

#pragma mark olm reader
OsmDbReader* newOlmReader(const char* directory);
