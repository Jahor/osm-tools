/*
 *  osm.c
 *  OSMapper
 *
 *  Created by Egor Leonenko on 9.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#include "osm.h"
#include <unistd.h>
#include <libxml/xmlreader.h>
#include <memory.h>
#include <zlib.h>

void freeRelationChange(RelationChange* relation){
    if(relation->change != OSM_CHANGE_NONE) {
        clearRelationMembers(&(relation->base.relationMembers));
        clearPlainTags(&(relation->base.tags));
    }
}

CollectionImplCustomElementFree(RelationChange, RelationChanges, 100)


OsmTimestamp parseTimestamp(char* timestamp) {
    //   2009-09-25T13:19:38Z 
    int year, month, day, hour, minute, second;
    sscanf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02dZ", &year, &month, &day, &hour, &minute, &second);
    struct tm tmstmp;
    //tmstmp.tm_gmtoff = 0;
    tmstmp.tm_year = year - 1900;
    tmstmp.tm_mon = month - 1;
    tmstmp.tm_mday = day;
    tmstmp.tm_hour = hour;
    tmstmp.tm_min= minute;
    tmstmp.tm_sec= second;
    tmstmp.tm_isdst = 0;
    //tmstmp.tm_zone = NULL;
    return timegm(&tmstmp);
}


void freePlainTag(PlainTag* tag) {
    free(tag->key);
    free(tag->value);
}
CollectionImplCustomElementFree(PlainTag, PlainTags, 5)

void freeRelationMemberInfo(RelationMemberInfo* member) {
    free(member->role);
}
CollectionImplCustomElementFree(RelationMemberInfo, RelationMembers, 5)

CollectionImplGeneric(WayNodeInfo, WayNodes, 10)

CollectionImplGeneric(NodeInfo, NodesInfo, 10)

void freeRelation(Relation* relation){
    clearRelationMembers(&(relation->relationMembers));
    clearPlainTags(&(relation->tags));
}

CollectionImplCustomElementFree(Relation, Relations, 100)

void freeWay(Way* way){
    removeAllNodesInfo(&(way->wayNodes));
    removeAllPlainTags(&(way->tags));
}
StaticCollectionImplCustomElementFree(Way, Ways)

void freeNode(Node* node){
    removeAllPlainTags(&(node->tags));
}
CollectionImplCustomElementFree(Node, Nodes, 100)


static const char* nodeTypeName(xmlReaderTypes type)
{
    switch (type) {
        case XML_READER_TYPE_NONE:
            return "NONE";
        case XML_READER_TYPE_ELEMENT:
            return "ELEMENT";
        case XML_READER_TYPE_ATTRIBUTE:
            return "ATTRIBUTE";
        case XML_READER_TYPE_TEXT:
            return "TEXT";
        case XML_READER_TYPE_CDATA:
            return "CDATA";
        case XML_READER_TYPE_ENTITY_REFERENCE:
            return "ENTITY REF";
        case XML_READER_TYPE_ENTITY:
            return "ENTITY";
        case XML_READER_TYPE_PROCESSING_INSTRUCTION:
            return "PROCESS INSTR";
        case XML_READER_TYPE_COMMENT:
            return "COMMENT";
        case XML_READER_TYPE_DOCUMENT:
            return "DOCUMENT";
        case XML_READER_TYPE_DOCUMENT_TYPE:
            return "DOCUMENT TYPE";
        case XML_READER_TYPE_DOCUMENT_FRAGMENT:
            return "DOCUMENT FRAGMENT";
        case XML_READER_TYPE_NOTATION:
            return "NOTATION";
        case XML_READER_TYPE_WHITESPACE:
            return "WHITESPACE";
        case XML_READER_TYPE_SIGNIFICANT_WHITESPACE:
            return "SIGNIFICANT WHITESPACE";
        case XML_READER_TYPE_END_ELEMENT:
            return "END ELEMENT";
        case XML_READER_TYPE_END_ENTITY:
            return "END ENTITY";
        case XML_READER_TYPE_XML_DECLARATION:
            return "XML DECLARATION";
            
        default:
            return "UNKNOWN";
    }
}

static void newNode(OsmStreamReader* self, xmlTextReaderPtr reader) {
    if(self->newNode) {
        //printf("Process new node\n");
        char* id = (char *)xmlTextReaderGetAttribute(reader, UTF8_CAST "id");
        char* lat = (char *)xmlTextReaderGetAttribute(reader, UTF8_CAST "lat");
        char* lon = (char *)xmlTextReaderGetAttribute(reader, UTF8_CAST "lon");
        char* timestamp = (char *)xmlTextReaderGetAttribute(reader, UTF8_CAST "timestamp");
    
        self->newNode(self->target, atoosmid(id), coordianteFromDouble(atof(lat)), coordianteFromDouble(atof(lon)), parseTimestamp(timestamp));

        free(id);
        free(lat);
        free(lon);
        free(timestamp);
    }
}

static void processTag(OsmStreamReader* self, xmlTextReaderPtr reader) {
    if(self->newTag) {
        if(self->currentEntityType == OSM_ENTITY_NODE || self->currentEntityType == OSM_ENTITY_WAY || self->currentEntityType == OSM_ENTITY_RELATION) {
            UTF8* key = xmlTextReaderGetAttribute(reader, UTF8_CAST "k");
            UTF8* value = xmlTextReaderGetAttribute(reader, UTF8_CAST "v");
            if(key == NULL || value == NULL) {
                if(key) {
                    free(key);
                }
                if(value) {
                    free(value);
                }
                fprintf(stderr, "EMPTY TAG\n");
                return;
            }
            self->newTag(self->target, self->currentEntityType, key, value);
            free(key);
            free(value);
        } else {
            fprintf(stderr, "TAG IN INVALID STATE: %i\n", self->currentEntityType);
        }
    }
}

static void processWayNode(OsmStreamReader* self, xmlTextReaderPtr reader) {
    if(self->currentEntityType != OSM_ENTITY_WAY) {
        fprintf(stderr, "WAY NODE IN INVALID STATE: %i\n", self->currentEntityType);
    } else {
        if(self->newWayNode) {
            char* ref = (char *)xmlTextReaderGetAttribute(reader, UTF8_CAST "ref");    
            self->newWayNode(self->target, atoosmid(ref));
            free(ref);
        }
    }
}

static void finishNode(OsmStreamReader* self) {
    if(self->finishNode[self->currentChangeType]) {
        self->finishNode[self->currentChangeType](self->target);
    }
}

static void finishWay(OsmStreamReader* self) {
    if(self->finishWay[self->currentChangeType]) {
        self->finishWay[self->currentChangeType](self->target);
    }
}

static void finishRelation(OsmStreamReader* self) {
    if(self->finishRelation[self->currentChangeType]) {
        self->finishRelation[self->currentChangeType](self->target);
    }
}

static void newWay(OsmStreamReader* self, xmlTextReaderPtr reader) {
    if(self->newWay) {   
        char* id = (char *)xmlTextReaderGetAttribute(reader, UTF8_CAST "id");
        char* timestamp = (char *)xmlTextReaderGetAttribute(reader, UTF8_CAST "timestamp");
        
        self->newWay(self->target, atoosmid(id), parseTimestamp(timestamp));
        free(id);
        free(timestamp);
    }
}

static void processNode(OsmStreamReader* self, xmlTextReaderPtr reader) {
    xmlReaderTypes type = xmlTextReaderNodeType(reader);
    if(type == XML_READER_TYPE_ELEMENT && (self->currentEntityType == OSM_ENTITY_MAP || self->currentEntityType == OSM_ENTITY_CHANGE_GROUP)) {
        self->currentEntityType = OSM_ENTITY_NODE;
        newNode(self, reader);
    } else if(type == XML_READER_TYPE_ELEMENT && self->currentEntityType == OSM_ENTITY_NODE) {
        finishNode(self);
        newNode(self, reader);
    } else if (type == XML_READER_TYPE_END_ELEMENT && self->currentEntityType == OSM_ENTITY_NODE) {
        finishNode(self);
        self->currentEntityType =  self->currentChangeType == OSM_CHANGE_NONE ? OSM_ENTITY_MAP :OSM_ENTITY_CHANGE_GROUP;
    } else {
        fprintf(stderr, "INVALID STATE AT END OF RELATION: %i\n", self->currentEntityType);
    }
}

static void processWay(OsmStreamReader* self, xmlTextReaderPtr reader) {
    xmlReaderTypes type = xmlTextReaderNodeType(reader);
    if(type == XML_READER_TYPE_ELEMENT && (self->currentEntityType == OSM_ENTITY_MAP || self->currentEntityType == OSM_ENTITY_CHANGE_GROUP)) {
        self->currentEntityType = OSM_ENTITY_WAY;
        newWay(self, reader);
    } else if(type == XML_READER_TYPE_ELEMENT && self->currentEntityType == OSM_ENTITY_NODE) {
        finishNode(self);
        self->currentEntityType = OSM_ENTITY_WAY;
        newWay(self,reader);
    } else if(type == XML_READER_TYPE_ELEMENT && self->currentEntityType == OSM_ENTITY_WAY) {
        finishWay(self);
        newWay(self, reader);
    } else if (type == XML_READER_TYPE_END_ELEMENT && self->currentEntityType == OSM_ENTITY_WAY) {
        finishWay(self);
        self->currentEntityType =  self->currentChangeType == OSM_CHANGE_NONE ? OSM_ENTITY_MAP :OSM_ENTITY_CHANGE_GROUP;
    } else {
        fprintf(stderr, "INVALID STATE AT END OF RELATION: %i\n", self->currentEntityType);
    }
}

static void newRelation(OsmStreamReader* self, xmlTextReaderPtr reader) {
    if(self->newRelation) {
        char* id = (char *)xmlTextReaderGetAttribute(reader, UTF8_CAST "id");
        char* timestamp = (char *)xmlTextReaderGetAttribute(reader, UTF8_CAST "timestamp");
        
        self->newRelation(self->target, atoosmid(id), parseTimestamp(timestamp));
        
        free(id);
        free(timestamp);
    }
}

static void processRelation(OsmStreamReader* self, xmlTextReaderPtr reader) {
    xmlReaderTypes type = xmlTextReaderNodeType(reader);
    if(type == XML_READER_TYPE_ELEMENT && (self->currentEntityType == OSM_ENTITY_MAP || self->currentEntityType == OSM_ENTITY_CHANGE_GROUP)) {
        self->currentEntityType = OSM_ENTITY_RELATION;
        newRelation(self, reader);
    } else if(type == XML_READER_TYPE_ELEMENT && self->currentEntityType == OSM_ENTITY_NODE) {
        finishNode(self);
        self->currentEntityType = OSM_ENTITY_RELATION;
        newRelation(self, reader);
    } else if(type == XML_READER_TYPE_ELEMENT && self->currentEntityType == OSM_ENTITY_WAY) {
        finishWay(self);
        self->currentEntityType = OSM_ENTITY_RELATION;
        newRelation(self, reader);
    } else if(type == XML_READER_TYPE_ELEMENT && self->currentEntityType == OSM_ENTITY_RELATION) {
        finishRelation(self);
        newRelation(self, reader);
    } else if (type == XML_READER_TYPE_END_ELEMENT && self->currentEntityType == OSM_ENTITY_RELATION) {
        finishRelation(self);
        self->currentEntityType = self->currentChangeType == OSM_CHANGE_NONE ? OSM_ENTITY_MAP :OSM_ENTITY_CHANGE_GROUP;
    } else {
        fprintf(stderr, "INVALID STATE AT END OF RELATION: %i\n", self->currentEntityType);
    }
}

OsmEntityType string2relationMemberType(UTF8* typeString) {
    if(utf8equal(typeString, UTF8_CAST "way")) {
        return OSM_ENTITY_WAY;
    }
    if(utf8equal(typeString, UTF8_CAST "node")) {
        return OSM_ENTITY_NODE;
    }
    if(utf8equal(typeString, UTF8_CAST "relation")) {
        return OSM_ENTITY_RELATION;
    }
    return OSM_ENTITY_NONE;
}

const char* relationMemberType2String(OsmEntityType type) {
    switch (type) {
        case OSM_ENTITY_WAY:
            return "way";
        case OSM_ENTITY_NODE:
            return "node";
        case OSM_ENTITY_RELATION:
            return "relation";
        default:
            return NULL;
    }
}

static void processRelationMember(OsmStreamReader* self, xmlTextReaderPtr reader) {
    if(self->currentEntityType != OSM_ENTITY_RELATION) {
        fprintf(stderr, "RELATION MEMBER IN INVALID STATE: %i\n", self->currentEntityType);
    } else {
        if(self->newRelationMember) {
            UTF8* role = xmlTextReaderGetAttribute(reader, UTF8_CAST "role");

            char* refString = (char *)xmlTextReaderGetAttribute(reader, UTF8_CAST "ref");
            OsmId ref = atoosmid(refString);
            free(refString);
            UTF8* typeString = xmlTextReaderGetAttribute(reader, UTF8_CAST "type");
            OsmEntityType type = string2relationMemberType(typeString);
            
            self->newRelationMember(self->target, ref, type, role);

            free(typeString);
            free(role);
        }
    }
}

static void processChangeGroup(OsmStreamReader* self, xmlTextReaderPtr reader, OsmChangeType changeType) {
    xmlReaderTypes type = xmlTextReaderNodeType(reader);
    if(type == XML_READER_TYPE_ELEMENT && self->currentEntityType == OSM_ENTITY_CHANGE_SET) {
        self->currentEntityType = OSM_ENTITY_CHANGE_GROUP;
        self->currentChangeType = changeType;
    } else if(type == XML_READER_TYPE_ELEMENT && self->currentEntityType == OSM_ENTITY_CHANGE_GROUP) {
        self->currentChangeType = changeType;
    } else if(type == XML_READER_TYPE_END_ELEMENT && self->currentEntityType == OSM_ENTITY_NODE && changeType == self->currentChangeType) {
        finishNode(self);
        self->currentChangeType = OSM_CHANGE_CREATE;
        self->currentEntityType = OSM_ENTITY_CHANGE_SET;
    } else if(type == XML_READER_TYPE_END_ELEMENT && self->currentEntityType == OSM_ENTITY_WAY && changeType == self->currentChangeType) {
        finishWay(self);
        self->currentChangeType = OSM_CHANGE_CREATE;
        self->currentEntityType = OSM_ENTITY_CHANGE_SET;
    } else if(type == XML_READER_TYPE_END_ELEMENT && self->currentEntityType == OSM_ENTITY_RELATION && changeType == self->currentChangeType) {
        finishRelation(self);
        self->currentChangeType = OSM_CHANGE_CREATE;
        self->currentEntityType = OSM_ENTITY_CHANGE_SET;
    } else if (type == XML_READER_TYPE_END_ELEMENT && self->currentEntityType == OSM_ENTITY_CHANGE_GROUP && changeType == self->currentChangeType) {
        self->currentChangeType = OSM_CHANGE_CREATE;
        self->currentEntityType = OSM_ENTITY_CHANGE_SET;
    } else {
        fprintf(stderr, "INVALID STATE AT THE END OF CHANGE GROUP: %i\n", self->currentEntityType);
    }
}

static void processXmlNode(OsmStreamReader* self, xmlTextReaderPtr reader) {
    const UTF8 *name;
    name = xmlTextReaderConstName(reader);
    if (name == NULL)
        name = UTF8_CAST "--";
    if(utf8equal(name, UTF8_CAST "node")) {
        processNode(self, reader);
    } else if(utf8equal(name, UTF8_CAST "way")) {
        processWay(self, reader);
    } else if(utf8equal(name, UTF8_CAST "relation")) {
        processRelation(self, reader);
    } else if(utf8equal(name, UTF8_CAST "tag")) {
        processTag(self, reader);
    } else if(utf8equal(name, UTF8_CAST "nd")) {
        processWayNode(self, reader);
    } else if(utf8equal(name, UTF8_CAST "member")) {
        processRelationMember(self, reader);
    } else if(utf8equal(name, UTF8_CAST "modify")) {
        processChangeGroup(self, reader, OSM_CHANGE_MODIFY);
    } else if(utf8equal(name, UTF8_CAST "create")) {
        processChangeGroup(self, reader, OSM_CHANGE_CREATE);
    } else if(utf8equal(name, UTF8_CAST "delete")) {
        processChangeGroup(self, reader, OSM_CHANGE_DELETE);
    } else if(utf8equal(name, UTF8_CAST "osm")) {
        self->currentEntityType = OSM_ENTITY_MAP;
    } else if(utf8equal(name, UTF8_CAST "osmChange")) {
        self->currentEntityType = OSM_ENTITY_CHANGE_SET;
    }
}

static void readOsmFromReader(OsmStreamReader* self, xmlTextReaderPtr reader) {
    LIBXML_TEST_VERSION
    int ret;
    
    if (reader != NULL) {
        ret = xmlTextReaderRead(reader);
        while (ret == 1) {
            processXmlNode(self, reader);
            ret = xmlTextReaderRead(reader);
        }
        xmlFreeTextReader(reader);
        if (ret != 0) {
            fprintf(stderr, "Failed to parse\n");
        }
    } else {
        fprintf(stderr, "Unable to open\n");
    }
}

void initOsmStreamReader(OsmStreamReader* reader, void* target) {
    reader->target = target;
    reader->newNode = NULL;
    reader->newWay = NULL;
    reader->newRelation = NULL;
    reader->newRelationMember = NULL;
    reader->newWayNode = NULL;
    reader->currentChangeType = OSM_CHANGE_NONE;
    
    reader->finishWay[OSM_CHANGE_CREATE] = NULL;
    reader->finishNode[OSM_CHANGE_CREATE] = NULL;
    reader->finishRelation[OSM_CHANGE_CREATE] = NULL;
    
    reader->finishWay[OSM_CHANGE_NONE] = NULL;
    reader->finishNode[OSM_CHANGE_NONE] = NULL;
    reader->finishRelation[OSM_CHANGE_NONE] = NULL;
    
    reader->finishWay[OSM_CHANGE_MODIFY] = NULL;
    reader->finishNode[OSM_CHANGE_MODIFY] = NULL;
    reader->finishRelation[OSM_CHANGE_MODIFY] = NULL;
    
    reader->finishWay[OSM_CHANGE_DELETE] = NULL;
    reader->finishNode[OSM_CHANGE_MODIFY] = NULL;
    reader->finishRelation[OSM_CHANGE_MODIFY] = NULL;
}

void closeOsmStreamReader(OsmStreamReader* reader) {
    xmlCleanupParser();
    xmlMemoryDump();
}

void readOsmFromGzip(OsmStreamReader* self, const char* filename) {
    readOsmFromReader(self, xmlReaderForIO((xmlInputReadCallback)gzread, (xmlInputCloseCallback)gzclose, gzopen(filename, "r"), filename, NULL, 0));
}

void readOsmFromFile(OsmStreamReader* self, const char* filename) {
    readOsmFromGzip(self, filename);
//    readOsmFromReader(self, xmlReaderForFile(filename, NULL, 0));
}

void readOsmFromStdin(OsmStreamReader* self) {
    readOsmFromReader(self, xmlReaderForFd(STDIN_FILENO, "", NULL, 0));
}

void initOsmDbReader(OsmDbReader* reader, void* target) {
    reader->restartNodes = NULL;
    reader->restartWays = NULL;
    reader->restartRelations = NULL;

    reader->nextNode = NULL;
    reader->nextWay = NULL;
    reader->nextRelation = NULL;
    
    reader->nodeWithId = NULL;
    reader->wayWithId = NULL;
    reader->relationWithId = NULL;
    
    reader->close = NULL;
    
    reader->target = target;
}

Node* nextNode(OsmDbReader* self) {
    if(self->nextNode) {
        return self->nextNode(self->target);
    }
    return NULL;
}

Way* nextWay(OsmDbReader* self) {
    if(self->nextWay) {
        return self->nextWay(self->target);
    }
    return NULL;
}

Relation* nextRelation(OsmDbReader* self) {
    if(self->nextRelation) {
        return self->nextRelation(self->target);
    }
    return NULL;   
}

void restartNodes(OsmDbReader* self) {
    if(self->restartNodes) {
        self->restartNodes(self->target);
    }
}

void restartWays(OsmDbReader* self) {
    if(self->restartWays) {
        self->restartWays(self->target);
    }
}

void restartRelations(OsmDbReader* self) {
    if(self->restartRelations) {
        self->restartRelations(self->target);
    }
}

Way* wayWithId(OsmDbReader* self, OsmId id) {
    if(self->wayWithId) {
        return self->wayWithId(self->target, id);
    }
    return NULL;
}

void closeOsmDbReader(OsmDbReader* reader) {
    if(reader->close) {
        reader->close(reader->target);
    }
}

UTF8* valueForKey(PlainTags* tags, UTF8* key) {
    for(int t=0;t<tags->count;t++) {
        if(utf8equal(tags->values[t].key, key)) {
            return tags->values[t].value;
        }
    }
    return NULL;
}
