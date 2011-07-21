/*
 *  MapperAttribute.c
 *  OSMapper
 *
 *  Created by Egor Leonenko on 12.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#include "MapperAttribute.h"
#include <stdlib.h>
#include <libxml/xmlstring.h>
#include <math.h>

CollectionImplGeneric(MapperAttribute, MapperAttributes, 10)
CollectionImplGeneric(MapperPartAttribute, MapperPartAttributes, 10)


static void addMapperAtribute(MapperAttributes* attributes, int key, UTF8* value8) {
    UTF16* value = utf8to16(value8);
    int tagSlots = ceil((double)utf16length(value)/ATTRIBUTE_VALUE_LENGTH);
//    printf("Check for available place..\n");
    ensureMapperAttributesCapacityForNNewElements(attributes, tagSlots);
//    printf("Done. New capacity: %i\n", attributes->capacity);
    int index = attributes->count; 
    int size = utf16size(value);
    int c = 0;
    for(int i = 0; i < size; i++, c++) {
        if(c == ATTRIBUTE_VALUE_LENGTH - 2) {
            attributes->values[index].value[c] = 0;
            index++;
            c = -1;
            i--;
        } else {
            if (c == 0) {
                attributes->values[index].key = i == 0 ? key : ATTRIBUTE_CONTINUATION;
            }
            attributes->values[index].value[c] = value[i];
        }
    }
    free(value);
    attributes->values[index].value[c] = 0;
    attributes->count = index + 1;
}

void mapperAttributesFromTags(MapperAttributes* attributes, PlainTags* tags, SimpleStringIndex* keysIndex) {
  //  printf("Convert %i tags\n", tags->count);
    for(int t = 0; t < tags->count; t++) {
        addMapperAtribute(attributes, simpleStringIndexOf(keysIndex, tags->values[t].key), tags->values[t].value);
    }
    //printf("Done\n");
}


static void addMapperPartAtribute(MapperPartAttributes* attributes, OsmId partId, int key, UTF8* value8) {
    //printf("Add tag %i = %s\n", key, value8);
    UTF16* value = utf8to16(value8);
    int tagSlots = ceil((double)utf16length(value)/ATTRIBUTE_VALUE_LENGTH);
    ensureMapperPartAttributesCapacityForNNewElements(attributes, tagSlots);
    //printf("Capacity: %i\n", attributes->capacity);
    int index = attributes->count; 
    int size = utf16size(value);
    int c = 0;
    for(int i = 0; i < size; i++, c++) {
        if(c == ATTRIBUTE_VALUE_LENGTH - 2) {
            attributes->values[index].attribute.value[c] = 0;
            index++;
            c = -1;
            i--;
        } else {
            if (c == 0) {
                attributes->values[index].attribute.key = i == 0 ? key : ATTRIBUTE_CONTINUATION;
                attributes->values[index].segmentId = partId;
            }
            attributes->values[index].attribute.value[c] = value[i];
        }
    }
    free(value);
    attributes->values[index].attribute.value[c] = 0;
    attributes->count = index + 1;
}


void mapperPartAttributesFromTags(MapperPartAttributes* attributes, OsmId partId, PlainTags* tags, SimpleStringIndex* keysIndex) {
    for(int t = 0; t < tags->count; t++) {
        int k = simpleStringIndexOf(keysIndex, tags->values[t].key);
        //printf("Add tag for %i\n", k);
        addMapperPartAtribute(attributes, partId, k, tags->values[t].value);
    }
}
