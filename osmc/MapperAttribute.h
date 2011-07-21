/*
 *  MapperAttribute.h
 *  OSMapper
 *
 *  Created by Egor Leonenko on 12.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#ifndef _MAPPER_ATTRIBUTE_H_
#define _MAPPER_ATTRIBUTE_H_

#include "MapperTypes.h"
#include "utf.h"
#include "osm.h"
#include "SimpleStringIndex.h"

#define ATTRIBUTE_VALUE_LENGTH 32

#define UNUSED_ATTRIBUTE 0
#define ATTRIBUTE_CONTINUATION 1

typedef MapperIndexedValue MapperAttributeKey;

typedef struct {
    MapperAttributeKey key;
    UTF16 value[ATTRIBUTE_VALUE_LENGTH+1]; 
} MapperAttribute;

Collection(MapperAttribute, MapperAttributes)

typedef struct {
    OsmId segmentId;
    MapperAttribute attribute;
} MapperPartAttribute;


Collection(MapperPartAttribute, MapperPartAttributes)

void mapperAttributesFromTags(MapperAttributes* attributes, PlainTags* tags, SimpleStringIndex* keysIndex);
void mapperPartAttributesFromTags(MapperPartAttributes* attributes, OsmId partId, PlainTags* tags, SimpleStringIndex* keysIndex);
#endif
