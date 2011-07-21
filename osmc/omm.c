/*
 *  omm.c
 *  OSMapper
 *
 *  Created by Egor Leonenko on 28.9.09.
 *  Copyright 2009 iTransition. All rights reserved.
 *
 */

#include "omm.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <libxml/xmlstring.h>
#include "utils.h"
#include <time.h>
#include <stdarg.h>



static int mysql_real_query_with_error(MYSQL* db, const char* query, unsigned long length) {
    int result = mysql_real_query(db, query, length);
    if(result) {
        fprintf(stderr, "Error executing ~~%s~~: %s.\n", query, mysql_error(db));
    } else {
        fprintf(stderr, "Executing ~~%s~~: OK.\n", query);
    }
    return result;
}

static int mysql_query_with_error(MYSQL* db, const char* query) {
    return mysql_real_query_with_error(db, query, strlen(query));
}

static int mysql_exec(mysql_stmt* statement, ...) {
    va_list args;
    va_start(args, statement);
    unsigned long length = statement->templateLength + 20;
    for (int j=0; j < statement->parametersCount; j++) {
        if (statement->parameters[j] == MYSQL_PARAM_INTEGER) {
            va_arg(args, int);
            length += 10;
        } else if(statement->parameters[j] == MYSQL_PARAM_LONG) {
            va_arg(args, long);
            length += 20;
        } else if (statement->parameters[j] == MYSQL_PARAM_TEXT) {
            UTF8* str = va_arg(args, UTF8*);
            length += utf8size(str) * 2 + 4;
        } else {
            fprintf(stderr, "Unsupported type %i.", statement->parameters[j]);
            return 0;
        }
    }
    va_end(args);
    
    char* query = calloc(sizeof(char), length);
    int end = 0;
    strcpy(query, statement->templateParts[0]);
    end += strlen(statement->templateParts[0]);
    va_start(args, statement);
    int i=0;
    int l;
    for (; i < statement->parametersCount; i++) {
        if (statement->parameters[i] == MYSQL_PARAM_INTEGER) {
            int ivalue = va_arg(args, int);
            l = sprintf(query + end, "%i", ivalue);
            end += l;
        } else if(statement->parameters[i] == MYSQL_PARAM_LONG) {
            long lvalue = va_arg(args, long);
            l = sprintf(query + end, "%li", lvalue);
            end += l;
        } else if (statement->parameters[i] == MYSQL_PARAM_TEXT) {
            const char* str = va_arg(args, const char*);
            *(query + end++) ='\'';
            end += mysql_real_escape_string(statement->db, query + end, str, strlen(str));
            *(query + end++)='\'';
        } else {
            fprintf(stderr, "Unsupported type %i.", statement->parameters[i]);
            return 0;
        }
        
        if(statement->templateParts[i+1]) {
            strcpy(query + end, statement->templateParts[i+1]);
            end += strlen(statement->templateParts[i+1]);
        }
    }
    
    va_end(args);
    int result = mysql_real_query_with_error(statement->db, query, end);
    free(query);
    return result;
}

static int nodeBelongsCountry(osm2omm* self, MCountry* country) {
    //printf("Check in Country with polygon: %x\n", country->polygon);
    return isPointInPolygon(self->node.lat, self->node.lon, country->polygon);
}

static char checkNodeInTree16(void* country, OsmId id) {
    return isInTree16(&(((MCountry*)country)->nodesIndex), id);
}

static char checkWayInTree16(void* country, OsmId id) {
    return isInTree16(&(((MCountry*)country)->waysIndex), id);
}

static char checkRelationInTree16(void* country, OsmId id) {
    return isInTree16(&(((MCountry*)country)->relationsIndex), id);
}

static int wayBelongsMCountry(osm2omm* self, MCountry* country) {
    for(int n = 0; n < self->wayNodes.count; n++) {
        if(country->ifNodeExists(country, self->wayNodes.values[n].ref)) {
            return 1;
        }
    }
    return 0;
}

static int relationBelongsMCountry(RelationChange* relation, MCountry* country, Tree16* relationsIndex) {
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

static int relationIsFullyBelongsCountry(Relation* relation, MCountry* country, Tree16* relationsIndex) {
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

static void beginTransaction(MYSQL* db) {
    mysql_query_with_error(db, "BEGIN");
}

static void commitTransaction(MYSQL* db) {
    mysql_query_with_error(db, "COMMIT");
}

static void rollbackTransaction(MYSQL* db) {
    mysql_query_with_error(db, "ROLLBACK");
}

static char deleteFromDbById(MYSQL* db, mysql_stmt* deleteStatement, OsmId id) {
    if(mysql_exec(deleteStatement, id)) {
        fprintf(stderr, "Could not delete object id %li: %s\n", (long) id, mysql_error(db));
        return 0;
    }
    return 1;
}

static void deleteNodeFromCountry(MCountry* country, OsmId id) {
    beginTransaction(&(country->db));
    if(deleteFromDbById(&(country->db), country->deleteNodeTagsStatement, id) &&
       deleteFromDbById(&(country->db), country->deleteNodeStatement, id)) {
        commitTransaction(&(country->db));
    } else {
        rollbackTransaction(&(country->db));
    }
}

static void deleteWayFromCountry(MCountry* country, OsmId id) {
    beginTransaction(&(country->db));
    if(deleteFromDbById(&(country->db), country->deleteWayTagsStatement, id) &&
       deleteFromDbById(&(country->db), country->deleteWayNodesStatement, id) &&
       deleteFromDbById(&(country->db), country->deleteWayStatement, id)) {
        commitTransaction(&(country->db));
    } else {
        rollbackTransaction(&(country->db));
    }
}

static void deleteRelationFromCountry(MCountry* country, OsmId id) {
    beginTransaction(&(country->db));
    if(deleteFromDbById(&(country->db), country->deleteRelationTagsStatement, id) &&
       deleteFromDbById(&(country->db), country->deleteRelationMembersStatement, id) &&
       deleteFromDbById(&(country->db), country->deleteRelationStatement, id)) {
        commitTransaction(&(country->db));
    } else {
        rollbackTransaction(&(country->db));
    }
}

static void newNode(void* abstractSelf, OsmId id, Coordinate lat, Coordinate lon, OsmTimestamp timestamp) {
    //printf("New node, %i\n", id);
    osm2omm* self = (osm2omm*) abstractSelf;
    self->node.id = id;
    self->node.lat = lat;
    self->node.lon = lon;
    self->node.timestamp = timestamp;
    //printf("New node, %i - End\n", id);
}

static void writeTags(osm2omm* self, PlainTags* tags, mysql_stmt* statement, OsmId ownerId) {
    for(int t = 0; t < tags->count; t++) {
        //printf("Writing tag %s=%s.\n", (char*) tags->values[t].key, (char*) tags->values[t].value);
        if(!utf8equal(tags->values[t].key, UTF8_CAST "created_by")) {
            mysql_exec(statement, ownerId, tags->values[t].key, tags->values[t].value);
        }
    }
}

static void writeNode(void* abstractSelf) {
    osm2omm* self = (osm2omm*) abstractSelf;
    
    for(int c = 0; c < self->countriesCount; c++) {
        if(nodeBelongsCountry(self, self->countries + c)) {
            addTree16Node(&(self->countries[c].nodesIndex), self->node.id, self->node.id);
//            printf("Writing node %li to DB.\n", self->node.id);
            beginTransaction(&(self->countries[c].db));
            mysql_exec(self->countries[c].insertNodeStatement, self->node.id, self->node.lat, self->node.lon, self->node.timestamp);
//            printf("Writing tags of node %li to DB.\n", self->node.id);
            writeTags(self, &(self->tags), self->countries[c].insertNodeTagStatement, self->node.id);
            commitTransaction(&(self->countries[c].db));
//            printf("Node %li has been written to DB.\n", self->node.id);
        }
    }
    self->tags.count = 0;
}

static void newWay(void* abstractSelf, OsmId id, OsmTimestamp timestamp) {
    osm2omm* self = (osm2omm*) abstractSelf;
    self->way.id = id;
    self->way.timestamp = timestamp;
}

static void writeWayNodes(osm2omm* self, MCountry* country) {
    mysql_stmt* statement = country->insertWayNodeStatement;
    int i = 0;
    for(int n = 0; n < self->wayNodes.count; n++) {
        if(country->ifNodeExists(country, self->wayNodes.values[n].ref)) {
            mysql_exec(statement, self->way.id, self->wayNodes.values[n].ref, i++);
        }
    }
}

static void writeWay(void* abstractSelf) {
    osm2omm* self = (osm2omm*) abstractSelf;
    
    for(int c = 0; c < self->countriesCount; c++) {
        if(wayBelongsMCountry(self, self->countries + c)) {
            addTree16Node(&(self->countries[c].waysIndex), self->way.id, self->way.id);
            beginTransaction(&(self->countries[c].db));
            mysql_exec(self->countries[c].insertWayStatement, self->way.id, self->way.timestamp);
            writeTags(self, &(self->tags), self->countries[c].insertWayTagStatement, self->way.id);
            writeWayNodes(self, self->countries+c);
            commitTransaction(&(self->countries[c].db));
        }
    }    
    removeAllPlainTags(&(self->tags));
    removeAllWayNodes(&(self->wayNodes));
}

static void newWayNode(void* abstractSelf, OsmId ref) {
    osm2omm* self = (osm2omm*) abstractSelf;
    if(self->wayNodes.capacity < self->wayNodes.count + 1) {
        self->wayNodes.capacity += 10;
        self->wayNodes.values = realloc(self->wayNodes.values, self->wayNodes.capacity * sizeof(WayNodeInfo));
    }
    self->wayNodes.values[self->wayNodes.count].ref = ref;
    self->wayNodes.count++;
}

static void newRelation(void* abstractSelf, OsmId id, OsmTimestamp timestamp) {
    osm2omm* self = (osm2omm*) abstractSelf;
    self->relation.id = id;
    self->relation.timestamp = timestamp;
}

static void writeRelationInternal(void* abstractSelf, OsmChangeType change) {
    osm2omm* self = (osm2omm*) abstractSelf;
    
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
    osm2omm* self = (osm2omm*) abstractSelf;
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
    osm2omm* self = (osm2omm*) abstractSelf;
    if(self->tags.capacity < self->tags.count + 1) {
        self->tags.capacity += 10;
        self->tags.values = realloc(self->tags.values, self->tags.capacity * sizeof(PlainTag));
    }
    self->tags.values[self->tags.count].key = utf8dup(key);
    self->tags.values[self->tags.count].value = utf8dup(value);
    self->tags.count++;
    //printf("End New tag\n");
}


static void writeRelationMember(RelationChange* relation, MCountry* country) {
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
            mysql_exec(country->insertRelationMemberStatement, 
                       relation->base.info.id, 
                       relationMemberType2String(relation->base.relationMembers.values[m].type), 
                       relation->base.relationMembers.values[m].ref, 
                       (char*)relation->base.relationMembers.values[m].role, 
                       i);
            i++;
        }
    }
}

static void writeRelations(osm2omm* self, MCountry* country) {
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
                    if((self->relations.values[r].change == OSM_CHANGE_NONE || relationBelongsMCountry(self->relations.values + r, country, &pseudoIndex))) {
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
            beginTransaction(&(country->db));
            addTree16Node(&(country->relationsIndex), self->relations.values[r].base.info.id, self->relations.values[r].base.info.id);
            if (self->relations.values[r].change == OSM_CHANGE_CREATE) {
                mysql_exec(country->insertRelationStatement, self->relations.values[r].base.info.id, self->relations.values[r].base.info.timestamp);
                writeTags(self, &(self->relations.values[r].base.tags), country->insertRelationTagStatement, self->relations.values[r].base.info.id);
                writeRelationMember(self->relations.values + r, country);
            } else if (self->relations.values[r].change == OSM_CHANGE_DELETE) {
                deleteRelationFromCountry(country, self->relations.values[r].base.info.id);
            } else if (self->relations.values[r].change == OSM_CHANGE_MODIFY) {
                mysql_exec(country->updateRelationStatement, self->relations.values[r].base.info.id, self->relations.values[r].base.info.timestamp);
                deleteFromDbById(&(country->db), country->deleteRelationTagsStatement, self->relations.values[r].base.info.id);
                writeTags(self, &(self->relations.values[r].base.tags), country->insertRelationTagStatement, self->relations.values[r].base.info.id);
                deleteFromDbById(&(country->db), country->deleteRelationMembersStatement, self->relations.values[r].base.info.id);
                writeRelationMember(self->relations.values + r, country);
            }
            commitTransaction(&country->db);
            written++;
        } else {
            deleteRelationFromCountry(country, self->relations.values[r].base.info.id);
            //printf("Skip relation %i. %s\n", self->relations.values[r].info.id, isInTree16(&pseudoIndex, self->relations.values[r].info.id) ? (relationIsFullyBelongsCountry(self->relations.values + r, country, &pseudoIndex)? "HM.." : "It does not have some members.") : "it is not in list");
        }
    }
    freeTree16(&pseudoIndex);
    printf("Relations written: %i\n", written);
}

static void prepareStatement(MYSQL* db, const char* text, mysql_stmt** statement) {
    *statement = calloc(sizeof(mysql_stmt), 1);
    (*statement)->db = db;
    unsigned long textLength = strlen(text);
    int parametersCount = 0;
    for(int j = 0; j < textLength - 2; j++) {
        if(text[j + 1] == '?' && text[j] != '?' && (text[j+2] == 's' || text[j+2] == 'l' || text[j+2] == 'i')) {
            parametersCount++;
        }
    }
    (*statement)->parametersCount = parametersCount;
    (*statement)->parameters = malloc(sizeof(MysqlParameter) * parametersCount);
    (*statement)->templatePartsCount = parametersCount + 1;
    (*statement)->templateParts = malloc(sizeof(char**) * (parametersCount + 1));
    (*statement)->templateParts[parametersCount] = NULL;
    char* partStart = (char*) text;
    int partNumber = 0;
    int i = 0;
    (*statement)->templateLength = 0;
    for(; i < textLength; i++) {
        if((i < textLength - 2 && text[i + 1] == '?' && text[i] != '?' && (text[i+2] == 's' || text[i+2] == 'l' || text[i+2] == 'i')) || i == (textLength - 1)) {
            int partLength = text + i - partStart + 1;
            (*statement)->templateLength += partLength;
            (*statement)->templateParts[partNumber] = malloc(sizeof(char) * (partLength + 1));
            strncpy((*statement)->templateParts[partNumber], partStart, partLength);
            if(i < textLength - 2) {
                switch (text[i+2]) {
                    case 's':
                        (*statement)->parameters[partNumber] = MYSQL_PARAM_TEXT;
                        break;
                    case 'l':
                        (*statement)->parameters[partNumber] = MYSQL_PARAM_LONG;
                        break;
                    case 'i':
                        (*statement)->parameters[partNumber] = MYSQL_PARAM_INTEGER;
                        break;
                }   
            }
            partNumber++;
            partStart = (char*)(text + i + 3);
            i+= 2;
        }
    }
}

static void mysql_finalize(mysql_stmt* statement) {
    if (statement) {
        for(int i=0; i<statement->templatePartsCount; i++) {
            if(statement->templateParts[i]) {
                free(statement->templateParts[i]);
            }
        }
        free(statement->templateParts);
        free(statement->parameters);
        free(statement);
    }
}

typedef void (*CountryInitializer)(MCountry* self);

void initOsm2OmmInternal(osm2omm* self, const char* host, const char* user, const char* password, CountryPolygon* polygons, int polygonsCount, CountryInitializer initCountry) {
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
    
    self->countries = calloc(sizeof(MCountry), polygonsCount);
    self->countriesCount = polygonsCount;
    
    
    for(int p=0;p <polygonsCount; p++) {
        self->countries[p].polygon = polygons + p;
        printf("Country %s\n", self->countries[p].polygon->name);
        
        initTree16(&(self->countries[p].nodesIndex));
        initTree16(&(self->countries[p].waysIndex));
        initTree16(&(self->countries[p].relationsIndex));
        
        MYSQL* db = &(self->countries[p].db);
        mysql_init(db);
        mysql_options(db, MYSQL_SET_CHARSET_NAME, "UTF8");
        if(!mysql_real_connect(db, host, user, password, NULL, 0, NULL, 0)) {
            fprintf(stderr, "Error connect ro db server %s:%s@%s: %s", user, password, host, mysql_error(db));
            continue;
        }
        
        printf("Connected to db with charset %s\n", mysql_character_set_name(db));
        
        initCountry(self->countries + p);
        
        printf("Done.\n");
    }
}

static void initInsertStatements(MCountry* country) {
    MYSQL* db = &(country->db);
    prepareStatement(db, "INSERT INTO current_nodes(id, latitude, longitude, visible, timestamp, tile) VALUES (?l, ?l, ?l, 1, ?l, -1)", 
                     &(country->insertNodeStatement));
    
    prepareStatement(db, "INSERT INTO current_node_tags(id, k, v) VALUES (?l, ?s, ?s)", 
                     &(country->insertNodeTagStatement));
    
    prepareStatement(db, "INSERT INTO current_ways(id, visible, timestamp, user_id) VALUES (?l, 1, ?l, -1)", 
                     &(country->insertWayStatement));
    prepareStatement(db, "INSERT INTO current_way_tags(id, k, v) VALUES (?l, ?s, ?s)", 
                     &(country->insertWayTagStatement));
    prepareStatement(db, "INSERT INTO current_way_nodes(id, node_id, sequence_id) VALUES (?l, ?l, ?l)", 
                     &(country->insertWayNodeStatement));
    
    prepareStatement(db, "INSERT INTO current_relations(id, visible, timestamp, user_id) VALUES (?l, 1, ?l, -1)", 
                     &(country->insertRelationStatement));
    prepareStatement(db, "INSERT INTO current_relation_tags(id, k, v) VALUES (?l, ?s, ?s)", 
                     &(country->insertRelationTagStatement));
    prepareStatement(db, "INSERT INTO current_relation_members(id, member_type, member_id, member_role, sequence_id) VALUES (?l, ?s, ?l, ?s, ?l)", 
                     &(country->insertRelationMemberStatement));
    
    country->nodeExistsStatement = NULL;
    country->wayExistsStatement = NULL;
    country->relationExistsStatement = NULL;
}

static void prepareForBulkImport(MYSQL* db) {
    //TODO
}

static void initCountryForInserts(MCountry* country) {
    MYSQL* db = &(country->db);
    
    char* dbQuery = calloc(sizeof(char), strlen(country->polygon->name) + 20);
    sprintf(dbQuery, "DROP DATABASE %s", country->polygon->name);
    mysql_query_with_error(db, dbQuery);
    
    sprintf(dbQuery, "CREATE DATABASE %s", country->polygon->name);
    mysql_query_with_error(db, dbQuery);
    free(dbQuery);
    
    mysql_select_db(db, country->polygon->name);
    
    prepareForBulkImport(db);
    
    mysql_query_with_error(db, "DROP TABLE IF EXISTS current_nodes");
    mysql_query_with_error(db, "DROP TABLE IF EXISTS current_ways");
    mysql_query_with_error(db, "DROP TABLE IF EXISTS current_way_nodes");
    mysql_query_with_error(db, "DROP TABLE IF EXISTS current_relations");
    mysql_query_with_error(db, "DROP TABLE IF EXISTS current_relation_members");
    mysql_query_with_error(db, "DROP TABLE IF EXISTS current_node_tags");
    mysql_query_with_error(db, "DROP TABLE IF EXISTS current_way_tags");
    mysql_query_with_error(db, "DROP TABLE IF EXISTS current_relation_tags");
    
    mysql_query_with_error(db, "CREATE TABLE current_nodes            (id INTEGER PRIMARY KEY, latitude INTEGER, longitude INTEGER, visible BIT, timestamp INTEGER, tile INTEGER)");
    mysql_query_with_error(db, "CREATE TABLE current_ways             (id INTEGER PRIMARY KEY, visible BIT, timestamp INTEGER, user_id INTEGER)");
    mysql_query_with_error(db, "CREATE TABLE current_way_nodes        (id INTEGER, node_id INTEGER, sequence_id INTEGER)");
    mysql_query_with_error(db, "CREATE TABLE current_relations        (id INTEGER PRIMARY KEY, visible BIT, timestamp INTEGER, user_id INTEGER)");
    mysql_query_with_error(db, "CREATE TABLE current_relation_members (id INTEGER, member_type VARCHAR(10), member_id INTEGER, member_role VARCHAR(255), sequence_id INTEGER)");
    mysql_query_with_error(db, "CREATE TABLE current_node_tags        (id INTEGER, k nvarchar(255), v nvarchar(255), UNIQUE KEY (id, k))");
    mysql_query_with_error(db, "CREATE TABLE current_way_tags         (id INTEGER, k nvarchar(255), v nvarchar(255), UNIQUE KEY (id, k))");
    mysql_query_with_error(db, "CREATE TABLE current_relation_tags    (id INTEGER, k nvarchar(255), v nvarchar(255), UNIQUE KEY (id, k))");
    
    country->ifNodeExists = checkNodeInTree16;
    country->ifWayExists = checkWayInTree16;
    country->ifRelationExists = checkRelationInTree16;
    
    
    initInsertStatements(country);
}

void initOsm2Omm(osm2omm* self, const char* host, const char* user, const char* password, CountryPolygon* polygons, int polygonsCount) {
    initOsm2OmmInternal(self, host, user, password, polygons, polygonsCount, initCountryForInserts);
}

void convertOsm2OmmFromFile(osm2omm* self, const char *filename) {
    readOsmFromFile(&(self->reader), filename);
}

void convertOsm2OmmFromStdin(osm2omm* self) {
    readOsmFromStdin(&(self->reader));
}

void closeOsm2OmmInternal(osm2omm* self, char createIndicies) {
    for(int c=0;c < self->countriesCount; c++) {
        
        writeRelations(self, self->countries + c);
        
        MCountry* country = self->countries + c;
        MYSQL* db = &(country->db);
        if (createIndicies) {
            mysql_query_with_error(db, "CREATE INDEX way_nodes_way_index                 ON current_way_nodes        (id      ASC);");
            mysql_query_with_error(db, "CREATE INDEX way_nodes_node_index                ON current_way_nodes        (node_id ASC);");
		
            mysql_query_with_error(db, "CREATE INDEX way_tags_way_index                  ON current_way_tags         (id      ASC);");
            mysql_query_with_error(db, "CREATE INDEX node_tags_node_index                ON current_node_tags        (id      ASC);");
            mysql_query_with_error(db, "CREATE INDEX relation_tags_relation_index        ON current_relation_tags    (id      ASC);");
        
            mysql_query_with_error(db, "CREATE INDEX relation_member_member_index        ON current_relation_members      (member_id, member_type);");
            mysql_query_with_error(db, "CREATE INDEX relation_member_relation_index      ON current_relation_members      (id ASC);");
        }
        
        
        mysql_finalize(country->nodeExistsStatement);
        mysql_finalize(country->wayExistsStatement);
        mysql_finalize(country->relationExistsStatement);
        
        
        mysql_finalize(country->insertNodeStatement);
        mysql_finalize(country->insertNodeTagStatement);
        mysql_finalize(country->insertWayStatement);
        mysql_finalize(country->insertWayTagStatement);
        mysql_finalize(country->insertWayNodeStatement);
        mysql_finalize(country->insertRelationStatement);
        mysql_finalize(country->insertRelationTagStatement);
        mysql_finalize(country->insertRelationMemberStatement);
        
        mysql_finalize(country->updateNodeStatement);
        mysql_finalize(country->updateWayStatement);
        mysql_finalize(country->updateRelationStatement);
        
        mysql_finalize(country->deleteNodeStatement);
        mysql_finalize(country->deleteNodeTagsStatement);
        mysql_finalize(country->deleteWayStatement);
        mysql_finalize(country->deleteWayTagsStatement);
        mysql_finalize(country->deleteWayNodesStatement);
        mysql_finalize(country->deleteRelationStatement);
        mysql_finalize(country->deleteRelationTagsStatement);
        mysql_finalize(country->deleteRelationMembersStatement);
        mysql_close(&(country->db));
    }
    clearRelationChanges(&(self->relations));
    
    closeOsmStreamReader(&(self->reader));
}

void closeOsm2Omm(osm2omm* self) {
    closeOsm2OmmInternal(self, 1);
}

static void initOmm(omm* self, const char* host, const char* user, const char* password, const char* database) {
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
    MYSQL* db = &(self->db);
    mysql_init(db);
    mysql_real_connect(db, host, user, password, database, 0, NULL, 0);
    prepareStatement(db, "SELECT id, latitude, longitude FROM current_nodes", &(self->nodeStatement));
    prepareStatement(db, "SELECT id FROM current_ways", &(self->wayStatement));
    prepareStatement(db, "SELECT id FROM current_relations", &(self->relationStatement));
    
    prepareStatement(db, "SELECT k, v FROM current_node_tags WHERE id = ?l", &(self->nodeTagsStatement));
    prepareStatement(db, "SELECT k, v FROM current_way_tags WHERE id = ?l", &(self->wayTagsStatement));
    prepareStatement(db, "SELECT k, v FROM current_relation_tags WHERE id = ?l", &(self->relationTagsStatement));
    
    prepareStatement(db, "SELECT current_nodes.id, latitude, longitude FROM current_way_nodes JOIN current_nodes ON node_id = current_nodes.id WHERE current_way_nodes.id = ?l ORDER BY sequence_id ASC", &(self->wayNodesStatement));
    prepareStatement(db, "SELECT member_type, member_id, member_role FROM current_relation_members WHERE id = ?l", &(self->relationMembersStatement));
    
    prepareStatement(db, "SELECT id FROM current_ways WHERE id = ?l", &(self->wayWithIdStatement));
    prepareStatement(db, "SELECT id, latitude, longitude FROM current_nodes WHERE id = ?l", &(self->nodeWithIdStatement));
    prepareStatement(db, "SELECT id FROM current_relations WHERE id = ?l", &(self->relationWithIdStatement));
}
/*
static void readMTags(omm* self, mysql_stmt* statement, PlainTags* tags, OsmId ownerId) {
    MYSQL_bind_int(statement, 1, ownerId);
    removeAllPlainTags(tags);
    while(MYSQL_step(statement) == SQLITE_ROW) {
        if(tags->capacity < tags->count + 1) {
            tags->capacity += 10;
            tags->values = realloc(tags->values, tags->capacity * sizeof(PlainTag));
        }
        tags->values[tags->count].key = utf8dup(MYSQL_column_text(statement, 0));
        tags->values[tags->count].value = utf8dup(MYSQL_column_text(statement, 1));
        tags->count++;
    }
    MYSQL_reset(statement);
}

static Node* readMNode(omm* self, Node* node) {
    //printf("Reading node...\n");
    node->info.id = MYSQL_column_int(self->nodeStatement, 0);
    node->info.lat = MYSQL_column_int(self->nodeStatement, 1);
    node->info.lon = MYSQL_column_int(self->nodeStatement, 2);
    //printf("Reading node tags...\n");
    readMTags(self, self->nodeTagsStatement, &(node->tags), node->info.id);
    //printf("Read.\n");
    return node;
}

static void readMWayNodes(omm* self, Way* way) {
    //printf("Bind id...\n");
    MYSQL_bind_int(self->wayNodesStatement, 1, way->info.id);
    //printf("Clear old nodes...\n");
    removeAllNodesInfo(&(way->wayNodes));
    //printf("Reading..");
    while(MYSQL_step(self->wayNodesStatement) == SQLITE_ROW) {
        if(way->wayNodes.capacity < way->wayNodes.count + 1) {
            way->wayNodes.capacity += 10;
            way->wayNodes.values = realloc(way->wayNodes.values, way->wayNodes.capacity * sizeof(NodeInfo));
        }
        way->wayNodes.values[way->wayNodes.count].id = MYSQL_column_int(self->wayNodesStatement, 0);
        way->wayNodes.values[way->wayNodes.count].lat = MYSQL_column_int(self->wayNodesStatement, 1);
        way->wayNodes.values[way->wayNodes.count].lon = MYSQL_column_int(self->wayNodesStatement, 2);
        way->wayNodes.count++;
    }
    //printf("Nodes read\n");
    MYSQL_reset(self->wayNodesStatement);
}

static Way* readMWay(omm* self, Way* way) {
    //printf("Reading way...\n");
    way->info.id = MYSQL_column_int(self->wayStatement, 0);
    //printf("Reading way tags...\n");
    readLTags(self, self->wayTagsStatement, &(way->tags), way->info.id);
    //printf("Reading nodes...\n");
    readLWayNodes(self, way);
    //printf("Read\n");
    return way;
}

static void readMRelationMembers(omm* self, Relation* relation) {
    MYSQL_bind_int(self->relationMembersStatement, 1, relation->info.id);
    removeAllRelationMembers(&(relation->relationMembers));
    while(MYSQL_step(self->relationMembersStatement) == SQLITE_ROW) {
        if(relation->relationMembers.capacity < relation->relationMembers.count + 1) {
            relation->relationMembers.capacity += 10;
            relation->relationMembers.values = realloc(relation->relationMembers.values, relation->relationMembers.capacity * sizeof(RelationMemberInfo));
        }
        relation->relationMembers.values[relation->relationMembers.count].type = string2relationMemberType((UTF8*)MYSQL_column_text(self->relationMembersStatement, 0));
        relation->relationMembers.values[relation->relationMembers.count].ref = MYSQL_column_int(self->relationMembersStatement, 1);
        relation->relationMembers.values[relation->relationMembers.count].role = utf8dup(MYSQL_column_text(self->relationMembersStatement, 2));
        relation->relationMembers.count++;
    }
    MYSQL_reset(self->relationMembersStatement);
}

static Relation* readMRelation(omm* self, Relation* relation) {
    relation->info.id = MYSQL_column_int(self->relationStatement, 0);
    readLTags(self, self->relationTagsStatement, &(relation->tags), relation->info.id);
    readLRelationMembers(self, relation);
    return relation;
}

static Node* nextMNode(void* self) {
    //printf("Retrive next node..\n");
    if(MYSQL_step(((omm*)self)->nodeStatement) == SQLITE_ROW) {
        return readMNode((omm*) self, &(((omm*)self)->currentNode));
    }    
    //printf("No more nodes..\n");
    return NULL;
}

static Way* nextMWay(void* self) {
    //printf("Next way...\n");
    if(MYSQL_step(((omm*)self)->wayStatement) == SQLITE_ROW) {
        return readMWay((omm*) self, &(((omm*)self)->currentWay));
    }
    //printf("No more ways...\n");
    return NULL;
}

static Relation* nextMRelation(void* self) {
    if(MYSQL_step(((omm*)self)->relationStatement) == SQLITE_ROW) {
        return readMRelation((omm*) self, &(((omm*)self)->currentRelation));
    }
    return NULL;
}
*/
void closeOmmReader(void* abstractSelf) {
    omm* self = (omm*)abstractSelf;
    mysql_finalize(self->nodeStatement);
    mysql_finalize(self->wayStatement);
    mysql_finalize(self->relationStatement);
    mysql_finalize(self->nodeTagsStatement);
    mysql_finalize(self->wayTagsStatement);
    mysql_finalize(self->relationTagsStatement);
    mysql_finalize(self->wayNodesStatement);
    mysql_finalize(self->relationMembersStatement);
    mysql_close(&(self->db));
    free(self->currentNode.tags.values);
    free(self->currentWay.tags.values);
    free(self->currentRelation.tags.values);
}/*

static void restartMNodes(void* self) {
    MYSQL_reset(((omm*) self)->nodeStatement);
}

static void restartMWays(void* self) {
    MYSQL_reset(((omm*) self)->wayStatement);
}

static void restartMRelations(void* self) {
    MYSQL_reset(((omm*) self)->relationStatement);
}

static Way* mWayWithId(void* self, OsmId id) {
    Way* way = calloc(sizeof(Way), 1);
    MYSQL_bind_int(((omm*)self)->wayWithIdStatement, 1, id);
    if(MYSQL_step(((omm*)self)->wayWithIdStatement) == SQLITE_ROW) {
        readMWay((omm*) self, way);
        return way;
    }
    return NULL;
}*/

OsmDbReader* newOmmReader(const char* host, const char* user, const char* password, const char* db) {
    OsmDbReader* reader = calloc(sizeof(OsmDbReader), 1);
    
    omm* self = calloc(sizeof(omm), 1);
    initOmm(self, host, user, password, db);
    initOsmDbReader(reader, self);/*
    reader->nextNode = nextMNode;
    reader->nextWay = nextMWay;
    reader->nextRelation = nextMRelation;
    
    reader->restartNodes = restartMNodes;
    reader->restartWays = restartMWays;
    reader->restartRelations = restartMRelations;
    
    reader->wayWithId = mWayWithId;*/
    
    reader->close = closeOmmReader;
    return reader;    
}



static void updateNode(void* abstractSelf) {
    osm2omm* self = (osm2omm*) abstractSelf;
    
    //printf("Write node\n");
    for(int c = 0; c < self->countriesCount; c++) {
        //printf("Check if belongs Country %i\n", c);
        if(nodeBelongsCountry(self, self->countries + c)) {
            //printf("Write node in country\n");
            //            printf("Updating node %i\n", self->node.id);
            addTree16Node(&(self->countries[c].nodesIndex), self->node.id, self->node.id);
            //printf("Added to index\n");
            //printf("Adding to db with statement: %x\n", self->countries[c].insertNodeStatement);
            beginTransaction(&(self->countries[c].db));
            mysql_exec(self->countries[c].updateNodeStatement, self->node.id, self->node.lat, self->node.lon);

            deleteFromDbById(&(self->countries[c].db), self->countries[c].deleteNodeTagsStatement, self->node.id);
            writeTags(self, &(self->tags), self->countries[c].insertNodeTagStatement, self->node.id);
            //printf("Writing tags\n");
            commitTransaction(&(self->countries[c].db));
        } else {
            deleteNodeFromCountry(self->countries + c, self->node.id);
        }
    }
    //printf("Write node - end\n");
    removeAllPlainTags(&(self->tags));
}

static void deleteNode(void* abstractSelf) {
    osm2omm* self = (osm2omm*) abstractSelf;
    
    //    printf("Deleting node %i\n", self->node.id);
    for(int c = 0; c < self->countriesCount; c++) {
        beginTransaction(&(self->countries[c].db));
        deleteNodeFromCountry(self->countries + c, self->node.id);
        commitTransaction(&(self->countries[c].db));
    }
    //printf("Delete node - end\n");
    removeAllPlainTags(&(self->tags));
}


static void updateWay(void* abstractSelf) {
    osm2omm* self = (osm2omm*) abstractSelf;
    
    //    printf("Updating way %i\n", self->way.id);
    for(int c = 0; c < self->countriesCount; c++) {
        if(wayBelongsMCountry(self, self->countries + c)) {
            addTree16Node(&(self->countries[c].waysIndex), self->way.id, self->way.id);
            beginTransaction(&(self->countries[c].db));
            mysql_exec(self->countries[c].updateWayStatement, 1);
            deleteFromDbById(&(self->countries[c].db), self->countries[c].deleteWayTagsStatement, self->way.id);
            writeTags(self, &(self->tags), self->countries[c].insertWayTagStatement, self->way.id);
            deleteFromDbById(&(self->countries[c].db), self->countries[c].deleteWayNodesStatement, self->way.id);
            writeWayNodes(self, self->countries+c);
            commitTransaction(&(self->countries[c].db));
        } else {
            deleteWayFromCountry(self->countries + c, self->way.id);
        }
    }    
    removeAllPlainTags(&(self->tags));
    removeAllWayNodes(&(self->wayNodes));
}


static void deleteWay(void* abstractSelf) {
    osm2omm* self = (osm2omm*) abstractSelf;
    //printf("Deleting way %i\n", self->way.id);
    for(int c = 0; c < self->countriesCount; c++) {
        beginTransaction(&(self->countries[c].db));
        deleteWayFromCountry(self->countries + c, self->way.id);
        commitTransaction(&(self->countries[c].db));
    }    
    removeAllPlainTags(&(self->tags));
    removeAllWayNodes(&(self->wayNodes));
}

static void updateRelation(void* abstractSelf) {
    osm2omm* self = (osm2omm*) abstractSelf;
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
        writeRelationInternal(abstractSelf, self->countries->ifRelationExists(self->countries,self->relation.id) ? OSM_CHANGE_MODIFY : OSM_CHANGE_CREATE);
    }
    
}

static void deleteRelation(void* abstractSelf) {
    osm2omm* self = (osm2omm*) abstractSelf;
    
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

static void initCountryForUpdatesCommon(MCountry* country) {
    MYSQL* db = &(country->db);
    mysql_select_db(db, country->polygon->name);
    prepareForBulkImport(db);
    initInsertStatements(country);
    prepareStatement(db, "REPLACE INTO current_nodes(id, latitude, longitude, visible, timestamp, tile) VALUES (?l, ?l, ?l, 1, ?l, -1)", 
                     &(country->updateNodeStatement));
    prepareStatement(db, "REPLACE INTO current_ways(id, visible, timestamp, user_id) VALUES (?l, 1, ?l, -1)", 
                     &(country->updateWayStatement));
    prepareStatement(db, "REPLACE INTO current_relations(id, visible, timestamp, user_id) VALUES (?l, 1, ?l, -1)", 
                     &(country->updateRelationStatement));
    
    prepareStatement(db, "DELETE FROM current_nodes WHERE id = ?l", 
                     &(country->deleteNodeStatement));
    prepareStatement(db, "DELETE FROM current_node_tags WHERE id = ?l", 
                     &(country->deleteNodeTagsStatement));
    
    prepareStatement(db, "DELETE FROM current_ways WHERE id = ?l", 
                     &(country->deleteWayStatement));
    prepareStatement(db, "DELETE FROM current_way_tags WHERE id = ?l", 
                     &(country->deleteWayTagsStatement));
    prepareStatement(db, "DELETE FROM current_way_nodes WHERE id = ?l", 
                     &(country->deleteWayNodesStatement));
    
    prepareStatement(db, "DELETE FROM current_relations WHERE id = ?l", 
                     &(country->deleteRelationStatement));
    prepareStatement(db, "DELETE FROM current_relation_tags WHERE id = ?l", 
                     &(country->deleteRelationTagsStatement));
    prepareStatement(db, "DELETE FROM current_relation_members WHERE id = ?l", 
                     &(country->deleteRelationMembersStatement));
}

static char checkInDb(mysql_stmt* statement, OsmId id) {
    mysql_exec(statement, id);
    MYSQL_RES* result = mysql_store_result(statement->db);    
    int count = mysql_num_rows(result);
    mysql_free_result(result);
    return count;
}

static char checkNodeInDb(void* abstractSelf, OsmId id) {
    return checkNodeInTree16(((MCountry*) abstractSelf), id) || checkInDb(((MCountry*) abstractSelf)->nodeExistsStatement, id);
}

static char checkWayInDb(void* abstractSelf, OsmId id) {
    return checkWayInTree16(((MCountry*) abstractSelf), id) || checkInDb(((MCountry*) abstractSelf)->wayExistsStatement, id);
}

static char checkRelationInDb(void* abstractSelf, OsmId id) {
    return checkRelationInTree16(((MCountry*) abstractSelf), id) || checkInDb(((MCountry*) abstractSelf)->relationExistsStatement, id);
}

static void initCountryForUpdatesNoCache(MCountry* country) {
    initCountryForUpdatesCommon(country);
    
    MYSQL* db = &(country->db);

    prepareStatement(db, "SELECT id FROM current_nodes WHERE id = ?l LIMIT 1", &(country->nodeExistsStatement));
    prepareStatement(db, "SELECT id FROM current_ways WHERE id = ?l LIMIT 1", &(country->wayExistsStatement));
    prepareStatement(db, "SELECT id FROM current_relations WHERE id = ?l LIMIT 1", &(country->relationExistsStatement));
    
    country->ifNodeExists = checkNodeInDb;
    country->ifWayExists = checkWayInDb;
    country->ifRelationExists = checkRelationInDb;
}

static void initCountryForUpdates(MCountry* country) {
    initCountryForUpdatesCommon(country);
    
    MYSQL* db = &(country->db);
    
    country->ifNodeExists = checkNodeInTree16;
    country->ifWayExists = checkWayInTree16;
    country->ifRelationExists = checkRelationInTree16;
    
    printf("Preloading nodes ids...\n");
    mysql_query_with_error(db, "SELECT id FROM current_nodes");
    MYSQL_RES* result = mysql_use_result(db);
    MYSQL_ROW row;
    while(row = mysql_fetch_row(result)) {
        OsmId id = atol(row[0]);
        addTree16Node(&(country->nodesIndex), id, id);
    }
    mysql_free_result(result);
    printf("Preloading ways ids...\n");
    mysql_query_with_error(db, "SELECT id FROM current_ways");
    result = mysql_use_result(db);
    while(row = mysql_fetch_row(result)) {
        OsmId id = atol(row[0]);
        addTree16Node(&(country->waysIndex), id, id);
    }
    mysql_free_result(result);        
}

void initOsd2Omm(osd2omm* self, const char* host, const char* user, const char* password, CountryPolygon* polygon, char fullMemory) {
    initOsm2OmmInternal((osm2omm*)self, host, user, password, polygon, 1, fullMemory ? initCountryForUpdates : initCountryForUpdatesNoCache);
    
    if (fullMemory) {
        MYSQL* db = &(self->base.countries->db);
        
        printf("Preloading relations ids...\n");
        mysql_query_with_error(db, "SELECT count(*) FROM current_relations");
        MYSQL_RES *result = mysql_store_result(db);
        MYSQL_ROW row = mysql_fetch_row(result);
        int count = atoi(row[0]);
        mysql_free_result(result);
        if (count) {
            self->base.relations.count = self->base.relations.capacity = count;
            self->base.relations.values = malloc(sizeof(RelationChange) * self->base.relations.count);
            int i = 0;
            mysql_query_with_error(db, "SELECT id FROM current_relations");
            result = mysql_use_result(db);
            while (row = mysql_fetch_row(result)) {
                RelationChange* relation = self->base.relations.values + i;
                relation->base.info.id = atol(row[0]);
                relation->change = OSM_CHANGE_NONE;
                relation->base.relationMembers.count = relation->base.relationMembers.capacity = 0;
                relation->base.tags.count = relation->base.tags.capacity = 0;
                relation->base.relationMembers.values = NULL;
                relation->base.tags.values = NULL;
                i++;
            }
            mysql_free_result(result);
            printf("%i relations on DB.\n", i);
            
        } else {
            self->base.relations.count = self->base.relations.capacity = 0;
        }
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

void convertOsd2OmmFromFile(osd2omm* self, const char *filename) {
    readOsmFromFile(&(self->base.reader), filename);
}

void convertOsd2OmmFromStdin(osd2omm* self) {
    readOsmFromStdin(&(self->base.reader));
}

void closeOsd2Omm(osd2omm* self) {
    closeOsm2OmmInternal((osm2omm*)self, 0);
}

