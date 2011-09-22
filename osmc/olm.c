/*
 *  olm.c
 *  OSMapper
 *
 *  Created by Egor Leonenko on 9.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#include "olm.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <libxml/xmlstring.h>
#include "utils.h"
#include <time.h>

static int nodeBelongsCountry(osm2olm* self, LCountry* country) {
    //printf("Check in Country with polygon: %x\n", country->polygon);
    return isPointInPolygon(self->node.lat, self->node.lon, country->polygon);
}

static char checkNodeInTree16(void* country, OsmId id) {
    return isInTree16(&(((LCountry*)country)->nodesIndex), id);
}

static char checkWayInTree16(void* country, OsmId id) {
    return isInTree16(&(((LCountry*)country)->waysIndex), id);
}

static char checkRelationInTree16(void* country, OsmId id) {
    return isInTree16(&(((LCountry*)country)->relationsIndex), id);
}

static int wayBelongsCountry(osm2olm* self, LCountry* country) {
    for(int n = 0; n < self->wayNodes.count; n++) {
        if(country->ifNodeExists(country, self->wayNodes.values[n].ref)) {
            return 1;
        }
    }
    return 0;
}

static int relationBelongsCountry(RelationChange* relation, LCountry* country, Tree16* relationsIndex) {
    for(int m = 0; m < relation->base.relationMembers.count; m++) {
        if(relation->base.relationMembers.values[m].type == OSM_ENTITY_NODE) {
            if(country->ifNodeExists(country, relation->base.relationMembers.values[m].ref)) {
                return 1;
            }            
        } else if(relation->base.relationMembers.values[m].type == OSM_ENTITY_WAY) {
            if(country->ifWayExists(country, relation->base.relationMembers.values[m].ref)) {
                return 1;
            }            
        } else if(relation->base.relationMembers.values[m].type == OSM_ENTITY_RELATION) {
            if(country->ifRelationExists(country, relation->base.relationMembers.values[m].ref) 
               || isInTree16(relationsIndex, relation->base.relationMembers.values[m].ref)) {
                return 1;
            }            
        } 
    }
    printf(">>>Relation %i does not belongs country\n", relation->base.info.id);
    return 0;
}

static int relationIsFullyBelongsCountry(Relation* relation, LCountry* country, Tree16* relationsIndex) {
    for(int m = 0; m < relation->relationMembers.count; m++) {
        if(relation->relationMembers.values[m].type == OSM_ENTITY_NODE) {
            if(!country->ifNodeExists(country, relation->relationMembers.values[m].ref)) {
                return 0;
            }            
        } else if(relation->relationMembers.values[m].type == OSM_ENTITY_WAY) {
            if(!country->ifWayExists(country, relation->relationMembers.values[m].ref)) {
                return 0;
            }            
        } else if(relation->relationMembers.values[m].type == OSM_ENTITY_RELATION) {
            if(!country->ifRelationExists(country, relation->relationMembers.values[m].ref)) {
                return 0;
            }            
        } 
    }
    return 1;
}

static void beginTransaction(sqlite3* db) {
    sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
}

static void commitTransaction(sqlite3* db) {
    sqlite3_exec(db, "END TRANSACTION", NULL, NULL, NULL);
}

static void rollbackTransaction(sqlite3* db) {
    sqlite3_exec(db, "ROLLBACK TRANSACTION", NULL, NULL, NULL);
}

static char deleteFromDbById(sqlite3* db, sqlite3_stmt* deleteStatement, OsmId id) {
    sqlite3_bind_int(deleteStatement, 1, id);
    if(sqlite3_step(deleteStatement) == SQLITE_ERROR) {
        sqlite3_reset(deleteStatement);
        fprintf(stderr, "Could not delete object id %u: %s", id, sqlite3_errmsg(db));
        return 0;
    }
    sqlite3_reset(deleteStatement);
    return 1;
}

static void deleteNodeFromCountry(LCountry* country, OsmId id) {
    beginTransaction(country->db);
    if(deleteFromDbById(country->db, country->deleteNodeTagsStatement, id) &&
       deleteFromDbById(country->db, country->deleteNodeStatement, id)) {
        commitTransaction(country->db);
    } else {
        rollbackTransaction(country->db);
    }
}

static void deleteWayFromCountry(LCountry* country, OsmId id) {
    beginTransaction(country->db);
    if(deleteFromDbById(country->db, country->deleteWayTagsStatement, id) &&
       deleteFromDbById(country->db, country->deleteWayNodesStatement, id) &&
       deleteFromDbById(country->db, country->deleteWayStatement, id)) {
        commitTransaction(country->db);
    } else {
        rollbackTransaction(country->db);
    }
}

static void deleteRelationFromCountry(LCountry* country, OsmId id) {
    beginTransaction(country->db);
    if(deleteFromDbById(country->db, country->deleteRelationTagsStatement, id) &&
       deleteFromDbById(country->db, country->deleteRelationMembersStatement, id) &&
       deleteFromDbById(country->db, country->deleteRelationStatement, id)) {
        commitTransaction(country->db);
    } else {
        rollbackTransaction(country->db);
    }
}

static void newNode(void* abstractSelf, OsmId id, Coordinate lat, Coordinate lon, OsmTimestamp timestamp) {
    //printf("New node, %i\n", id);
    osm2olm* self = (osm2olm*) abstractSelf;
    self->node.id = id;
    self->node.lat = lat;
    self->node.lon = lon;
    self->node.timestamp = timestamp;
    //printf("New node, %i - End\n", id);
}

static void writeTags(osm2olm* self, PlainTags* tags, sqlite3_stmt* statement, OsmId ownerId) {
    for(int t = 0; t < tags->count; t++) {
        sqlite3_bind_int64(statement, 1, ownerId);
        sqlite3_bind_text(statement, 2, (char*) tags->values[t].key, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 3, (char*) tags->values[t].value, -1, SQLITE_TRANSIENT);
        sqlite3_step(statement);
        sqlite3_reset(statement);
    }
}

static void writeNode(void* abstractSelf) {
    osm2olm* self = (osm2olm*) abstractSelf;
    
    //printf("Write node\n");
    for(int c = 0; c < self->countriesCount; c++) {
        //printf("Check if belongs Country %i\n", c);
        if(nodeBelongsCountry(self, self->countries + c)) {
            //printf("Write node in country\n");
            
            addTree16Node(&(self->countries[c].nodesIndex), self->node.id, self->node.id);
            //printf("Added to index\n");
            //printf("Adding to db with statement: %x\n", self->countries[c].insertNodeStatement);
            beginTransaction(self->countries[c].db);       
            sqlite3_bind_int64(self->countries[c].insertNodeStatement, 1, self->node.id);
            sqlite3_bind_int(self->countries[c].insertNodeStatement, 2, self->node.lat);
            sqlite3_bind_int(self->countries[c].insertNodeStatement, 3, self->node.lon);
            sqlite3_bind_int64(self->countries[c].insertNodeStatement, 4, self->node.timestamp);
            //printf("Binded\n");
            sqlite3_step(self->countries[c].insertNodeStatement);
            //printf("Written\n");
            sqlite3_reset(self->countries[c].insertNodeStatement);
            //printf("Reset\n");
            writeTags(self, &(self->tags), self->countries[c].insertNodeTagStatement, self->node.id);
            //printf("Writing tags\n");
            commitTransaction(self->countries[c].db);
        }
    }
    //printf("Write node - end\n");
    self->tags.count = 0;
}

static void newWay(void* abstractSelf, OsmId id, OsmTimestamp timestamp) {
    osm2olm* self = (osm2olm*) abstractSelf;
    self->way.id = id;
    self->way.timestamp = timestamp;
}

static void writeWayNodes(osm2olm* self, LCountry* country) {
    sqlite3_stmt* statement = country->insertWayNodeStatement;
    int i = 0;
    for(int n = 0; n < self->wayNodes.count; n++) {
        if(country->ifNodeExists(country, self->wayNodes.values[n].ref)) {
            sqlite3_bind_int64(statement, 1, self->way.id);
            sqlite3_bind_int64(statement, 2, self->wayNodes.values[n].ref);
            sqlite3_bind_int64(statement, 3, i++);
            sqlite3_step(statement);
            sqlite3_reset(statement);
        }
    }
}

static void writeWay(void* abstractSelf) {
    osm2olm* self = (osm2olm*) abstractSelf;
    
    for(int c = 0; c < self->countriesCount; c++) {
        if(wayBelongsCountry(self, self->countries + c)) {
            addTree16Node(&(self->countries[c].waysIndex), self->way.id, self->way.id);
            beginTransaction(self->countries[c].db);
            sqlite3_bind_int64(self->countries[c].insertWayStatement, 1, self->way.id);
            sqlite3_bind_int64(self->countries[c].insertWayStatement, 2, self->way.timestamp);
            sqlite3_step(self->countries[c].insertWayStatement);
            sqlite3_reset(self->countries[c].insertWayStatement);
            
            writeTags(self, &(self->tags), self->countries[c].insertWayTagStatement, self->way.id);
            writeWayNodes(self, self->countries+c);
            commitTransaction(self->countries[c].db);
        }
    }    
    removeAllPlainTags(&(self->tags));
    removeAllWayNodes(&(self->wayNodes));
}

static void newWayNode(void* abstractSelf, OsmId ref) {
    osm2olm* self = (osm2olm*) abstractSelf;
    if(self->wayNodes.capacity < self->wayNodes.count + 1) {
        self->wayNodes.capacity += 10;
        self->wayNodes.values = realloc(self->wayNodes.values, self->wayNodes.capacity * sizeof(WayNodeInfo));
    }
    self->wayNodes.values[self->wayNodes.count].ref = ref;
    self->wayNodes.count++;
}

static void newRelation(void* abstractSelf, OsmId id, OsmTimestamp timestamp) {
    osm2olm* self = (osm2olm*) abstractSelf;
    self->relation.id = id;
    self->relation.timestamp = timestamp;
}

static void writeRelationInternal(void* abstractSelf, OsmChangeType change) {
    osm2olm* self = (osm2olm*) abstractSelf;
    
    if(self->relations.capacity < self->relations.count + 1) {
        self->relations.capacity += 10;
        self->relations.values = realloc(self->relations.values, self->relations.capacity * sizeof(RelationChange));
    }
    RelationChange* relation = self->relations.values + self->relations.count;

    relation->base.info.id = self->relation.id;
    relation->base.info.timestamp = self->relation.timestamp;  
    relation->base.relationMembers.values = malloc(sizeof(RelationMemberInfo) * self->relationMembers.count);
    relation->base.relationMembers.count = relation->base.relationMembers.capacity = self->relationMembers.count;
    for(int m=0;m<self->relationMembers.count;m++) {
        relation->base.relationMembers.values[m].ref = self->relationMembers.values[m].ref;
        relation->base.relationMembers.values[m].type = self->relationMembers.values[m].type;
        relation->base.relationMembers.values[m].role = utf8dup(self->relationMembers.values[m].role);
    }
    
    relation->base.tags.values = malloc(sizeof(PlainTag) * self->tags.count);
    relation->base.tags.count = relation->base.tags.capacity = self->tags.count;
    for(int t=0; t< self->tags.count;t++) {
        relation->base.tags.values[t].key = utf8dup(self->tags.values[t].key);
        relation->base.tags.values[t].value = utf8dup(self->tags.values[t].value);
    }
    relation->change = change;
    
    self->relations.count++;
    
    removeAllPlainTags(&(self->tags));
    removeAllRelationMembers(&(self->relationMembers));
}

static void writeRelation(void* abstractSelf) {
    writeRelationInternal(abstractSelf, OSM_CHANGE_CREATE);
}

static void newRelationMember(void* abstractSelf, OsmId ref, OsmEntityType type, UTF8* role) {
    osm2olm* self = (osm2olm*) abstractSelf;
    if(self->relationMembers.capacity < self->relationMembers.count + 1) {
        self->relationMembers.capacity += 10;
        self->relationMembers.values = realloc(self->relationMembers.values, self->relationMembers.capacity * sizeof(RelationMemberInfo));
    }
    self->relationMembers.values[self->relationMembers.count].ref = ref;
    self->relationMembers.values[self->relationMembers.count].type = type;
    self->relationMembers.values[self->relationMembers.count].role = utf8dup(role);
    self->relationMembers.count++;
}

static void newTag(void* abstractSelf, OsmEntityType type, UTF8* key, UTF8* value) {
    //printf("New tag\n");
    osm2olm* self = (osm2olm*) abstractSelf;
    if(self->tags.capacity < self->tags.count + 1) {
        self->tags.capacity += 10;
        self->tags.values = realloc(self->tags.values, self->tags.capacity * sizeof(PlainTag));
    }
    self->tags.values[self->tags.count].key = utf8dup(key);
    self->tags.values[self->tags.count].value = utf8dup(value);
    self->tags.count++;
    //printf("End New tag\n");
}


static void writeRelationMember(RelationChange* relation, LCountry* country) {
    int i=0;
    for(int m=0;m<relation->base.relationMembers.count;m++) {
        RelationMemberInfo* member = relation->base.relationMembers.values + m;
        int inMap = 0;
        if(member->type == OSM_ENTITY_NODE) {
            if(country->ifNodeExists(country, member->ref)) {
                inMap = 1;
            }            
        } else if(member->type == OSM_ENTITY_WAY) {
            if(country->ifWayExists(country, member->ref)) {
                inMap = 1;
            }            
        } else if(member->type == OSM_ENTITY_RELATION) {
            if(country->ifRelationExists(country, member->ref)) {
                inMap = 1;
            }            
        }
        if(inMap) {
            sqlite3_bind_int(country->insertRelationMemberStatement, 1, relation->base.info.id);
            sqlite3_bind_text(country->insertRelationMemberStatement, 2, relationMemberType2String(relation->base.relationMembers.values[m].type), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(country->insertRelationMemberStatement, 3, relation->base.relationMembers.values[m].ref);
            sqlite3_bind_text(country->insertRelationMemberStatement, 4, (char*)relation->base.relationMembers.values[m].role, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(country->insertRelationMemberStatement, 5, i);
            sqlite3_step(country->insertRelationMemberStatement);
            sqlite3_reset(country->insertRelationMemberStatement);
            i++;
        }
    }
}

static void writeRelations(osm2olm* self, LCountry* country) {
    Tree16 pseudoIndex;
    initTree16(&pseudoIndex);
    Tree16 whishesIndex;
    initTree16(&whishesIndex);
    int found;
    printf("Writing relations for %s. Total relations: %i\n", country->polygon->name, self->relations.count);
    int iterations = 0;
    do {
        found = 0;
        for(int r = 0; r < self->relations.count; r++) {
            if(!isInTree16(&pseudoIndex, self->relations.values[r].base.info.id)) {
                if(self->relations.values[r].change != OSM_CHANGE_DELETE) {
                    if((self->relations.values[r].change == OSM_CHANGE_NONE || relationBelongsCountry(self->relations.values + r, country, &pseudoIndex))) {
                        addTree16Node(&pseudoIndex, self->relations.values[r].base.info.id, 0);
                        
                        found++;
                    } else {
                        if(self->relations.values[r].change != OSM_CHANGE_NONE) {
                            printf("Relation %i does not belongs country\n", self->relations.values[r].base.info.id);
                        } else {
                            printf("Relation %i exists in DB and was'nt changed\n", self->relations.values[r].base.info.id);
                        }
                        
                    }
                } else {
                    printf("Relation %i was deleted\n", self->relations.values[r].base.info.id);
                }
            } else {
                //printf("Relation %i already encountered\n", self->relations.values[r].base.info.id);
            }
        }
        iterations++;
    } while(found);
    printf("Relations found in %i iterations\n", iterations);
    int written = 0;
    for(int r=0;r < self->relations.count; r++) {
        if(isInTree16(&pseudoIndex, self->relations.values[r].base.info.id)){
            beginTransaction(country->db);
            addTree16Node(&(country->relationsIndex), self->relations.values[r].base.info.id, self->relations.values[r].base.info.id);
            switch (self->relations.values[r].change) {
                case OSM_CHANGE_CREATE:
                    sqlite3_bind_int(country->insertRelationStatement, 1, self->relations.values[r].base.info.id);
                    sqlite3_bind_int64(country->insertRelationStatement, 2, self->relations.values[r].base.info.timestamp);
                    sqlite3_step(country->insertRelationStatement);
                    sqlite3_reset(country->insertRelationStatement);
                    writeTags(self, &(self->relations.values[r].base.tags), country->insertRelationTagStatement, self->relations.values[r].base.info.id);
                    writeRelationMember(self->relations.values + r, country);
                    break;
                case OSM_CHANGE_DELETE:
                    deleteRelationFromCountry(country, self->relations.values[r].base.info.id);
                case OSM_CHANGE_MODIFY:
                    sqlite3_bind_int(country->insertRelationStatement, 1, self->relations.values[r].base.info.id);
                    sqlite3_bind_int64(country->insertRelationStatement, 2, self->relations.values[r].base.info.timestamp);
                    sqlite3_step(country->insertRelationStatement);
                    sqlite3_reset(country->insertRelationStatement);
                    deleteFromDbById(country->db, country->deleteRelationTagsStatement, self->relations.values[r].base.info.id);
                    writeTags(self, &(self->relations.values[r].base.tags), country->insertRelationTagStatement, self->relations.values[r].base.info.id);
                    deleteFromDbById(country->db, country->deleteRelationMembersStatement, self->relations.values[r].base.info.id);
                    writeRelationMember(self->relations.values + r, country);
                    break;
                case OSM_CHANGE_COUNT:
                case OSM_CHANGE_NONE:
                    //Just do nothing, as db is up to date
                    break;
                    
            }
            commitTransaction(country->db);
            written++;
        } else {
            deleteRelationFromCountry(country, self->relations.values[r].base.info.id);
            //printf("Skip relation %i. %s\n", self->relations.values[r].info.id, isInTree16(&pseudoIndex, self->relations.values[r].info.id) ? (relationIsFullyBelongsCountry(self->relations.values + r, country, &pseudoIndex)? "HM.." : "It does not have some members.") : "it is not in list");
        }
    }
    freeTree16(&pseudoIndex);
    printf("Relations written: %i\n", written);
}

static void prepareStatement(sqlite3* db, const char* text, sqlite3_stmt** statement) {
    if (sqlite3_prepare(db, text, -1, statement, NULL) != SQLITE_OK) {
        fprintf(stderr, "Error: failed to prepare statement with message '%s'. Full Text: \n %s\n", sqlite3_errmsg(db), text);
    }
}

typedef void (*CountryInitializer)(LCountry* self);

void initOsm2OlmInternal(osm2olm* self, const char* outputDirectory, CountryPolygon* polygons, int polygonsCount, CountryInitializer initCountry) {
    initOsmStreamReader(&(self->reader), self);
    
    self->reader.newTag = newTag;
    self->reader.newNode = newNode;
    self->reader.newWayNode = newWayNode;
    self->reader.newWay = newWay;
    self->reader.newRelationMember = newRelationMember;
    self->reader.newRelation = newRelation;
    
    self->reader.finishNode[OSM_CHANGE_NONE] = writeNode;
    self->reader.finishWay[OSM_CHANGE_NONE] = writeWay;
    self->reader.finishRelation[OSM_CHANGE_NONE] = writeRelation;
    
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
    
    self->countries = calloc(sizeof(LCountry), polygonsCount);
    self->countriesCount = polygonsCount;
    
    if(-1 == mkdir(outputDirectory, S_IRWXU) && errno != EEXIST) {  
        printf("Error creating directory %s: %i\n", outputDirectory, errno);        
    }
    
    
    for(int p=0;p <polygonsCount; p++) {
        self->countries[p].polygon = polygons + p;
        printf("Country %s\n", self->countries[p].polygon->name);
        char* countryFileName = fullFileName(polygons[p].name, outputDirectory);
        
        initTree16(&(self->countries[p].nodesIndex));
        initTree16(&(self->countries[p].waysIndex));
        initTree16(&(self->countries[p].relationsIndex));
        
        sqlite3* db = NULL;
        if(sqlite3_open(strdup(countryFileName), &db) != SQLITE_OK){
            fprintf(stderr, "Error open db %s: %s", countryFileName, sqlite3_errmsg(db));
        }
        free(countryFileName);
        self->countries[p].db = db;
        
        initCountry(self->countries + p);
        
        printf("Done.\n");
    }
}

void initInsertStatements(LCountry* country) {
    sqlite3* db = country->db;
    prepareStatement(db, "INSERT INTO current_nodes(id, latitude, longitude, visible, timestamp, tile) VALUES (?1, ?2, ?3, 1, ?4, -1)", 
                     &(country->insertNodeStatement));
    
    prepareStatement(db, "INSERT INTO current_node_tags(id, k, v) VALUES (?1, ?2, ?3)", 
                     &(country->insertNodeTagStatement));
    
    prepareStatement(db, "INSERT INTO current_ways(id, visible, timestamp, user_id) VALUES (?1, 1, ?2, -1)", &(country->insertWayStatement));
    prepareStatement(db, "INSERT INTO current_way_tags(id, k, v) VALUES (?1, ?2, ?3)", &(country->insertWayTagStatement));
    prepareStatement(db, "INSERT INTO current_way_nodes(id, node_id, sequence_id) VALUES (?1, ?2, ?3)", &(country->insertWayNodeStatement));
    
    prepareStatement(db, "INSERT INTO current_relations(id, visible, timestamp, user_id) VALUES (?1, 1, ?2, -1)", &(country->insertRelationStatement));
    prepareStatement(db, "INSERT INTO current_relation_tags(id, k, v) VALUES (?1, ?2, ?3)", &(country->insertRelationTagStatement));
    prepareStatement(db, "INSERT INTO current_relation_members(id, member_type, member_id, member_role, sequence_id) VALUES (?1, ?2, ?3, ?4, ?5)", &(country->insertRelationMemberStatement));

    country->nodeExistsStatement = NULL;
    country->wayExistsStatement = NULL;
    country->relationExistsStatement = NULL;
}

void prepareForBulkImport(sqlite3* db) {
    sqlite3_exec(db, "PRAGMA synchronous = OFF            ", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA jounal_mode = MEMORY         ", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA locking_mode = EXCLUSIVE     ", NULL, NULL, NULL);        
}

void initCountryForInserts(LCountry* country) {
    sqlite3* db = country->db;
    prepareForBulkImport(db);
    
    sqlite3_exec(db, "DELETE FROM current_nodes           ", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM current_ways            ", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM current_way_nodes       ", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM current_relations       ", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM current_relation_members", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM current_node_tags       ", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM current_way_tags        ", NULL, NULL, NULL);
    sqlite3_exec(db, "DELETE FROM current_relation_tags   ", NULL, NULL, NULL);
    
    sqlite3_exec(db, "CREATE TABLE current_nodes            (id INTEGER PRIMARY KEY, latitude, longitude, visible, timestamp, tile)", NULL, NULL, NULL);
    sqlite3_exec(db, "CREATE TABLE current_ways             (id INTEGER PRIMARY KEY, visible, timestamp, user_id)", NULL, NULL, NULL);
    sqlite3_exec(db, "CREATE TABLE current_way_nodes        (id INTEGER, node_id INTEGER, sequence_id INTEGER)", NULL, NULL, NULL);
    sqlite3_exec(db, "CREATE TABLE current_relations        (id INTEGER PRIMARY KEY, visible, timestamp, user_id)", NULL, NULL, NULL);
    sqlite3_exec(db, "CREATE TABLE current_relation_members (id INTEGER, member_type, member_id, member_role, sequence_id INTEGER)", NULL, NULL, NULL);
    sqlite3_exec(db, "CREATE TABLE current_node_tags        (id INTEGER, k TEXT, v TEXT, UNIQUE (id, k))", NULL, NULL, NULL);
    sqlite3_exec(db, "CREATE TABLE current_way_tags         (id INTEGER, k TEXT, v TEXT, UNIQUE (id, k))", NULL, NULL, NULL);
    sqlite3_exec(db, "CREATE TABLE current_relation_tags    (id INTEGER, k TEXT, v TEXT, UNIQUE (id, k))", NULL, NULL, NULL);
    
    country->ifNodeExists = checkNodeInTree16;
    country->ifWayExists = checkWayInTree16;
    country->ifRelationExists = checkRelationInTree16;

    
    initInsertStatements(country);
}

void initOsm2OlmWithOutputDirectory(osm2olm* self, const char* outputDirectory, CountryPolygon* polygons, int polygonsCount) {
    initOsm2OlmInternal(self, outputDirectory, polygons, polygonsCount, initCountryForInserts);
}

void convertOsm2OlmFromFile(osm2olm* self, const char *filename) {
    readOsmFromFile(&(self->reader), filename);
}

void convertOsm2OlmFromStdin(osm2olm* self) {
    readOsmFromStdin(&(self->reader));
}

void closeOsm2OlmInternal(osm2olm* self, char vacuum) {
    for(int c=0;c < self->countriesCount; c++) {
        
        writeRelations(self, self->countries + c);
        
        LCountry country = self->countries[c];
        sqlite3* db = country.db;
        
        sqlite3_exec(db, "CREATE INDEX way_nodes_way_index                 ON current_way_nodes        (id      ASC);", NULL, NULL, NULL);
		sqlite3_exec(db, "CREATE INDEX way_nodes_node_index                ON current_way_nodes        (node_id ASC);", NULL, NULL, NULL);
		
		sqlite3_exec(db, "CREATE INDEX way_tags_way_index                  ON current_way_tags         (id      ASC);", NULL, NULL, NULL);
		sqlite3_exec(db, "CREATE INDEX node_tags_node_index                ON current_node_tags        (id      ASC);", NULL, NULL, NULL);
		sqlite3_exec(db, "CREATE INDEX relation_tags_relation_index        ON current_relation_tags    (id      ASC);", NULL, NULL, NULL);
        
		sqlite3_exec(db, "CREATE INDEX relation_member_member_index        ON current_relation_members      (member_id, member_type);", NULL, NULL, NULL);
		sqlite3_exec(db, "CREATE INDEX relation_member_relation_index      ON current_relation_members      (id ASC);", NULL, NULL, NULL);
        
        sqlite3_exec(db, "PRAGMA synchronous = NORMAL", NULL, NULL, NULL);
        sqlite3_exec(db, "PRAGMA jounal_mode = DELETE", NULL, NULL, NULL);
        sqlite3_exec(db, "PRAGMA locking_mode = NORMAL", NULL, NULL, NULL);
        if (vacuum) {
            sqlite3_exec(db, "VACUUM", NULL, NULL, NULL);
        }
        
        sqlite3_finalize(country.nodeExistsStatement);
        sqlite3_finalize(country.wayExistsStatement);
        sqlite3_finalize(country.relationExistsStatement);
        
        
        sqlite3_finalize(country.insertNodeStatement);
        sqlite3_finalize(country.insertNodeTagStatement);
        sqlite3_finalize(country.insertWayStatement);
        sqlite3_finalize(country.insertWayTagStatement);
        sqlite3_finalize(country.insertWayNodeStatement);
        sqlite3_finalize(country.insertRelationStatement);
        sqlite3_finalize(country.insertRelationTagStatement);
        sqlite3_finalize(country.insertRelationMemberStatement);
        
        sqlite3_finalize(country.updateNodeStatement);
        sqlite3_finalize(country.updateWayStatement);
        sqlite3_finalize(country.updateRelationStatement);
        
        sqlite3_finalize(country.deleteNodeStatement);
        sqlite3_finalize(country.deleteNodeTagsStatement);
        sqlite3_finalize(country.deleteWayStatement);
        sqlite3_finalize(country.deleteWayTagsStatement);
        sqlite3_finalize(country.deleteWayNodesStatement);
        sqlite3_finalize(country.deleteRelationStatement);
        sqlite3_finalize(country.deleteRelationTagsStatement);
        sqlite3_finalize(country.deleteRelationMembersStatement);
        
        sqlite3_close(db);
    }    
    clearRelationChanges(&(self->relations));
    
    closeOsmStreamReader(&(self->reader));
}

void closeOsm2Olm(osm2olm* self) {
    closeOsm2OlmInternal(self, 1);
}

static void initOlm(olm* self, const char* fileName) {
    self->currentNode.tags.values = NULL;
    self->currentNode.tags.count = 0;
    self->currentWay.tags.values = NULL;
    self->currentWay.tags.count = 0;
    self->currentRelation.tags.values = NULL;
    self->currentRelation.tags.count = 0;
    
    self->currentWay.wayNodes.values = NULL;
    self->currentWay.wayNodes.count = 0;
    self->currentRelation.relationMembers.values = NULL;
    self->currentRelation.relationMembers.count = 0;    
    sqlite3_open(fileName, &(self->db));
    prepareStatement(self->db, "SELECT id, latitude, longitude FROM current_nodes", &(self->nodeStatement));
    prepareStatement(self->db, "SELECT id FROM current_ways", &(self->wayStatement));
    prepareStatement(self->db, "SELECT id FROM current_relations", &(self->relationStatement));
    
    prepareStatement(self->db, "SELECT k, v FROM current_node_tags WHERE id = ?1", &(self->nodeTagsStatement));
    prepareStatement(self->db, "SELECT k, v FROM current_way_tags WHERE id = ?1", &(self->wayTagsStatement));
    prepareStatement(self->db, "SELECT k, v FROM current_relation_tags WHERE id = ?1", &(self->relationTagsStatement));
    
    prepareStatement(self->db, "SELECT current_nodes.id, latitude, longitude FROM current_way_nodes JOIN current_nodes ON node_id = current_nodes.id WHERE current_way_nodes.id = ?1 ORDER BY sequence_id ASC", &(self->wayNodesStatement));
    prepareStatement(self->db, "SELECT member_type, member_id, member_role FROM current_relation_members WHERE id = ?1", &(self->relationMembersStatement));
    
    prepareStatement(self->db, "SELECT id FROM current_ways WHERE id = ?1", &(self->wayWithIdStatement));
    prepareStatement(self->db, "SELECT id, latitude, longitude FROM current_nodes WHERE id = ?1", &(self->nodeWithIdStatement));
    prepareStatement(self->db, "SELECT id FROM current_relations WHERE id = ?1", &(self->relationWithIdStatement));
}

static void readLTags(olm* self, sqlite3_stmt* statement, PlainTags* tags, OsmId ownerId) {
    sqlite3_bind_int(statement, 1, ownerId);
    removeAllPlainTags(tags);
    while(sqlite3_step(statement) == SQLITE_ROW) {
        if(tags->capacity < tags->count + 1) {
            tags->capacity += 10;
            tags->values = realloc(tags->values, tags->capacity * sizeof(PlainTag));
        }
        tags->values[tags->count].key = utf8dup(sqlite3_column_text(statement, 0));
        tags->values[tags->count].value = utf8dup(sqlite3_column_text(statement, 1));
        tags->count++;
    }
    sqlite3_reset(statement);
}

static Node* readLNode(olm* self, Node* node) {
    //printf("Reading node...\n");
    node->info.id = sqlite3_column_int(self->nodeStatement, 0);
    node->info.lat = sqlite3_column_int(self->nodeStatement, 1);
    node->info.lon = sqlite3_column_int(self->nodeStatement, 2);
    //printf("Reading node tags...\n");
    readLTags(self, self->nodeTagsStatement, &(node->tags), node->info.id);
    //printf("Read.\n");
    return node;
}

static void readLWayNodes(olm* self, Way* way) {
    //printf("Bind id...\n");
    sqlite3_bind_int(self->wayNodesStatement, 1, way->info.id);
    //printf("Clear old nodes...\n");
    removeAllNodesInfo(&(way->wayNodes));
    //printf("Reading..");
    while(sqlite3_step(self->wayNodesStatement) == SQLITE_ROW) {
        if(way->wayNodes.capacity < way->wayNodes.count + 1) {
            way->wayNodes.capacity += 10;
            way->wayNodes.values = realloc(way->wayNodes.values, way->wayNodes.capacity * sizeof(NodeInfo));
        }
        way->wayNodes.values[way->wayNodes.count].id = sqlite3_column_int(self->wayNodesStatement, 0);
        way->wayNodes.values[way->wayNodes.count].lat = sqlite3_column_int(self->wayNodesStatement, 1);
        way->wayNodes.values[way->wayNodes.count].lon = sqlite3_column_int(self->wayNodesStatement, 2);
        way->wayNodes.count++;
    }
    //printf("Nodes read\n");
    sqlite3_reset(self->wayNodesStatement);
}

static Way* readLWay(olm* self, Way* way) {
    //printf("Reading way...\n");
    way->info.id = sqlite3_column_int(self->wayStatement, 0);
    //printf("Reading way tags...\n");
    readLTags(self, self->wayTagsStatement, &(way->tags), way->info.id);
    //printf("Reading nodes...\n");
    readLWayNodes(self, way);
    //printf("Read\n");
    return way;
}

static void readLRelationMembers(olm* self, Relation* relation) {
    sqlite3_bind_int(self->relationMembersStatement, 1, relation->info.id);
    removeAllRelationMembers(&(relation->relationMembers));
    while(sqlite3_step(self->relationMembersStatement) == SQLITE_ROW) {
        if(relation->relationMembers.capacity < relation->relationMembers.count + 1) {
            relation->relationMembers.capacity += 10;
            relation->relationMembers.values = realloc(relation->relationMembers.values, relation->relationMembers.capacity * sizeof(RelationMemberInfo));
        }
        relation->relationMembers.values[relation->relationMembers.count].type = string2relationMemberType((UTF8*)sqlite3_column_text(self->relationMembersStatement, 0));
        relation->relationMembers.values[relation->relationMembers.count].ref = sqlite3_column_int(self->relationMembersStatement, 1);
        relation->relationMembers.values[relation->relationMembers.count].role = utf8dup(sqlite3_column_text(self->relationMembersStatement, 2));
        relation->relationMembers.count++;
    }
    sqlite3_reset(self->relationMembersStatement);
}

static Relation* readLRelation(olm* self, Relation* relation) {
    relation->info.id = sqlite3_column_int(self->relationStatement, 0);
    readLTags(self, self->relationTagsStatement, &(relation->tags), relation->info.id);
    readLRelationMembers(self, relation);
    return relation;
}

static Node* nextLNode(void* self) {
    //printf("Retrive next node..\n");
    if(sqlite3_step(((olm*)self)->nodeStatement) == SQLITE_ROW) {
        return readLNode((olm*) self, &(((olm*)self)->currentNode));
    }    
    //printf("No more nodes..\n");
    return NULL;
}

static Way* nextLWay(void* self) {
    //printf("Next way...\n");
    if(sqlite3_step(((olm*)self)->wayStatement) == SQLITE_ROW) {
        return readLWay((olm*) self, &(((olm*)self)->currentWay));
    }
    //printf("No more ways...\n");
    return NULL;
}

static Relation* nextLRelation(void* self) {
    if(sqlite3_step(((olm*)self)->relationStatement) == SQLITE_ROW) {
        return readLRelation((olm*) self, &(((olm*)self)->currentRelation));
    }
    return NULL;
}

void closeOlmReader(void* abstractSelf) {
    olm* self = (olm*)abstractSelf;
    sqlite3_finalize(self->nodeStatement);
    sqlite3_finalize(self->wayStatement);
    sqlite3_finalize(self->relationStatement);
    sqlite3_finalize(self->nodeTagsStatement);
    sqlite3_finalize(self->wayTagsStatement);
    sqlite3_finalize(self->relationTagsStatement);
    sqlite3_finalize(self->wayNodesStatement);
    sqlite3_finalize(self->relationMembersStatement);
    sqlite3_close(self->db);
    free(self->currentNode.tags.values);
    free(self->currentWay.tags.values);
    free(self->currentRelation.tags.values);
}

static void restartLNodes(void* self) {
    sqlite3_reset(((olm*) self)->nodeStatement);
}

static void restartLWays(void* self) {
    sqlite3_reset(((olm*) self)->wayStatement);
}

static void restartLRelations(void* self) {
    sqlite3_reset(((olm*) self)->relationStatement);
}

static Way* lWayWithId(void* self, OsmId id) {
    Way* way = calloc(sizeof(Way), 1);
    sqlite3_bind_int(((olm*)self)->wayWithIdStatement, 1, id);
    if(sqlite3_step(((olm*)self)->wayWithIdStatement) == SQLITE_ROW) {
        readLWay((olm*) self, way);
        sqlite3_reset(((olm*)self)->wayWithIdStatement);
        return way;
    }
    sqlite3_reset(((olm*)self)->wayWithIdStatement);
    return NULL;
}

OsmDbReader* newOlmReader(const char* fileName) {
    OsmDbReader* reader = calloc(sizeof(OsmDbReader), 1);
    
    olm* self = calloc(sizeof(olm), 1);
    initOlm(self, fileName);
    initOsmDbReader(reader, self);
    reader->nextNode = nextLNode;
    reader->nextWay = nextLWay;
    reader->nextRelation = nextLRelation;
    
    reader->restartNodes = restartLNodes;
    reader->restartWays = restartLWays;
    reader->restartRelations = restartLRelations;
    
    reader->wayWithId = lWayWithId;
    
    reader->close = closeOlmReader;
    return reader;    
}



static void updateNode(void* abstractSelf) {
    osm2olm* self = (osm2olm*) abstractSelf;
    
    //printf("Write node\n");
    for(int c = 0; c < self->countriesCount; c++) {
        //printf("Check if belongs Country %i\n", c);
        if(nodeBelongsCountry(self, self->countries + c)) {
            //printf("Write node in country\n");
            //            printf("Updating node %i\n", self->node.id);
            addTree16Node(&(self->countries[c].nodesIndex), self->node.id, self->node.id);
            //printf("Added to index\n");
            //printf("Adding to db with statement: %x\n", self->countries[c].insertNodeStatement);
            beginTransaction(self->countries[c].db);
            sqlite3_bind_int(self->countries[c].updateNodeStatement, 1, self->node.id);
            sqlite3_bind_int(self->countries[c].updateNodeStatement, 2, self->node.lat);
            sqlite3_bind_int(self->countries[c].updateNodeStatement, 3, self->node.lon);
            //printf("Binded\n");
            sqlite3_step(self->countries[c].updateNodeStatement);
            //printf("Written\n");
            sqlite3_reset(self->countries[c].updateNodeStatement);
            //printf("Reset\n");
            deleteFromDbById(self->countries[c].db, self->countries[c].deleteNodeTagsStatement, self->node.id);
            writeTags(self, &(self->tags), self->countries[c].insertNodeTagStatement, self->node.id);
            //printf("Writing tags\n");
            commitTransaction(self->countries[c].db);
        } else {
            deleteNodeFromCountry(self->countries + c, self->node.id);
        }
    }
    //printf("Write node - end\n");
    removeAllPlainTags(&(self->tags));
}

static void deleteNode(void* abstractSelf) {
    osm2olm* self = (osm2olm*) abstractSelf;
    
    //    printf("Deleting node %i\n", self->node.id);
    for(int c = 0; c < self->countriesCount; c++) {
        beginTransaction(self->countries[c].db);
        deleteNodeFromCountry(self->countries + c, self->node.id);
        commitTransaction(self->countries[c].db);
    }
    //printf("Delete node - end\n");
    removeAllPlainTags(&(self->tags));
}


static void updateWay(void* abstractSelf) {
    osm2olm* self = (osm2olm*) abstractSelf;
    
    //    printf("Updating way %i\n", self->way.id);
    for(int c = 0; c < self->countriesCount; c++) {
        if(wayBelongsCountry(self, self->countries + c)) {
            addTree16Node(&(self->countries[c].waysIndex), self->way.id, self->way.id);
            beginTransaction(self->countries[c].db);
            sqlite3_bind_int64(self->countries[c].updateWayStatement, 1, self->way.id);
            sqlite3_step(self->countries[c].updateWayStatement);
            sqlite3_reset(self->countries[c].updateWayStatement);
            deleteFromDbById(self->countries[c].db, self->countries[c].deleteWayTagsStatement, self->way.id);
            writeTags(self, &(self->tags), self->countries[c].insertWayTagStatement, self->way.id);
            deleteFromDbById(self->countries[c].db, self->countries[c].deleteWayNodesStatement, self->way.id);
            writeWayNodes(self, self->countries+c);
            commitTransaction(self->countries[c].db);
        } else {
            deleteWayFromCountry(self->countries + c, self->way.id);
        }
    }    
    removeAllPlainTags(&(self->tags));
    removeAllWayNodes(&(self->wayNodes));
}


static void deleteWay(void* abstractSelf) {
    osm2olm* self = (osm2olm*) abstractSelf;
    //printf("Deleting way %i\n", self->way.id);
    for(int c = 0; c < self->countriesCount; c++) {
        beginTransaction(self->countries[c].db);
        deleteWayFromCountry(self->countries + c, self->way.id);
        commitTransaction(self->countries[c].db);
    }    
    removeAllPlainTags(&(self->tags));
    removeAllWayNodes(&(self->wayNodes));
}

static void updateRelation(void* abstractSelf) {
    osm2olm* self = (osm2olm*) abstractSelf;
    int index = -1;
    
    for(int i = 0; i < self->relations.count; i++) {
        if(self->relations.values[i].base.info.id == self->relation.id) {
            index = i;
            break;
        }
    }
    
    if(index >= 0) {
        RelationChange* relation = self->relations.values + index;
        if (relation->change != OSM_CHANGE_NONE) {
            clearRelationMembers(&(relation->base.relationMembers));
            clearPlainTags(&(relation->base.tags));
        }
        relation->change = OSM_CHANGE_MODIFY;
        
        relation->base.info.id = self->relation.id;
        
        relation->base.relationMembers.values = malloc(sizeof(RelationMemberInfo) * self->relationMembers.count);
        relation->base.relationMembers.count = relation->base.relationMembers.capacity = self->relationMembers.count;
        for(int m=0;m<self->relationMembers.count;m++) {
            relation->base.relationMembers.values[m].ref = self->relationMembers.values[m].ref;
            relation->base.relationMembers.values[m].type = self->relationMembers.values[m].type;
            relation->base.relationMembers.values[m].role = utf8dup(self->relationMembers.values[m].role);
        }
        
        relation->base.tags.values = malloc(sizeof(PlainTag) * self->tags.count);
        relation->base.tags.count = relation->base.tags.capacity = self->tags.count;
        for(int t=0; t< self->tags.count;t++) {
            relation->base.tags.values[t].key = utf8dup(self->tags.values[t].key);
            relation->base.tags.values[t].value = utf8dup(self->tags.values[t].value);
        }
        
        removeAllPlainTags(&(self->tags));
        removeAllRelationMembers(&(self->relationMembers));        
    } else {
        writeRelationInternal(abstractSelf, self->countries->ifRelationExists(self->countries, self->relation.id) ? OSM_CHANGE_MODIFY : OSM_CHANGE_CREATE);
    }
    
}

static void deleteRelation(void* abstractSelf) {
    osm2olm* self = (osm2olm*) abstractSelf;
    
    int index = -1;
    for(int i = 0; i < self->relations.count; i++) {
        if(self->relations.values[i].base.info.id == self->relation.id) {
            index = i;
            break;
        }
    }
    
    if(index >= 0) {
        RelationChange* relation = self->relations.values + index;
        relation->change = OSM_CHANGE_DELETE;
        removeAllPlainTags(&(self->tags));
        removeAllRelationMembers(&(self->relationMembers));
    }
    
}

void initCountryForUpdatesCommon(LCountry* country) {
    sqlite3* db = country->db;
    prepareForBulkImport(db);
    initInsertStatements(country);
    prepareStatement(db, "INSERT OR REPLACE INTO current_nodes(id, latitude, longitude, visible, timestamp, tile) VALUES (?1, ?2, ?3, 1, ?4, -1)", &(country->updateNodeStatement));    
    prepareStatement(db, "INSERT OR REPLACE INTO current_ways(id, visible, timestamp, user_id) VALUES (?1, 1, ?2, -1)", &(country->updateWayStatement));
    prepareStatement(db, "INSERT OR REPLACE INTO current_relations(id, visible, timestamp, user_id) VALUES (?1, 1, ?2, -1)", &(country->updateRelationStatement));
    
    prepareStatement(db, "DELETE FROM current_nodes WHERE id = ?1", &(country->deleteNodeStatement));    
    prepareStatement(db, "DELETE FROM current_node_tags WHERE id = ?1", &(country->deleteNodeTagsStatement));
    
    prepareStatement(db, "DELETE FROM current_ways WHERE id = ?1", &(country->deleteWayStatement));
    prepareStatement(db, "DELETE FROM current_way_tags WHERE id = ?1", &(country->deleteWayTagsStatement));
    prepareStatement(db, "DELETE FROM current_way_nodes WHERE id = ?1", &(country->deleteWayNodesStatement));
    
    prepareStatement(db, "DELETE FROM current_relations WHERE id = ?1", &(country->deleteRelationStatement));
    prepareStatement(db, "DELETE FROM current_relation_tags WHERE id = ?1", &(country->deleteRelationTagsStatement));
    prepareStatement(db, "DELETE FROM current_relation_members WHERE id = ?1", &(country->deleteRelationMembersStatement));
}

static char checkInDb(sqlite3_stmt* statement, OsmId id) {
    sqlite3_bind_int64(statement, 1, id);
    if(sqlite3_step(statement) == SQLITE_ROW) {
        sqlite3_reset(statement);
        return 1;
    }
    sqlite3_reset(statement);
    return 0;
}

static char checkNodeInDb(void* abstractSelf, OsmId id) {
    return checkNodeInTree16(((LCountry*) abstractSelf), id) || checkInDb(((LCountry*) abstractSelf)->nodeExistsStatement, id);
}

static char checkWayInDb(void* abstractSelf, OsmId id) {
    return checkWayInTree16(((LCountry*) abstractSelf), id) || checkInDb(((LCountry*) abstractSelf)->wayExistsStatement, id);
}

static char checkRelationInDb(void* abstractSelf, OsmId id) {
    return checkRelationInTree16(((LCountry*) abstractSelf), id) || checkInDb(((LCountry*) abstractSelf)->relationExistsStatement, id);
}

void initCountryForUpdatesNoCache(LCountry* country) {
    initCountryForUpdatesCommon(country);

    sqlite3* db = country->db;
    prepareStatement(db, "SELECT id FROM current_nodes WHERE id = ?1 LIMIT 1", &(country->nodeExistsStatement));
    prepareStatement(db, "SELECT id FROM current_ways WHERE id = ?1 LIMIT 1", &(country->wayExistsStatement));
    prepareStatement(db, "SELECT id FROM current_relations WHERE id = ?1 LIMIT 1", &(country->relationExistsStatement));
    
    country->ifNodeExists = checkNodeInDb;
    country->ifWayExists = checkWayInDb;
    country->ifRelationExists = checkRelationInDb;
}

void initCountryForUpdates(LCountry* country) {
    initCountryForUpdatesCommon(country);
    
    sqlite3* db = country->db;
    sqlite3_stmt* preloadNodesStatement;
    sqlite3_stmt* preloadWaysStatement;
    
    country->ifNodeExists = checkNodeInTree16;
    country->ifWayExists = checkWayInTree16;
    country->ifRelationExists = checkRelationInTree16;
    
    prepareStatement(db, "SELECT id FROM current_nodes", &preloadNodesStatement);
    prepareStatement(db, "SELECT id FROM current_ways", &preloadWaysStatement);
    printf("Preloading nodes ids...\n");
    while(sqlite3_step(preloadNodesStatement) == SQLITE_ROW) {
        OsmId id = sqlite3_column_int(preloadNodesStatement, 0);
        addTree16Node(&(country->nodesIndex), id, id);
    }
    printf("Preloading ways ids...\n");
    while(sqlite3_step(preloadWaysStatement) == SQLITE_ROW) {
        OsmId id = sqlite3_column_int(preloadWaysStatement, 0);
        addTree16Node(&(country->waysIndex), id, id);
    }
    
    
    sqlite3_finalize(preloadNodesStatement);
    sqlite3_finalize(preloadWaysStatement);
    
}

void initOsd2Olm(osd2olm* self, const char* file, CountryPolygon* polygon, char fullMemory) {
    initOsm2OlmInternal((osm2olm*)self, ".", polygon, 1, fullMemory ? initCountryForUpdates : initCountryForUpdatesNoCache);
    
    if (fullMemory) {
        sqlite3* db = self->base.countries->db;
        sqlite3_stmt* preloadRelationsStatement;
        sqlite3_stmt* relationsCountStatement;
        prepareStatement(db, "SELECT id FROM current_relations", &preloadRelationsStatement);
        prepareStatement(db, "SELECT count(*) FROM current_relations", &relationsCountStatement);
        
        printf("Preloading relations ids...\n");
        
        if (sqlite3_step(relationsCountStatement) == SQLITE_ROW) {
            self->base.relations.count = self->base.relations.capacity = sqlite3_column_int(relationsCountStatement, 0);
            self->base.relations.values = malloc(sizeof(RelationChange) * self->base.relations.count);
            int i = 0;
            while (sqlite3_step(preloadRelationsStatement) == SQLITE_ROW) {
                RelationChange* relation = self->base.relations.values + i;
                relation->base.info.id = sqlite3_column_int(preloadRelationsStatement, 0);
                relation->change = OSM_CHANGE_NONE;
                relation->base.relationMembers.count = relation->base.relationMembers.capacity = 0;
                relation->base.tags.count = relation->base.tags.capacity = 0;
                relation->base.relationMembers.values = NULL;
                relation->base.tags.values = NULL;
                i++;
            }
            printf("%i relations on DB.\n", i);
            
        } else {
            self->base.relations.count = self->base.relations.capacity = 0;
        }
        
        sqlite3_finalize(preloadRelationsStatement);
        sqlite3_finalize(relationsCountStatement);
    }
    
    self->base.reader.finishWay[OSM_CHANGE_NONE] = NULL;
    self->base.reader.finishNode[OSM_CHANGE_NONE] = NULL;
    self->base.reader.finishRelation[OSM_CHANGE_NONE] = NULL;
    
    self->base.reader.finishNode[OSM_CHANGE_CREATE] = writeNode;
    self->base.reader.finishWay[OSM_CHANGE_CREATE] = writeWay;
    self->base.reader.finishRelation[OSM_CHANGE_CREATE] = writeRelation;
    
    self->base.reader.finishNode[OSM_CHANGE_MODIFY] = updateNode;
    self->base.reader.finishWay[OSM_CHANGE_MODIFY] = updateWay;
    self->base.reader.finishRelation[OSM_CHANGE_MODIFY] = updateRelation;
    
    self->base.reader.finishNode[OSM_CHANGE_DELETE] = deleteNode;
    self->base.reader.finishWay[OSM_CHANGE_DELETE] = deleteWay;
    self->base.reader.finishRelation[OSM_CHANGE_DELETE] = deleteRelation;
}

void convertOsd2OlmFromFile(osd2olm* self, const char *filename) {
    readOsmFromFile(&(self->base.reader), filename);
}

void convertOsd2OlmFromStdin(osd2olm* self) {
    readOsmFromStdin(&(self->base.reader));
}

void closeOsd2Olm(osd2olm* self) {
    closeOsm2OlmInternal((osm2olm*)self, 0);
}

