//
//  Tree16.c
//  OSMapper
//
//  Created by Egor Leonenko on 22.2.09.
//  Copyright 2009 Egor Leonenko. All rights reserved.
//

#include "Tree16.h"
#include <math.h>
#include <stdlib.h>

void initTree16Internal(Tree16* tree, int level) {
    if(level == MAX_LEVEL) {
        for(int i=0; i< TREE_CHILDREN;i++) {
            tree->node.offsets[i] = -1;
        }
    } else {
        initTree16(tree);
    }
}

void initTree16(Tree16* tree) {
    tree->allChildrenCount = 1;
    for(int i=0; i< TREE_CHILDREN;i++) {
        tree->node.children[i] = NULL;
    }
}

int addTree16NodeInternal(Tree16* tree, int Id, int offset, int level) {
    int i = Id & MASK;
    if(level == MAX_LEVEL) {
        if(tree->node.offsets[i] == -1) {
            tree->node.offsets[i] = offset;
        }
        return 0;
    } else {
        Tree16* child = tree->node.children[i];
        int childrenCount = 0;
        if (!child) {
            tree -> node.children[i] = child = malloc(sizeof(Tree16));
            initTree16Internal(child, level + 1);
            childrenCount++;
        }
        childrenCount += addTree16NodeInternal(child, Id >> BITS_COUNT, offset, level + 1);
        tree->allChildrenCount += childrenCount;
        return childrenCount;
    }
}

int isInTree16Internal(Tree16* tree, int Id, int level) {
    int i = Id & MASK;
    if(level == MAX_LEVEL) {
        return tree->node.offsets[i] != -1;
    } else {
        Tree16* child = tree->node.children[i];
        if (!child) {
            return 0;
        } else {
            return isInTree16Internal(child, Id >> BITS_COUNT, level + 1);       
        }
    }
}

int isInTree16(Tree16* tree, int Id) {
    return isInTree16Internal(tree, Id, 0);
}

void addTree16Node(Tree16* tree, int Id, long offset) {
    addTree16NodeInternal(tree, Id, offset, 0);
}

int allChildrenCountTree16(Tree16* tree, int level) {
    return tree->allChildrenCount + 1;
    int count = 1;
    if(level < MAX_LEVEL) {
        for(int i = 0; i < TREE_CHILDREN; i++) {
            if(tree-> node.children[i]) {
                int c = allChildrenCountTree16(tree-> node.children[i], level + 1);
                //debug(@"%i@%i => %i", i, level, c);
                count += c;
            }
        }
    }
    return count;
}

void saveTree16ToFile(Tree16* tree, FILE* index, int level, long offset) {
    TreeRecord record;
    if(level < MAX_LEVEL) {
        offset += sizeof(TreeRecord);
        Tree16** children = tree -> node.children;
        long* recordNumbers = record.recordNumbers;
        for(int i = 0; i < TREE_CHILDREN; i++, children++, recordNumbers++) {
            if(*children) {
                *recordNumbers = offset;
                //int c = allChildrenCountTree16(*children, level + 1);
                //debug(@"%i@%i will be at %i (%i)", i, level, offset, c);
                
                offset += ((*children)->allChildrenCount) * sizeof(TreeRecord);
                
            } else {
                *recordNumbers = -1;
            }
        }
        fwrite(&record, sizeof(TreeRecord), 1, index);
        children = tree -> node.children;
        recordNumbers = record.recordNumbers;
        for(int i = 0; i < TREE_CHILDREN; i++, children++, recordNumbers++) {
            if(*children) {
                saveTree16ToFile(*children, index, level + 1, *recordNumbers);
            }
        }
    } else {
        for(int i = 0; i < TREE_CHILDREN; i++) {
            record.recordNumbers[i] = tree-> node.offsets[i];
        }
        fwrite(&record, sizeof(TreeRecord), 1, index);
    }
}

void freeTree16Internal(Tree16* tree, int level) {
    if(level < MAX_LEVEL) {
        Tree16** children = tree -> node.children; 
        for(int i = 0; i < TREE_CHILDREN; i++, children++) {
            if(*children) {
                freeTree16Internal(*children, level + 1);
                free(*children);
            }
        }
    }
}

void freeTree16(Tree16* tree) {
    freeTree16Internal(tree, 0);
}

void initTree16WithFile(Tree16OnFile* self, FILE* aFile) {
    self->file = aFile;
    TreeRecord froot;
    fseek(aFile, 0, SEEK_SET);
    fread(&froot, sizeof(TreeRecord), 1, aFile);
    for(int i=0; i< TREE_CHILDREN; i++) {
        if(froot.recordNumbers[i] != -1) {
            fseek(aFile, froot.recordNumbers[i], SEEK_SET);
            TreeRecord r2;
            fread(&r2, sizeof(TreeRecord), 1, aFile);
            for(int j=0; j< TREE_CHILDREN; j++) {
                fseek(aFile, r2.recordNumbers[j], SEEK_SET);
                TreeRecord r3;
                fread(&r3, sizeof(TreeRecord), 1, aFile);
                for(int k=0; k< TREE_CHILDREN; k++) {    
                    fseek(aFile, r3.recordNumbers[k], SEEK_SET);
                    TreeRecord r4;
                    fread(&r4, sizeof(TreeRecord), 1, aFile);
                    for(int l=0; l< TREE_CHILDREN; l++) {    
                        self->root[(((l<<BITS_COUNT | k)<<BITS_COUNT | j)<<BITS_COUNT | i)] = r4.recordNumbers[l];
                    }
                }
            }
        } else {
            for(int j=0; j< TREE_CHILDREN; j++) {
                for(int k=0; k< TREE_CHILDREN; k++) {
                    for(int l=0; l< TREE_CHILDREN; l++) {
                        self->root[(((l<<BITS_COUNT | k)<<BITS_COUNT | j)<<BITS_COUNT | i)] = -1;
                    }
                }
            }
        }
    }
} 

long findObjectOffsetInternal(Tree16OnFile* self, long Id, TreeRecord record,int level) {
    int i = Id & MASK;
    int recordNumber = record.recordNumbers[i];
    if(level == MAX_LEVEL) { //leaves
        return recordNumber;
    } else {
        if(recordNumber == -1) {
            return -1;
        }
        fseek(self->file, recordNumber, SEEK_SET);
        TreeRecord childRecord;
        fread(&childRecord, sizeof(TreeRecord), 1, self->file);
        return findObjectOffsetInternal(self, Id >> BITS_COUNT, childRecord, level + 1);
    }
}

long findObjectOffset(Tree16OnFile* self, long Id) {
    int i = Id & CACHE_MASK;
    if(self->root[i] == -1) {
        return -1;
    }
    fseek(self->file, self->root[i], SEEK_SET);
    TreeRecord start;
    fread(&start, sizeof(TreeRecord), 1, self->file);
    return findObjectOffsetInternal(self, Id >> CACHE_BITS_COUNT, start, CACHE_LEVEL);
}

void freeTree16WithFile(Tree16OnFile* tree) {
    fclose(tree->file);
}
