/*
 *  osm.h
 *  OSMapper
 *
 *  File contains function to work with osm xml file format.
 *
 *  Created by Egor Leonenko on 9.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#ifndef _OSM_H_
#define _OSM_H_
#include "utf.h"
#include "CountryPolygon.h"
#include "MapperTypes.h"
#include "collections.h"
#include <time.h>

typedef enum {
    OSM_ENTITY_NONE = 0,
    OSM_ENTITY_MAP = 1,
    OSM_ENTITY_BOUNDARY = 2,
    OSM_ENTITY_NODE = 3,
    OSM_ENTITY_WAY = 4,
    OSM_ENTITY_RELATION = 5,
    OSM_ENTITY_CHANGE_GROUP = 6,
    OSM_ENTITY_CHANGE_SET = 7
} OsmEntityType;

typedef enum {
    OSM_CHANGE_NONE = 0,
    OSM_CHANGE_CREATE = 1,
    OSM_CHANGE_MODIFY = 2,
    OSM_CHANGE_DELETE = 3,
    OSM_CHANGE_COUNT = 4,
} OsmChangeType;

typedef time_t OsmTimestamp;

typedef struct {
    OsmId id;
    OsmTimestamp timestamp;
} WayInfo;

typedef struct {
    OsmId id;
    Coordinate lat;
    Coordinate lon;
    OsmTimestamp timestamp;
} NodeInfo;

Collection(NodeInfo, NodesInfo)

typedef struct {
    OsmId id;
    OsmTimestamp timestamp;
} RelationInfo;

typedef struct {
    OsmId ref;
    OsmEntityType type;
    UTF8* role;
} RelationMemberInfo;

Collection(RelationMemberInfo, RelationMembers);

typedef struct {
    OsmId ref;
} WayNodeInfo;

Collection(WayNodeInfo, WayNodes)

typedef struct {
    UTF8* key;
    UTF8* value;
} PlainTag;

CollectionWithCustomAdd(PlainTag, PlainTags)

UTF8* valueForKey(PlainTags* tags, UTF8* key);

typedef struct {
    RelationInfo info;
    PlainTags tags;
    RelationMembers relationMembers;
} Relation;

Collection(Relation, Relations)

typedef struct {
    Relation base;
    OsmChangeType change;
} RelationChange;

Collection(RelationChange, RelationChanges)

typedef struct {
    NodeInfo info;
    PlainTags tags;
} Node;

Collection(Node, Nodes)    

typedef struct {
    WayInfo info;
    PlainTags tags;
    NodesInfo wayNodes;
} Way;

StaticCollection(Way, Ways)

typedef void (*TagProcessor)(void* self, OsmEntityType type, UTF8* key, UTF8* value);
typedef void (*NodeProcessor)(void* self, OsmId id, Coordinate lat, Coordinate lon, OsmTimestamp timestamp);
typedef void (*WayNodeProcessor)(void* self, OsmId ref);
typedef void (*WayProcessor)(void* self, OsmId id, OsmTimestamp timestamp);
typedef void (*RelationMemberProcessor)(void* self, OsmId ref, OsmEntityType type, UTF8* role);
typedef void (*RelationProcessor)(void* self, OsmId id, OsmTimestamp timestamp);

typedef void (*NodePostprocessor)(void* self);
typedef void (*WayPostprocessor)(void* self);
typedef void (*RelationPostprocessor)(void* self);

typedef char (*ExistCheck)(void* country, OsmId id);

typedef struct {
    void* target;
    
    NodeProcessor newNode;
    WayProcessor newWay;
    RelationProcessor newRelation;
    
    TagProcessor newTag;
    WayNodeProcessor newWayNode;
    RelationMemberProcessor newRelationMember;
    
    NodePostprocessor finishNode[OSM_CHANGE_COUNT];
    WayPostprocessor finishWay[OSM_CHANGE_COUNT];
    RelationPostprocessor finishRelation[OSM_CHANGE_COUNT];
    
    OsmEntityType currentEntityType;
    OsmChangeType currentChangeType;
} OsmStreamReader;

void initOsmStreamReader(OsmStreamReader* reader, void* target);
void closeOsmStreamReader(OsmStreamReader* reader);

void readOsmFromFile(OsmStreamReader* reader, const char* filename);

void readOsmFromStdin(OsmStreamReader* reader);


typedef Node* (*GetNextNode)(void* self);
typedef Way* (*GetNextWay)(void* self);
typedef Relation* (*GetNextRelation)(void* self);

typedef Node* (*GetNode)(void* self, OsmId id);
typedef Way* (*GetWay)(void* self, OsmId id);
typedef Relation* (*GetRelation)(void* self, OsmId id);

typedef void (*DbReaderCloser)(void* self);

typedef void (*Restarter)(void* self);

typedef struct {
    void* target;
    
    GetNextNode nextNode;
    GetNextWay nextWay;
    GetNextRelation nextRelation;
    
    GetNode nodeWithId;
    GetWay wayWithId;
    GetRelation relationWithId;    
    
    Restarter restartNodes;
    Restarter restartWays;
    Restarter restartRelations;
    
    
    DbReaderCloser close;
} OsmDbReader;

void initOsmDbReader(OsmDbReader* reader, void* target);
void closeOsmDbReader(OsmDbReader* reader);

Node* nextNode(OsmDbReader* self);
Way* nextWay(OsmDbReader* self);
Relation* nextRelation(OsmDbReader* self);

Way* wayWithId(OsmDbReader* self, OsmId id);

void restartNodes(OsmDbReader* self);
void restartWays(OsmDbReader* self);
void restartRelations(OsmDbReader* self);


OsmEntityType string2relationMemberType(UTF8* typeString);
const char* relationMemberType2String(OsmEntityType type);
#endif
