/*
 *  obm.c
 *  OSMapper
 *
 *  Created by Egor Leonenko on 7.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#include "obm.h"
#include "utils.h"
#include <math.h>
#include <memory.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#pragma mark osm2obm

#define ATTRIBUTE_CONTINUATION 1
#define UNUSED_ATTRIBUTE 0

static Node* bNodeWithId(void* self, OsmId id);
static Way* bWayWithId(void* self, OsmId id);
static Relation* bRelationWithId(void* self, OsmId id);

static int nodeBelongsCountry(osm2obm* self, BCountry* country) {
    return isPointInPolygon(self->node.lat, self->node.lon, country->polygon);
}

static int wayBelongsCountry(osm2obm* self, BCountry* country) {
    for(int n = 0; n < self->wayNodes.count; n++) {
        if(isInTree16(&(country->nodesIndex), self->wayNodes.values[n].ref)) {
            return 1;
        }
    }
    return 0;
}

static int relationBelongsCountry(BRelationInMemory* relation, BCountry* country, Tree16* relationsIndex) {
    for(int m = 0; m < relation->relationMembers.count; m++) {
        if(relation->relationMembers.values[m].type == OSM_ENTITY_NODE) {
            if(isInTree16(&(country->nodesIndex), relation->relationMembers.values[m].ref)) {
                return 1;
            }            
        } else if(relation->relationMembers.values[m].type == OSM_ENTITY_WAY) {
            if(isInTree16(&(country->waysIndex), relation->relationMembers.values[m].ref)) {
                return 1;
            }            
        } else if(relation->relationMembers.values[m].type == OSM_ENTITY_RELATION) {
            if(isInTree16(relationsIndex, relation->relationMembers.values[m].ref)) {
                return 1;
            }            
        } 
    }
    return 0;
}

static int relationIsFullyBelongsCountry(BRelationInMemory* relation, BCountry* country, Tree16* relationsIndex) {
    for(int m = 0; m < relation->relationMembers.count; m++) {
        if(relation->relationMembers.values[m].type == OSM_ENTITY_NODE) {
            if(!isInTree16(&(country->nodesIndex), relation->relationMembers.values[m].ref)) {
                return 0;
            }            
        } else if(relation->relationMembers.values[m].type == OSM_ENTITY_WAY) {
            if(!isInTree16(&(country->waysIndex), relation->relationMembers.values[m].ref)) {
                return 0;
            }            
        } else if(relation->relationMembers.values[m].type == OSM_ENTITY_RELATION) {
            if(!isInTree16(relationsIndex, relation->relationMembers.values[m].ref)) {
                return 0;
            }            
        } 
    }
    return 1;
}

static void nprintf(const char* format, ...) {
    //do nothing
}

static void growBTags(osm2obm* self) {
    self->tags.capacity += 10;
    self->tags.values = realloc(self->tags.values, sizeof(BTag) * self->tags.capacity);
}

static int btagSlotsCount(UTF8* value) {
    int slots = ceil(((double)utf8size(value)) / ATTRIBUTE_VALUE_LENGTH);
    return slots;
}

static void addBTag(osm2obm* self, int key, UTF8* value) {
    int tagSlots = btagSlotsCount(value);
    if(self->tags.capacity < self->tags.count + tagSlots) {
        growBTags(self);
    }
    int index = self->tags.count; 
    int size = utf8size(value);
    //    dprintf("Adding tag %i = %s (%i)\n", key, value, size);
    int c = 0;
    for(int i = 0; i < size; i++, c++) {
        if(c == ATTRIBUTE_VALUE_LENGTH - 2) {
            self->tags.values[index].value[c] = (unsigned char)'\0';
            index++;
            c = -1;
            i--;
        } else {
            if (c == 0) {
                self->tags.values[index].key = i == 0 ? key : ATTRIBUTE_CONTINUATION;
            }
            self->tags.values[index].value[c] = value[i];
        }
    }
    self->tags.values[index].value[c] = (unsigned char)'\0';
    self->tags.count = index + 1;
}

static void growBWayNodes(osm2obm* self) {
    self->wayNodes.capacity += 10;
    self->wayNodes.values = realloc(self->wayNodes.values, sizeof(BWayNode) * self->wayNodes.capacity);
}

static void addBWayNode(osm2obm* self, BId ref) {
    if(self->wayNodes.capacity < self->wayNodes.count + 1) {
        growBWayNodes(self);
    }
    self->wayNodes.values[self->wayNodes.count].ref = ref;
    self->wayNodes.count++;
}


static void growBRelations(osm2obm* self) {
    self->relations.capacity = self->relations.capacity == 0 ? 10 : self->relations.capacity * 2;
    self->relations.values = realloc(self->relations.values, sizeof(BRelationInMemory) * self->relations.capacity);
}

static void addBRelation(osm2obm* self) {
    if(self->relations.capacity < self->relations.count + 1) {
        growBRelations(self);
    }
    self->relations.values[self->relations.count].info.id = self->relation.id;
    
    self->relations.values[self->relations.count].relationMembers.values = malloc(sizeof(BRelationMember) * self->relationMembers.count);
    memcpy(self->relations.values[self->relations.count].relationMembers.values, 
           self->relationMembers.values, 
           sizeof(BRelationMember) * self->relationMembers.count);
    self->relations.values[self->relations.count].relationMembers.count = self->relationMembers.count;
    
    self->relations.values[self->relations.count].tags.values = malloc(sizeof(BTag) * self->tags.count);
    memcpy(self->relations.values[self->relations.count].tags.values, 
           self->tags.values, 
           sizeof(BTag) * self->tags.count);
    self->relations.values[self->relations.count].tags.count = self->tags.count;
    
    self->relations.count++;
}

static void growBRelationMembers(osm2obm* self) {
    self->relationMembers.capacity += 10;
    self->relationMembers.values = realloc(self->relationMembers.values, sizeof(BRelationMember) * self->relationMembers.capacity);
}

static void addBRelationMember(osm2obm* self, BId ref, OsmEntityType type, int role) {
    if(self->relationMembers.capacity < self->relationMembers.count + 1) {
        growBRelationMembers(self);
    }
    self->relationMembers.values[self->relationMembers.count].type = type;
    self->relationMembers.values[self->relationMembers.count].role = role;
    self->relationMembers.values[self->relationMembers.count].ref = ref;
    self->relationMembers.count++;
}

static void newTag(void* self, OsmEntityType type, UTF8* key, UTF8* value) {
     int keyIndex = simpleStringIndexOf(&(((osm2obm*)self)->keysIndex), key);
     addBTag((osm2obm*)self, keyIndex, value);
}

static void newNode(void* self, OsmId id, Coordinate lat, Coordinate lon, OsmTimestamp timestamp) {
    ((osm2obm*)self)->node.id = id;
    ((osm2obm*)self)->node.lat = lat;
    ((osm2obm*)self)->node.lon = lon;
    ((osm2obm*)self)->node.timestamp = timestamp;
}

static BTag emptyTag = {0, "\0EMPTY\0EMPTY\0EMPTY\0EMPTY\0EMPTY!!"};

static char writeSpecifiedBTags(BTag* tags, int count, int* current, int maxCount, FILE* file) {
    int tagsLeft = count - *current;
    if( tagsLeft > maxCount) {
        fwrite(tags + *current, sizeof(BTag), maxCount, file);
        *current= *current + maxCount;
    } else {
        if(tagsLeft) {
            fwrite(tags + *current, sizeof(BTag), tagsLeft, file);   
        }
        for(int i=0; i < maxCount - tagsLeft; i++) {
            fwrite(&emptyTag, sizeof(BTag), 1, file);
        }
        *current= count;
    }
    return count > *current;
}

static char writeBTags(osm2obm* self, int* current, int maxCount, FILE* file) {
    return writeSpecifiedBTags(self->tags.values, self->tags.count, current, maxCount, file);
}

static void writeNode(void* abstractSelf) {
    osm2obm* self = (osm2obm*)abstractSelf;
    for(int c=0; c< self->countriesCount; c++) {
        BCountry* country = self->countries + c;
        if(nodeBelongsCountry(self, country)) {
            int currentTag = 0;
            char tagsLeft = 1;
            addTree16Node(&(country->nodesIndex), self->node.id, country->nodesOffset);
            while(tagsLeft) {
                fwrite(&(self->node), sizeof(NodeInfo), 1, country->nodesFile);
                tagsLeft = writeBTags(self, &currentTag, NODE_ATTRIBUTES_COUNT, country->nodesFile);
                country->nodesOffset += sizeof(BNode);
            }  
        } 
    }
    self->tags.count = 0;
    //dprintf("END NODE\n");
}

static void newWayNode(void* self, OsmId ref) {
    addBWayNode((osm2obm*)self, ref);
}

static void newWay(void* self, OsmId id, OsmTimestamp timestamp) {
    ((osm2obm*)self)->way.id = id;
    ((osm2obm*)self)->way.timestamp = timestamp;
}

static BWayNode emptyWayNode = {0};

static int writeBWayNodesn(osm2obm* self, BCountry* country, int* current, int count) {
    int c = 0;
    int n=*current;
    for(; n < self-> wayNodes.count && c < count ; n++,(*current)++) {
        if(isInTree16(&(country->nodesIndex), self->wayNodes.values[n].ref)) {
            fwrite(self->wayNodes.values + n, sizeof(BWayNode), 1, country->waysFile);
            c++;
        }
    }
    
    return c;
}

static char writeBWayNodes(osm2obm* self, BCountry* country, int* current) {
    int nodesLeft = min(self->wayNodes.count - *current, WAY_NODES_COUNT);
    int nodesWritten = writeBWayNodesn(self, country, current, nodesLeft);
    for(int w=0; w< WAY_NODES_COUNT - nodesWritten; w++) {
       fwrite(&emptyWayNode, sizeof(BWayNode), 1, country->waysFile);
    }
    return self->wayNodes.count > *current;
}


static void writeWay(void* abstractSelf) {
    osm2obm* self = (osm2obm*)abstractSelf;
    for(int c=0; c< self->countriesCount; c++) {
        BCountry* country = self->countries + c;
        if(wayBelongsCountry(self, country)) {
            int currentTag = 0;
            int currentNode = 0;
            char tagsLeft = 1;
            char nodesLeft = 1;
            addTree16Node(&(country->waysIndex), self->way.id, country->waysOffset);
            while(tagsLeft || nodesLeft) {
                fwrite(&(self->way), sizeof(WayInfo), 1, country->waysFile);
                tagsLeft = writeBTags(self, &currentTag, WAY_ATTRIBUTES_COUNT, country->waysFile);
                nodesLeft = writeBWayNodes(self, country, &currentNode);
                country->waysOffset += sizeof(BWay);
            }
        } 
    }
    self->tags.count = 0;
    self->wayNodes.count = 0;
}

static void newRelation(void* self, OsmId id, OsmTimestamp timestamp) {
    ((osm2obm*)self)->relation.id = id;
    ((osm2obm*)self)->relation.timestamp = timestamp;
}

static BRelationMember emptyRelationMember = {0, OSM_ENTITY_NONE, 0};

static int writeBRelationMembersn(BRelationInMemory* relation, BCountry* country, int* current, int count) {
    int c = 0;
    int n=*current;
    for(; n < relation->relationMembers.count && c < count ; n++,(*current)++) {
        BRelationMember* member = relation->relationMembers.values + n;
        int inMap = 0;
        if(member->type == OSM_ENTITY_NODE) {
            if(isInTree16(&(country->nodesIndex), member->ref)) {
                inMap = 1;
            }            
        } else if(member->type == OSM_ENTITY_WAY) {
            if(isInTree16(&(country->waysIndex), member->ref)) {
                inMap = 1;
            }            
        } else if(member->type == OSM_ENTITY_RELATION) {
            if(isInTree16(&(country->relationsIndex), member->ref)) {
                inMap = 1;
            }            
        }
        if(inMap) {
            fwrite(relation->relationMembers.values + n, sizeof(BRelationMember), 1, country->relationsFile);
            c++;
        }
    }
    return c;    
}

static void writeRelation(void* self){
    addBRelation((osm2obm*)self);
    ((osm2obm*)self)->tags.count = 0;
    ((osm2obm*)self)->relationMembers.count = 0;
}

static char writeBRelationMember(BRelationInMemory* relation, BCountry* country, int* current) {
    int membersLeft = min(relation->relationMembers.count - *current, RELATION_MEMBERS_COUNT);
    int membersWritten = writeBRelationMembersn(relation, country, current, membersLeft);
    for(int m=0; m< RELATION_MEMBERS_COUNT - membersWritten; m++) {
        fwrite(&emptyRelationMember, sizeof(BRelationMember), 1, country->relationsFile);
    }
    return relation->relationMembers.count > *current;
}

static void writeRelations(osm2obm* self, BCountry* country) {
    Tree16 pseudoIndex;
    initTree16(&pseudoIndex);
    int found;
    printf("Writing relations for %s. Total relations: %i\n", country->polygon->name, self->relations.count);
    int iterations = 0;
    do {
        found = 0;
        for(int r = 0; r < self->relations.count; r++) {
            //dprintf("Relation %i\n", self->relations.values[r].id);
            if(!isInTree16(&pseudoIndex, self->relations.values[r].info.id) && relationBelongsCountry(self->relations.values + r, country, &pseudoIndex)) {
                //dprintf("Added relation %i\n", self->relations.values[r].id);    
                addTree16Node(&pseudoIndex, self->relations.values[r].info.id, 0);
                found++;
            }
        }
        iterations++;
    } while(found);
    printf("Iterations found in %i iterations\n", iterations);
    int written = 0;
    for(int r=0;r < self->relations.count; r++) {
        if(isInTree16(&pseudoIndex, self->relations.values[r].info.id)){
            //printf("Writing relation %i\n", self->relations.values[r].info.id);
            // && relationIsFullyBelongsCountry(self->relations.values + r, country, &pseudoIndex)) {
            int currentTag = 0;
            int currentMember = 0;
            char tagsLeft = 1;
            char membersLeft = 1;
            addTree16Node(&(country->relationsIndex), self->relations.values[r].info.id, country->relationsOffset);
            while(tagsLeft || membersLeft) {
                fwrite(&(self->relations.values[r].info), sizeof(RelationInfo), 1, country->relationsFile);
                tagsLeft = writeSpecifiedBTags(self->relations.values[r].tags.values, self->relations.values[r].tags.count, &currentTag, RELATION_ATTRIBUTES_COUNT, country->relationsFile);
                membersLeft = writeBRelationMember(self->relations.values + r, country, &currentMember);
                country->relationsOffset += sizeof(BRelation);
            }
            written++;
        } else {
            //printf("Skip relation %i. %s\n", self->relations.values[r].id, isInTree16(&pseudoIndex, self->relations.values[r].id) ? (relationIsFullyBelongsCountry(self->relations.values + r, country, &pseudoIndex)? "HM.." : "It does not have some members.") : "it is not in list");
        }
    }
    freeTree16(&pseudoIndex);
    printf("Relations written: %i\n", written);
}

static void newRelationMember(void* self, OsmId ref, OsmEntityType type, UTF8* role) {
    int roleIndex = simpleStringIndexOf(&(((osm2obm*)self)->rolesIndex), role);
    addBRelationMember((osm2obm*)self, ref, type, roleIndex);
}

void initOsm2obmWithOutputDirectory(osm2obm* self, const char* outputDirectory, CountryPolygon* polygons, int polygonsCount) {
    
    initOsmStreamReader(&(self->reader), self);
    
    self->reader.newTag = newTag;
    self->reader.newNode = newNode;
    self->reader.newWayNode = newWayNode;
    self->reader.newWay = newWay;
    self->reader.newRelationMember = newRelationMember;
    self->reader.newRelation = newRelation;
    
    self->reader.finishNode[OSM_CHANGE_CREATE] = writeNode;
    self->reader.finishWay[OSM_CHANGE_CREATE] = writeWay;
    self->reader.finishRelation[OSM_CHANGE_CREATE] = writeRelation;
    
    self->tags.values = NULL;
    self->tags.count = 0;
    self->tags.capacity = 0;
    
    self->wayNodes.values = NULL;
    self->wayNodes.count = 0;
    self->wayNodes.capacity = 0;
    
    self->relationMembers.values = NULL;
    self->relationMembers.count = 0;
    self->relationMembers.capacity = 0;
    
    self->relations.values = NULL;
    self->relations.count = 0;
    self->relations.capacity = 0;
    
    self->currentEntityType = OSM_ENTITY_NONE;
	//printf("Initialize keys index...\n");
    initSimpleStringIndex(&(self->keysIndex));
	//printf("Adding default keys...\n");
    simpleStringIndexOf(&(self->keysIndex), UTF8_CAST "UNUSED");
    simpleStringIndexOf(&(self->keysIndex), UTF8_CAST "CONTINUATION");
    simpleStringIndexOf(&(self->keysIndex), UTF8_CAST "EMPTY_STRING");
//    printf("CONTINUATION key: %i",);
	//dprintf("Done.\n");
	
	//printf("Initialize roles index...\n");	
    initSimpleStringIndex(&(self->rolesIndex));
    simpleStringIndexOf(&(self->rolesIndex), UTF8_CAST "UNUSED");
    simpleStringIndexOf(&(self->rolesIndex), UTF8_CAST "CONTINUATION");
    simpleStringIndexOf(&(self->rolesIndex), UTF8_CAST "EMPTY_STRING");
//    printf("CONTINUATION role: %i", );
   	//printf("Done.\n");

    self->countries = malloc(sizeof(BCountry) * polygonsCount);
    self->countriesCount = polygonsCount;
    if(-1 == mkdir(outputDirectory, S_IRWXU) && errno != EEXIST) {  
        fprintf(stderr, "Error creating directory %s: %i\n", outputDirectory, errno);        
    }
    
    for(int p=0;p <polygonsCount; p++) {
        self->countries[p].nodesOffset = 0;
        self->countries[p].waysOffset = 0;
        self->countries[p].relationsOffset = 0;        
        initTree16(&(self->countries[p].nodesIndex));
        initTree16(&(self->countries[p].waysIndex));
        initTree16(&(self->countries[p].relationsIndex));      
        self->countries[p].outputDirectory = fullFileName(polygons[p].name, outputDirectory);
        printf("Country %s\n", polygons[p].name);
        if(-1 == mkdir(self->countries[p].outputDirectory, S_IRWXU) && errno != EEXIST) {
            fprintf(stderr, "Error creating directory %s: %i\n", self->countries[p].outputDirectory, errno);        
        }
        self->countries[p].nodesFile = openFile("nodes.obm", self->countries[p].outputDirectory, "wb+");
        self->countries[p].waysFile = openFile("ways.obm", self->countries[p].outputDirectory, "wb+");
        self->countries[p].relationsFile = openFile("relations.obm", self->countries[p].outputDirectory, "wb+");
        self->countries[p].polygon = polygons+p;
    }
}

void closeOsm2obm(osm2obm* self) {
    for(int c=0;c < self->countriesCount; c++) {
        
        writeRelations(self, self->countries + c);
        
        fclose(self->countries[c].nodesFile);
        fclose(self->countries[c].waysFile);
        fclose(self->countries[c].relationsFile);
        
        FILE* keysFile = openFile("keys.l", self->countries[c].outputDirectory, "w+");
        FILE* rolesFile = openFile("roles.l", self->countries[c].outputDirectory, "w+");
        
        writeSimpleStringIndex(&(self->keysIndex), keysFile);    
        writeSimpleStringIndex(&(self->rolesIndex), rolesFile);
        fclose(keysFile);
        fclose(rolesFile);
        
        FILE* nodesIndexFile = openFile("nodes.idx", self->countries[c].outputDirectory, "wb+");
        FILE* waysIndexFile = openFile("ways.idx", self->countries[c].outputDirectory, "wb+");
        FILE* relationsIndexFile = openFile("relations.idx", self->countries[c].outputDirectory, "wb+");
        saveTree16ToFile(&(self->countries[c].nodesIndex), nodesIndexFile, 0, 0);
        saveTree16ToFile(&(self->countries[c].waysIndex), waysIndexFile, 0, 0);
        saveTree16ToFile(&(self->countries[c].relationsIndex), relationsIndexFile, 0, 0);
        free(self->countries[c].outputDirectory);
        fclose(nodesIndexFile);
        fclose(waysIndexFile);
        fclose(relationsIndexFile);
    }    
    
    closeOsmStreamReader(&(self->reader));
}

void convertOsm2ObmFromFile(osm2obm* self, const char *filename) {
    readOsmFromFile(&(self->reader), filename);
}
void convertOsm2ObmFromStdin(osm2obm* self) {
    readOsmFromStdin(&(self->reader));
}

#pragma mark Read obm

void closeObm(void* abstractSelf) {
    obm* self = (obm*) abstractSelf;
    if(self->cacheNodes) {
        clearNodes(&(self->nodes));
    } else {
        fclose(self->nodesFile);    
    }

    fclose(self->waysFile);
    fclose(self->relationsFile);
    
    freeTree16WithFile(&(self->nodesIndex));
    freeTree16WithFile(&(self->waysIndex));
    freeTree16WithFile(&(self->relationsIndex));
}

static int compareNodes(const void * a, const void * b) {
    return ((Node*)a)->info.id - ((Node*)b)->info.id;
}

static void appendBTags(PlainTags* tags, BTag* rawTags, int count, SimpleStringIndex* keysIndex) {
    for(int a = 0; a < count; a++) {
        BTag tag = rawTags[a];
        //printf("Raw tag %i.\n", a);
        //printf("%i = %s\n", rawTags[a].key, rawTags[a].value);
        if(tag.key == ATTRIBUTE_CONTINUATION) {
            int i = tags->count - 1;
            tags->values[i].value = utf8cat(tags->values[i].value, tag.value);
        } else {
            if(tag.key != UNUSED_ATTRIBUTE) {
                ensurePlainTagsCapacityForNNewElements(tags, 1);
                tags->values[tags->count].key = utf8dup(simpleStringValuesAtIndex(keysIndex,tag.key));
                tags->values[tags->count].value = utf8dup(tag.value);
                
                tags->count++;
            }
        }
    }
} 

static void readBTags(obm* self, FILE* file, int count, PlainTags* tags) {
    //printf("Reading tags...\n");
    fread(self->tags, sizeof(BTag), count, file);
    //printf("Done.\n");
    //printf("Appending tags...\n");
    appendBTags(tags, self->tags, count, &(self->keysIndex));
}

static Node* readNodeFromFile(obm* self, Node* node, FILE* nodesFile) {
    NodeInfo nodeInfo;
    //printf("Reading node info...\n");
    if(!fread(&(node->info), sizeof(NodeInfo), 1, nodesFile)) {
        return NULL;
    }
    //printf("Done\n");
    int readed;
    removeAllPlainTags(&(node->tags));
    do {
        readBTags(self, nodesFile, NODE_ATTRIBUTES_COUNT, &(node->tags));
        //printf("Reading next part...\n");
        readed = fread(&nodeInfo, sizeof(NodeInfo), 1, nodesFile);
    } while (readed && nodeInfo.id == node->info.id);
    
    if(readed) {
        fseek(nodesFile, -sizeof(NodeInfo), SEEK_CUR);
    }
    //printf("Node Finished.\n");
    return node;
}

void initObm(obm* self, const char* directory, int cacheNodes) {
    FILE* nodesFile = openFile("nodes.obm", directory, "rb+");
    
    self->cacheNodes = cacheNodes;
    if(!self->cacheNodes) {
        self->nodesFile = nodesFile;    
    }

    self->waysFile = openFile("ways.obm", directory, "rb+");
    self->relationsFile = openFile("relations.obm", directory, "rb+");
    
    initTree16WithFile(&(self->nodesIndex), openFile("nodes.idx", directory, "rb+"));
    initTree16WithFile(&(self->waysIndex), openFile("ways.idx", directory, "rb+"));
    initTree16WithFile(&(self->relationsIndex), openFile("relations.idx", directory, "rb+"));
    if(!self->cacheNodes) {
        self->currentNode = calloc(sizeof(Node), 1);
        initPlainTags(&(self->currentNode->tags));
    }
    self->currentWay.tags.values = NULL;
    self->currentWay.tags.count = 0;
    self->currentRelation.tags.values = NULL;
    self->currentRelation.tags.count = 0;
    
    self->currentWay.wayNodes.values = NULL;
    self->currentWay.wayNodes.count = 0;
    self->currentRelation.relationMembers.values = NULL;
    self->currentRelation.relationMembers.count = 0;
    
    FILE* keysFile = openFile("keys.l", directory, "r+");
    initSimpleStringIndexFromFile(&(self->keysIndex), keysFile);
    fclose(keysFile);
    
    FILE* rolesFile = openFile("roles.l", directory, "r+");
    initSimpleStringIndexFromFile(&(self->rolesIndex), rolesFile);
    fclose(rolesFile);

    
    self->tags = malloc(sizeof(BTag)* max(NODE_ATTRIBUTES_COUNT, max(WAY_ATTRIBUTES_COUNT, RELATION_ATTRIBUTES_COUNT)));

    if(!nodesFile) {
        fprintf(stderr, "Error opening nodes file\n");
    }
        
    if(!self->waysFile) {
        fprintf(stderr, "Error opening ways file\n");
    }
    if(!self->relationsFile) {
        fprintf(stderr, "Error opening relations file\n");
    }
    
    if(!self->nodesIndex.file) {
        fprintf(stderr, "Error opening nodes index file\n");
    }
    if(!self->waysIndex.file) {
        fprintf(stderr, "Error opening ways index file\n");
    }
    if(!self->relationsIndex.file) {
        fprintf(stderr, "Error opening relations index file\n");
    }
    
    if(self->cacheNodes) {
    printf("Caching nodes...\n");
    initNodes(&(self->nodes));
    ensureNodesCapacityForNNewElements(&(self->nodes), 1);
    initPlainTags(&(self->nodes.values->tags));
    while(readNodeFromFile(self, self->nodes.values + self->nodes.count, nodesFile)) {
        self->nodes.count++;
        ensureNodesCapacityForNNewElements(&(self->nodes), 1);
        initPlainTags(&((self->nodes.values + self->nodes.count)->tags));
    }
    printf("%i nodes in memory.\n", self->nodes.count);
    qsort(self->nodes.values, self->nodes.count, sizeof(Node), compareNodes);
    self->currentNode = self->nodes.values;
        
    }
    
}


static Node* readCachedNode(obm* self, Node* node) {
    if(self->currentNode) {
        if(node != self->currentNode) {
            node->info.id = self->currentNode->info.id;
            node->info.lat = self->currentNode->info.lat;
            node->info.lon = self->currentNode->info.lon;
            node->tags.values = self->currentNode->tags.values;
            node->tags.count = self->currentNode->tags.count;
            node->tags.capacity = self->currentNode->tags.capacity;
        }
        if(self->currentNode - self->nodes.values >= self->nodes.count) {
            self->currentNode = NULL;
        } else {
            self->currentNode++;    
        }
        return node;
    } else {
        return NULL;
    }
}

static Node* readNode(obm* self, Node* node) {
    if(self->cacheNodes) {
        return readCachedNode(self, node);
    }
    return readNodeFromFile(self, node, self->nodesFile);
}

static Node* bNodeWithIdCached(void* self, OsmId id) {
    Node* originalNode = ((obm*)self)->currentNode;
    ((obm*)self)->currentNode = (Node*)bsearch(&id, ((obm*)self)->nodes.values, ((obm*)self)->nodes.count, sizeof(Node), compareNodes);
    Node* node = NULL;
    if(((obm*)self)->currentNode) {
        node = calloc(sizeof(Node), 1);
        readNode(((obm*)self), node);
    } else {
        fprintf(stderr, "Node %i was not found.\n", id);
    }
    ((obm*)self)->currentNode = originalNode;
    return node;
}


static Node* bNodeWithIdNotCached(void* self, OsmId id) {
    long offset = findObjectOffset(&(((obm*)self)->nodesIndex), id);
    long oldOffset = ftell(((obm*)self)->nodesFile);
    fseek(((obm*)self)->nodesFile, offset, SEEK_SET);
    Node* node = calloc(sizeof(Node), 1);
    if(!readNode(((obm*)self), node)) {
        free(node);
        node = NULL;
    }
    fseek(((obm*)self)->nodesFile, oldOffset, SEEK_SET);
    return node;
}


static Node* bNodeWithId(void* self, OsmId id) {
    if(((obm*)self)->cacheNodes) {
        return bNodeWithIdCached(self, id);
    }
    return bNodeWithIdNotCached(self, id);
}


static void readBWayNodes(obm* self, Way* way) {
    BWayNode nodes[WAY_NODES_COUNT];
    fread(nodes, sizeof(BWayNode), WAY_NODES_COUNT, self->waysFile);
    for(int n=0;n<WAY_NODES_COUNT;n++) {
        if(nodes[n].ref) {
            if(way->wayNodes.capacity < way->wayNodes.count + 1) {
                way->wayNodes.capacity += WAY_NODES_COUNT;
                way->wayNodes.values = realloc(way->wayNodes.values, sizeof(NodeInfo)* way->wayNodes.capacity);
            }
            way->wayNodes.values[way->wayNodes.count].id = nodes[n].ref;
            Node* node = bNodeWithId(self, nodes[n].ref);
            if(node) {
                way->wayNodes.values[way->wayNodes.count].lat = node->info.lat;
                way->wayNodes.values[way->wayNodes.count].lon = node->info.lon;
                free(node);                
            } else {
                fprintf(stderr, "Node %li was not found.\n", nodes[n].ref);
            }
            way->wayNodes.count++;
        }
    }
}

static void readBRelationMembers(obm* self, Relation* relation) {
    BRelationMember members[RELATION_MEMBERS_COUNT];
    fread(members, sizeof(BRelationMember), RELATION_MEMBERS_COUNT, self->relationsFile);
    for(int m=0;m<RELATION_MEMBERS_COUNT;m++) {
        if(members[m].role != UNUSED_ATTRIBUTE) {
            if(relation->relationMembers.capacity < relation->relationMembers.count + 1) {
                relation->relationMembers.capacity += RELATION_MEMBERS_COUNT;
                relation->relationMembers.values = realloc(relation->relationMembers.values, sizeof(RelationMemberInfo)* relation->relationMembers.capacity);
            }
            relation->relationMembers.values[relation->relationMembers.count].ref = members[m].ref;
            relation->relationMembers.values[relation->relationMembers.count].type = members[m].type;
            //printf("Relation %i. Member ref %i. Role %i.\n", relation->info.id, members[m].ref, members[m].role);
            relation->relationMembers.values[relation->relationMembers.count].role = utf8dup(simpleStringValuesAtIndex(&(self->rolesIndex), members[m].role));
            relation->relationMembers.count++;
        }
    }
}


static Way* readWay(obm* self, Way* way) {
    //printf("Reading way...\n");
    WayInfo wayInfo;
    if(!fread(&(way->info), sizeof(WayInfo), 1, self->waysFile)) {
        return NULL;
    }
    
    int readed;
    //printf("  Clear tags..\n");
    removeAllPlainTags(&(way->tags));
    //printf("  Clear nodes..\n");
    removeAllNodesInfo(&(way->wayNodes));
    //printf("  Read..\n");
    do {
        readBTags(self, self->waysFile, WAY_ATTRIBUTES_COUNT, &(way->tags));
        readBWayNodes(self, way);
        readed = fread(&wayInfo, sizeof(WayInfo), 1, self->waysFile);
    } while (readed && wayInfo.id == way->info.id);

    if(readed) {
        //printf("  Back..\n");
        fseek(self->waysFile, -sizeof(WayInfo), SEEK_CUR);
    }  
    //printf("Done.\n");
    return way;
}

Relation* readRelation(obm* self, Relation* relation) {
    RelationInfo relationInfo;
    if(!fread(&(relation->info), sizeof(RelationInfo), 1, self->relationsFile)) {
        return NULL;
    }
    int readed;
    removeAllPlainTags(&(relation->tags));
    removeAllRelationMembers(&(relation->relationMembers));
    do {
        readBTags(self, self->relationsFile, RELATION_ATTRIBUTES_COUNT, &(relation->tags));
        readBRelationMembers(self, relation);
        readed = fread(&relationInfo, sizeof(RelationInfo), 1, self->relationsFile);
    } while (readed && relationInfo.id == relation->info.id);
    if(readed) {
        fseek(self->relationsFile, -sizeof(RelationInfo), SEEK_CUR);   
    }
    
    return relation;
}

static Node* nextBNode(void* self) {
    return readNode((obm*)self, (((obm*)self)->currentNode));
}

static Way* nextBWay(void* self) {
    return readWay((obm*)self, &(((obm*)self)->currentWay));
}

static Relation* nextBRelation(void* self) {
    return readRelation((obm*)self, &(((obm*)self)->currentRelation));
}

Nodes* nodesForWayCached(obm* self, Way* way) {
    int count = way->wayNodes.count;
    Node* nodes = malloc(sizeof(Node) * way->wayNodes.count);
    Node* originalNode = self->currentNode;
    for(int i = 0; i< count; i++) {
        self->currentNode = bsearch(&(way->wayNodes.values[i].id), self->nodes.values, self->nodes.count, sizeof(Node), compareNodes);
        if(self->currentNode) {
            readNode(self, nodes + i);
        } else {
            fprintf(stderr, "Node %i was not found.\n", way->wayNodes.values[i].id);
        }
        
    }
    self->currentNode = originalNode;
    
    Nodes* result = malloc(sizeof(Nodes));
    result->count = count;
    result->values = nodes;
    
    return result;
}

Nodes* nodesForWayNotCached(obm* self, Way* way) {
    int count = way->wayNodes.count;
    Node* nodes = malloc(sizeof(Node) * way->wayNodes.count);
    long originalOffset = ftell(self->nodesFile);
    for(int i = 0; i< count; i++) {
        long offset = findObjectOffset(&(self->nodesIndex), way->wayNodes.values[i].id);
        fseek(self->nodesFile, offset, SEEK_SET);
        readNode(self, nodes + i);
    }
    fseek(self->nodesFile, originalOffset, SEEK_SET);
    
    Nodes* result = malloc(sizeof(Nodes));
    result->count = count;
    result->values = nodes;
    
    return result;
}

Nodes* nodesForWay(obm* self, Way* way) {
    if(self->cacheNodes) {
        return nodesForWayCached(self, way);
    }
    return nodesForWayNotCached(self, way);
}

static Way* bWayWithId(void* self, OsmId id) {
    long offset = findObjectOffset(&(((obm*)self)->waysIndex), id);
    long oldOffset = ftell(((obm*)self)->waysFile);
    fseek(((obm*)self)->waysFile, offset, SEEK_SET);
    Way* way = calloc(sizeof(Way), 1);
    if(!readWay(((obm*)self), way)){
        free(way);
        way = NULL;
    }
    fseek(((obm*)self)->waysFile, oldOffset, SEEK_SET);
    return way;
}


static Relation* bRelationWithId(void* self, OsmId id) {
    long offset = findObjectOffset(&(((obm*)self)->relationsIndex), id);
    long oldOffset = ftell(((obm*)self)->relationsFile);
    fseek(((obm*)self)->relationsFile, offset, SEEK_SET);
    Relation* relation = calloc(sizeof(Relation), 1);
    if(!readRelation(((obm*)self), relation)) {
        free(relation);
        relation = NULL;
    }
    fseek(((obm*)self)->relationsFile, oldOffset, SEEK_SET);
    return relation;
}

static void restartBNodes(void* self) {
    if(((obm*) self)->cacheNodes) {
        ((obm*) self)->currentNode = ((obm*) self)->nodes.values;    
    } else {
        fseek(((obm*) self)->nodesFile, 0, SEEK_SET);    
    }
}

static void restartBWays(void* self) {
    fseek(((obm*) self)->waysFile, 0, SEEK_SET);
}

static void restartBRelations(void* self) {
    fseek(((obm*) self)->relationsFile, 0, SEEK_SET);
}

OsmDbReader* newObmReader(const char* directory, int cacheNodes) {
    OsmDbReader* reader = calloc(sizeof(OsmDbReader), 1);
     
    obm* self = calloc(sizeof(obm), 1);
    initObm(self, directory, cacheNodes);
    initOsmDbReader(reader, self);
    reader->nextNode = nextBNode;
    reader->nextWay = nextBWay;
    reader->nextRelation = nextBRelation;
    
    reader->restartNodes = restartBNodes;
    reader->restartWays = restartBWays;
    reader->restartRelations = restartBRelations;
    
    reader->nodeWithId = bNodeWithId;
    reader->wayWithId = bWayWithId;
    reader->relationWithId = bRelationWithId;
    
    reader->close = closeObm;
    return reader;
}
