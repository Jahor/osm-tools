/*
 *  omm.h
 *  OSMapper
 *
 *  Created by Egor Leonenko on 28.9.09.
 *  Copyright 2009 iTransition. All rights reserved.
 *
 */

#include "osm.h"
#include "Tree16.h"
#include <mysql.h>

typedef enum {
    MYSQL_PARAM_INTEGER,
    MYSQL_PARAM_LONG,
    MYSQL_PARAM_TEXT
} MysqlParameter;

typedef struct {
    char** templateParts;
    int templatePartsCount;
    int templateLength;
    MysqlParameter* parameters;
    char parametersCount;
    MYSQL* db;
} mysql_stmt;

typedef struct {
    CountryPolygon* polygon;
    MYSQL db;
    
    mysql_stmt* insertNodeStatement;
    mysql_stmt* insertWayNodeStatement;
    mysql_stmt* insertWayStatement;
    mysql_stmt* insertRelationStatement;
    mysql_stmt* insertRelationMemberStatement;
    
    mysql_stmt* insertNodeTagStatement;
    mysql_stmt* insertWayTagStatement;
    mysql_stmt* insertRelationTagStatement;
    
    mysql_stmt* updateNodeStatement;
    mysql_stmt* updateWayStatement;
    mysql_stmt* updateRelationStatement;
    
    mysql_stmt* deleteNodeStatement;
    mysql_stmt* deleteWayStatement;
    mysql_stmt* deleteRelationStatement;
    
    mysql_stmt* deleteWayNodesStatement;
    mysql_stmt* deleteRelationMembersStatement;
    
    mysql_stmt* deleteNodeTagsStatement;
    mysql_stmt* deleteWayTagsStatement;
    mysql_stmt* deleteRelationTagsStatement;
    
    mysql_stmt* nodeExistsStatement;
    mysql_stmt* wayExistsStatement;
    mysql_stmt* relationExistsStatement;
    
    Tree16 nodesIndex;
    Tree16 waysIndex;
    Tree16 relationsIndex;
    
    ExistCheck ifNodeExists;
    ExistCheck ifWayExists;
    ExistCheck ifRelationExists;
} MCountry;

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
    MCountry* countries;
    int countriesCount;
} osm2omm;

typedef struct {
    osm2omm base;
} osd2omm;

typedef struct {
    MYSQL db;
    
    mysql_stmt* nodeStatement;
    mysql_stmt* nodeTagsStatement;
    
    mysql_stmt* wayStatement;
    mysql_stmt* wayTagsStatement;
    mysql_stmt* wayNodesStatement;
    
    mysql_stmt* relationStatement;
    mysql_stmt* relationTagsStatement;
    mysql_stmt* relationMembersStatement;
    
    mysql_stmt* wayWithIdStatement;
    mysql_stmt* nodeWithIdStatement;
    mysql_stmt* relationWithIdStatement;
    
    
    Node currentNode;
    Way currentWay;
    Relation currentRelation;
} omm;

#pragma mark osm2olm
void initOsm2Omm(osm2omm* self, const char* host, const char* user, const char* password, CountryPolygon* polygons, int polygonsCount);
void convertOsm2OmmFromFile(osm2omm* self, const char *filename);
void convertOsm2OmmFromStdin(osm2omm* self);
void closeOsm2Omm(osm2omm* self);

#pragma mark osd2olm
void initOsd2Omm(osd2omm* self, const char* host, const char* user, const char* password, CountryPolygon* polygon, char fullMemory);
void convertOsd2OmmFromFile(osd2omm* self, const char *filename);
void convertOsd2OmmFromStdin(osd2omm* self);
void closeOsd2Omm(osd2omm* self);

#pragma mark olm reader
OsmDbReader* newOmmReader(const char* host, const char* user, const char* password, const char* db);
