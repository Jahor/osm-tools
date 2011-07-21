/*
 *  collections.h
 *  OSMapper
 *
 *  Created by Egor Leonenko on 14.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#ifndef _COLLECTIONS_H_
#define _COLLECTIONS_H_

#define CollectionWithCustomAdd(type, name)\
typedef struct { \
    type* values; \
    int count;\
    int capacity;\
} name;\
\
void init##name(name* self);\
void removeAll##name(name* self);\
void clear##name(name* self);\
void free##name(name* self);\
void ensure##name##CapacityForNNewElements(name* self, int n);

#define Collection(type, name) \
CollectionWithCustomAdd(type, name);\
void addTo##name(name* self, type element);\

#define CollectionImplMinimal(type, name, growFactor) \
void init##name(name* self) {\
    self->values = NULL;\
    self->count = 0;\
    self->capacity = 0;\
}\
void ensure##name##CapacityForNNewElements(name* self, int n) {\
    if(self->capacity < self->count + n) {\
        self->capacity = self->count + (growFactor > n ? growFactor : n);\
        self->values = realloc(self->values, sizeof(type) * self->capacity);\
    }\
}\
void clear##name(name* self) {\
    if(self->values) {\
        removeAll##name(self);\
        free(self->values);\
        self->values = NULL;\
    }\
    self->capacity = 0;\
}\
void free##name(name* self) {\
    clear##name(self);\
    free(self);\
}

#define CollectionImplCustomElementFree(type, name, growFactor) \
CollectionImplMinimal(type, name, growFactor)\
void removeAll##name(name* self) {\
    for(int i=0; i < self->count; i++) {\
        free##type(self->values + i);\
    }\
    self->count = 0;\
}

#define CollectionImplAdd(type, name)\
void addTo##name(name* self, type element) {\
ensure##name##CapacityForNNewElements(self, 1);\
self->values[self->count] = element;\
self->count++;\
}\

#define CollectionImplGeneric(type, name, growFactor) \
CollectionImplMinimal(type, name, growFactor)\
CollectionImplAdd(type, name)\
void removeAll##name(name* self) {\
    self->count = 0;\
}

#define StaticCollection(type, name) typedef struct { \
    type* values; \
    int count;\
} name;\
\
void init##name(name* self, type* values, int count);\
void free##name(name* self);

#define StaticCollectionImpl(type, name) \
void init##name(name* self, type* values, int count){\
    self->values = malloc(sizeof(type) * count);\
    memcpy(self->values, values, sizeof(type) * count);\
    self->count = count;\
}\
void free##name(name* self) {\
    free(self->values);\
    self->values = NULL;\
    self->count = 0;\
    free(self);\
}\

#define StaticCollectionImplCustomElementFree(type, name) \
void init##name(name* self, type* values, int count){\
    self->values = malloc(sizeof(type) * count);\
    memcpy(self->values, values, sizeof(type) * count);\
    self->count = count;\
}\
void free##name(name* self) {\
    for(int i=0; i < self->count; i++) {\
        free##type(self->values + i);\
    }\
    free(self->values);\
    self->values = NULL;\
    self->count = 0;\
    free(self);\
}\


#endif
