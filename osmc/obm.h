/*
 *  obm.h
 *  OSMapper
 *
 *  File contains function to work with obm file format.
 *
 *  Created by Egor Leonenko on 7.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#include "Tree16.h"
#include "SimpleStringIndex.h"
#include "CountryPolygon.h"
#include "osm.h"


#define NODE_ATTRIBUTES_COUNT 2
#define WAY_ATTRIBUTES_COUNT 2
#define RELATION_ATTRIBUTES_COUNT 2
#define RELATION_MEMBERS_COUNT 2
#define WAY_NODES_COUNT 11
#define ATTRIBUTE_VALUE_LENGTH 32

typedef long int BId;
#define atobid atol

typedef struct {
    int key;
    UTF8 value[ATTRIBUTE_VALUE_LENGTH];
} BTag;

typedef struct {
    int key;
    UTF8* value;
} BTagInMemory;

typedef struct {
    BTagInMemory* values;
    int count;
} BTagsInMemory;

typedef struct {
    NodeInfo info;
    BTag tags[NODE_ATTRIBUTES_COUNT];
} BNode;

typedef struct {
    BId ref;
} BWayNode;

typedef struct {
    WayInfo info;
    BTag tags[WAY_ATTRIBUTES_COUNT];
    BWayNode nodes[WAY_NODES_COUNT];
} BWay;

typedef struct {
    BId ref;
    OsmEntityType type;
    int role;
} BRelationMember;

typedef struct {
    RelationInfo info;
    BTag tags[RELATION_ATTRIBUTES_COUNT];
    BRelationMember members[RELATION_MEMBERS_COUNT];
} BRelation;

typedef struct {
    int nodesOffset;
    int waysOffset;
    int relationsOffset;
    
    Tree16 nodesIndex;
    Tree16 waysIndex;
    Tree16 relationsIndex;
    
    FILE* nodesFile;
    FILE* waysFile;
    FILE* relationsFile;
    
    char* outputDirectory;
    
    CountryPolygon* polygon;
    
} BCountry;

typedef struct {
    BTag* values;
    int count;
    int capacity;
} BTags;

typedef struct {
    RelationInfo info;
    struct {
        BRelationMember* values;
        int count;
    } relationMembers;
    BTags tags;
    BTagsInMemory tagsInMemory;
} BRelationInMemory;

typedef struct {
    BCountry* countries;
    int countriesCount;
    OsmStreamReader reader;
	int compressed;
    
    SimpleStringIndex keysIndex;
    SimpleStringIndex rolesIndex;
    
    WayInfo way;
    NodeInfo node;
    RelationInfo relation;
    
    struct {
        BRelationInMemory* values;
        int capacity;
        int count;
    } relations;
    
    BTags tags;
    
    struct {
        BWayNode* values;
        int capacity;
        int count;
    } wayNodes;
    
    struct {
        BRelationMember* values;
        int capacity;
        int count;
    } relationMembers;
        
    OsmEntityType currentEntityType;
} osm2obm;

typedef struct {
    int cacheNodes;
    Nodes nodes;
    Node* currentNode;
    FILE* nodesFile;
    FILE* waysFile;
    FILE* relationsFile;
    
    Tree16OnFile nodesIndex;
    Tree16OnFile waysIndex;
    Tree16OnFile relationsIndex;
    

    Way currentWay;
    Relation currentRelation;
    
    BTag* tags;
    
    SimpleStringIndex keysIndex;
    SimpleStringIndex rolesIndex;    
} obm;

#pragma mark osm2obm
void initOsm2obmWithOutputDirectory(osm2obm* self, const char* outputDirectory, CountryPolygon* polygon, int polygonsCount, char compress);
void convertOsm2ObmFromFile(osm2obm* self, const char *filename);
void convertOsm2ObmFromStdin(osm2obm* self);
void closeOsm2obm(osm2obm* self);

#pragma mark Read obm

OsmDbReader* newObmReader(const char* directory, int cacheNodes);
