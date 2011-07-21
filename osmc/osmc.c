/*
 *  osm2bosm.c
 *  OSMapper
 *
 *  Created by Egor Leonenko on 7.3.09.
 *  Copyright 2009 Egor Leonenko. All rights reserved.
 *
 */

#include <math.h>
#include <memory.h>
#include <stdlib.h>
#include "obm.h"
#include "olm.h"
#include "omm.h"
#include "utils.h"
#include "mapper.h"
#include "utf.h"
#include <argtable2.h>
#include <regex.h>
#include <curl/curl.h>
#include <zlib.h>
#include <time.h>
#include <stdarg.h>

static int convertOsm2Obm(const char* inputFile, const char* outputDirectory, const char* polygonsDirectory) {
 	//printf("Reading polygons...");
    osm2obm converter;
    
    
    int count;
    CountryPolygon* polygons = readPolygons(polygonsDirectory, &count, "FULL");
    //	printf("Done\n");    
    //	printf("Initialize converter...\n");
    initOsm2obmWithOutputDirectory(&converter, outputDirectory, polygons, count);
    //	printf("Done.\n");
    if(!inputFile) {
        //		printf("Converting from stdin...\n");
        convertOsm2ObmFromStdin(&converter);
    } else {
        //		printf("Converting from file...\n");
        convertOsm2ObmFromFile(&converter, inputFile);
    }
    //	printf("Done.\n");
    //	printf("Finishing...\n");
    closeOsm2obm(&converter);
    //	printf("Done.\n");
    return 0;
}

static int convertOsm2Olm(const char* inputFile, const char* outputDirectory, const char* polygonsDirectory) {
    osm2olm converter;
    
    int count;
    //    printf("Reading polygons...");
    CountryPolygon* polygons = readPolygons(polygonsDirectory, &count, "FULL");
	//printf("Done\n");
    /*    sqlite3* db;
     sqlite3_open(":memory:", &db);
     sqlite3_close(db);*/
    
	printf("Initialize converter...\n");
    initOsm2OlmWithOutputDirectory(&converter, outputDirectory, polygons, count);
	printf("Done.\n");
    if(!inputFile) {
		printf("Converting from stdin...\n");
        convertOsm2OlmFromStdin(&converter);
    } else {
		printf("Converting from file...\n");
        convertOsm2OlmFromFile(&converter, inputFile);
    }
	printf("Done.\n");
	printf("Finishing...\n");
    closeOsm2Olm(&converter);
	printf("Done.\n");
    return 0;
}

static int convertOsm2Omm(const char* inputFile, const char* host, const char* user, const char* password, const char* database, const char* polygonsDirectory) {
    osm2omm converter;
    
    int count;
    //    printf("Reading polygons...");
    CountryPolygon* polygons = readPolygons(polygonsDirectory, &count, database);
	//printf("Done\n");
    /*    sqlite3* db;
     sqlite3_open(":memory:", &db);
     sqlite3_close(db);*/
    
	printf("Initialize converter...\n");
    initOsm2Omm(&converter, host, user, password, polygons, count);
	printf("Done.\n");
    if(!inputFile) {
		printf("Converting from stdin...\n");
        convertOsm2OmmFromStdin(&converter);
    } else {
		printf("Converting from file...\n");
        convertOsm2OmmFromFile(&converter, inputFile);
    }
	printf("Done.\n");
	printf("Finishing...\n");
    closeOsm2Omm(&converter);
	printf("Done.\n");
    return 0;
}


static size_t save_changefile(void *ptr, size_t size, size_t nmemb, void *stream) {
    if(stream) {
        int written = fwrite(ptr, size, nmemb, (FILE*) stream);
        return written;
    }
    return size;
}

static struct tm addTime(struct tm start, const char* period, int value) {
    if (strcmp(period, "minute") == 0) {
        start.tm_min++;
    } else if (strcmp(period, "hourly") == 0) {
        start.tm_hour++;
    } else if (strcmp(period, "daily") == 0) {
        start.tm_mday++;
    }
    time_t time = timegm(&start);
    return *gmtime(&time);
}

static char* nextChangeFileName(OsmTimestamp* timestamp, const char* timePeriod) {
    //    time_t now; 
    //    time(&now);
    struct tm startTime = *gmtime(timestamp);
    
    /*    int year, month, day, hour = 0, minute = 0;
     sscanf(timestamp, "%04d%02d%02d%02d%02d", &year, &month, &day, &hour, &minute);
     
     startTime.tm_year = year - 1900;
     startTime.tm_mon = month - 1;
     startTime.tm_mday = day;
     startTime.tm_hour = hour;
     startTime.tm_min = minute;*/
    struct tm endTime = addTime(startTime, timePeriod, 1);
    
    char* format;
    if (strcmp(timePeriod, "minute") == 0) {
        format = "%Y%m%d%H%M";
        endTime.tm_sec = 0;
    } else if (strcmp(timePeriod, "hourly") == 0) {
        format = "%Y%m%d%H";
        endTime.tm_sec = 0;
        endTime.tm_min = 0;
    } else if (strcmp(timePeriod, "daily") == 0) {
        format = "%Y%m%d";
        endTime.tm_sec = 0;
        endTime.tm_min = 0;
        endTime.tm_hour = 0;
    }
    *timestamp = timegm(&endTime);
    char* timeStartString = calloc(sizeof(char), 15);
    char* timeEndString = calloc(sizeof(char), 15);
    strftime(timeStartString, 14, format, &startTime);
    strftime(timeEndString, 14, format, &endTime);
    
    char* result = calloc(sizeof(char), 41+12*2+strlen(timePeriod));
    sprintf(result, "http://planet.openstreetmap.org/%s/%s-%s.osc.gz", timePeriod, timeStartString, timeEndString);
    return result;
}

static int initUpdates(const char* inputFile) {
    sqlite3* db;
    if (sqlite3_open(inputFile, &db) == SQLITE_OK) {
        sqlite3_exec(db, "DROP TABLE timestamp", NULL, NULL, NULL);
        sqlite3_exec(db, "CREATE TABLE timestamp (timestamp INTEGER)", NULL, NULL, NULL);
        sqlite3_stmt* selectMaxTimestampFromNodes, *selectMaxTimestampFromWays, *selectMaxTimestampFromRelations;
        sqlite3_prepare(db, "SELECT MAX(timestamp) FROM current_nodes", -1, &selectMaxTimestampFromNodes, NULL);
        sqlite3_prepare(db, "SELECT MAX(timestamp) FROM current_ways", -1, &selectMaxTimestampFromWays, NULL);
        sqlite3_prepare(db, "SELECT MAX(timestamp) FROM current_relations", -1, &selectMaxTimestampFromRelations, NULL);
        OsmTimestamp maxTimestampForNode = 0, maxTimestampForWay = 0, maxTimestampForRelation = 0;
        if (sqlite3_step(selectMaxTimestampFromNodes) == SQLITE_ROW) {
            maxTimestampForNode = sqlite3_column_int64(selectMaxTimestampFromNodes, 0);
        }
        if (sqlite3_step(selectMaxTimestampFromWays) == SQLITE_ROW) {
            maxTimestampForWay = sqlite3_column_int64(selectMaxTimestampFromWays, 0);
        }
        if (sqlite3_step(selectMaxTimestampFromRelations) == SQLITE_ROW) {
            maxTimestampForRelation = sqlite3_column_int64(selectMaxTimestampFromRelations, 0);
        }
        sqlite3_finalize(selectMaxTimestampFromNodes);
        sqlite3_finalize(selectMaxTimestampFromWays);
        sqlite3_finalize(selectMaxTimestampFromRelations);
        
        OsmTimestamp maxTimestamp = max(maxTimestampForRelation, max(maxTimestampForNode, maxTimestampForWay));
        sqlite3_stmt* insertTimestamp;
        sqlite3_prepare(db, "INSERT INTO timestamp (timestamp) VALUES (?1)", -1, &insertTimestamp, NULL);
        sqlite3_bind_int64(insertTimestamp, 1, maxTimestamp);
        sqlite3_step(insertTimestamp);
        sqlite3_finalize(insertTimestamp);
        sqlite3_close(db);
        printf("DB timestamp: %li\n", maxTimestamp);
        return 0;
    } else {
        fprintf(stderr, "Error Opening file %s", inputFile);
        return 1;
    }
}

static int initUpdatesMysql(const char*  host, const char* user, const char* password, const char* database) {
    MYSQL mysql;
    mysql_init(&mysql);
    mysql_options(&mysql, MYSQL_SET_CHARSET_NAME, "utf8");
    mysql_real_connect(&mysql, host, user, password, database, 0, NULL, 0);
    mysql_query(&mysql, "SELECT MAX(timestamp) FROM (SELECT MAX(timestamp) timestamp FROM current_nodes UNION SELECT MAX(timestamp) FROM current_ways UNION SELECT MAX(timestamp) FROM current_relations) ts");
    MYSQL_RES* result = mysql_store_result(&mysql);
    OsmTimestamp timestamp = 0;
    if(mysql_num_rows(result)) {
        MYSQL_ROW row = mysql_fetch_row(result);
        timestamp = atol(row[0]);
    }
    mysql_free_result(result);
    mysql_query(&mysql, "DROP TABLE IF EXISTS timestamp");
    mysql_query(&mysql, "CREATE TABLE timestamp (timestamp INTEGER)");
    char* insert_query = calloc(sizeof(char), 90);
    sprintf(insert_query, "INSERT INTO timestamp (timestamp) VALUES (%li)", timestamp);
    mysql_query(&mysql, insert_query);
    mysql_close(&mysql);
    printf("DB timestamp: %li\n", timestamp);
    return 0;
}



static OsmTimestamp readTimestamp(const char* inputFile) {
    sqlite3* db;
    OsmTimestamp timestamp = 0;
    if (sqlite3_open(inputFile, &db) == SQLITE_OK) {
        sqlite3_stmt* selectTimestamp;
        sqlite3_prepare(db, "SELECT timestamp FROM timestamp", -1, &selectTimestamp, NULL);
        if (sqlite3_step(selectTimestamp) == SQLITE_ROW) {
            timestamp = sqlite3_column_int64(selectTimestamp, 0); 
        }
        sqlite3_finalize(selectTimestamp);
        sqlite3_close(db);
    }    
    return timestamp;
}

static OsmTimestamp readTimestampMysql(const char*  host, const char* user, const char* password, const char* database) {
    OsmTimestamp timestamp = 0;
    MYSQL mysql;
    mysql_init(&mysql);
    mysql_options(&mysql, MYSQL_SET_CHARSET_NAME, "utf8");
    mysql_real_connect(&mysql, host, user, password, database, 0, NULL, 0);
    mysql_query(&mysql, "SELECT timestamp FROM timestamp");
    MYSQL_RES *result = mysql_store_result(&mysql);
    if(mysql_num_rows(result)) {
        MYSQL_ROW row = mysql_fetch_row(result);
        timestamp = atol(row[0]);
    }
    mysql_free_result(result);
    mysql_close(&mysql);
    return timestamp;
}


static void writeTimestamp(const char* inputFile, OsmTimestamp timestamp) {
    sqlite3* db;
    if (sqlite3_open(inputFile, &db) == SQLITE_OK) {
        printf("Writing timestamp %li to db.\n", timestamp);
        if(sqlite3_exec(db, "DELETE FROM timestamp", NULL, NULL, NULL) == SQLITE_OK) {
            sqlite3_stmt* insertTimestamp;
            if(sqlite3_prepare(db, "INSERT INTO timestamp (timestamp) VALUES (?1)", -1, &insertTimestamp, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(insertTimestamp, 1, timestamp);
                if (sqlite3_step(insertTimestamp) == SQLITE_ERROR) {
                    fprintf(stderr, "Could not insert timestamp: %s.\n", sqlite3_errmsg(db));
                }
                sqlite3_finalize(insertTimestamp);
            } else {
                fprintf(stderr, "Error: failed to prepare statement with message '%s'.\n", sqlite3_errmsg(db));
            }
        } else {
            fprintf(stderr, "Could not clear timestamps: %s.\n", sqlite3_errmsg(db));
        }
        sqlite3_close(db);
    } else {
        fprintf(stderr, "Could not open db to write timestamp.\n");
    }
    
}

static void writeTimestampMysql(const char*  host, const char* user, const char* password, const char* database, OsmTimestamp timestamp) {
    MYSQL mysql;
    mysql_init(&mysql);
    mysql_options(&mysql, MYSQL_SET_CHARSET_NAME, "utf8");
    mysql_real_connect(&mysql, host, user, password, database, 0, NULL, 0);
    mysql_query(&mysql, "DELETE FROM timestamp");
    char* insert_query = calloc(sizeof(char), 60);
    sprintf(insert_query, "INSERT INTO timestamp (timestamp) VALUES (%li)", timestamp);
    mysql_query(&mysql, insert_query);
    mysql_close(&mysql);
}

static int convertOsd2Olm(const char* inputFile, char** changeFiles, int changeFilesCount, const char* polygonFile, char fullMemory);
static int convertOsd2Omm(const char*  host, const char* user, const char* password, const char* database, char** changeFiles, int changeFilesCount, const char* polygonFile, char fullMemory);

#define MINUTE (1)
#define HOUR (60)
#define DAY (HOUR*24)
#define SECONDS_IN_MINUTE (60)

#define MINUTE_SLOWNESS (5)
#define HOUR_SLOWNESS (2)
#define DAY_SLOWNESS (1)

#define DIFF_TYPES_COUNT 3


static int downloadFile(const char* url, const char* localName) {
    CURL *curl_handle;
    printf("Downloading file from %s to %s\n", url, localName);
    
    FILE *bodyfile;
    curl_global_init(CURL_GLOBAL_ALL);
    
    /* init the curl session */ 
    curl_handle = curl_easy_init();
    
    /* set URL to get */ 
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    
    /* no progress meter please */ 
    curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);
    
    /* send all data to this function  */ 
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, save_changefile);
    
    /* open the files */ 
    
    bodyfile = fopen(localName, "w");
    if (bodyfile == NULL) {
        curl_easy_cleanup(curl_handle);
        return -1;
    }
    
    /* we want the headers to this file handle */ 
    //curl_easy_setopt(curl_handle, CURLOPT_WRITEHEADER, NULL);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, bodyfile);
    
    /*
     * Notice here that if you want the actual data sent anywhere else but
     * stdout, you should consider using the CURLOPT_WRITEDATA option.  */ 
    
    /* get it! */ 
    if(curl_easy_perform(curl_handle) != CURLE_ABORTED_BY_CALLBACK) {
        long http_code = 0;
        curl_easy_getinfo (curl_handle, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code == 200) {
            
            /* cleanup curl stuff */ 
            curl_easy_cleanup(curl_handle);
            fclose(bodyfile);
            return 1;
        }
    }
    
    /* cleanup curl stuff */ 
    curl_easy_cleanup(curl_handle);
    fclose(bodyfile);
    fprintf(stderr, "Could not get update from server. Stop.\n");
    remove(localName);
    return 0;
}



static int updateLFromWebMysql(const char*  host, const char* user, const char* password, const char* database, const char* polygonFile, char fullMemory) {
    time_t timestamp = readTimestampMysql(host, user, password, database);
    time_t now;
    time(&now);
    struct tm local;
    memcpy(&local, gmtime(&now), sizeof(struct tm));
    now = timegm(&local);
    long differenceMinutes = (long)difftime(now, timestamp) / SECONDS_IN_MINUTE;
    //printf("Diff: %li\n", differenceMinutes);
    char* periods[DIFF_TYPES_COUNT] = {NULL, NULL, NULL};
    
    int iterations[DIFF_TYPES_COUNT] = {0,0,0};
    
    if (differenceMinutes/DAY > DAY_SLOWNESS) {
        periods[0] = "daily";
        iterations[0] = differenceMinutes / DAY - DAY_SLOWNESS;
        differenceMinutes -= iterations[0] * DAY;
    }
    
    if (differenceMinutes/HOUR > HOUR_SLOWNESS) {
        periods[1] = "hourly";
        iterations[1] = differenceMinutes / HOUR - HOUR_SLOWNESS;
        differenceMinutes -= iterations[1] * HOUR;
    } 
    
    if (differenceMinutes/MINUTE > MINUTE_SLOWNESS) {
        periods[2] = "minute";
        iterations[2] = differenceMinutes / MINUTE - MINUTE_SLOWNESS;
        differenceMinutes -= iterations[2] * MINUTE;
    } else {
        printf("DB is up to date.\n");
        remove("update.osc.gz");
        return 2;
    }
    
    int totalFiles = 0;
    for(int i=0;i<DIFF_TYPES_COUNT;i++) {
        if(iterations[i] > 0 && periods[i]) {
            totalFiles += iterations[i];
            printf("Running %i %s updates.\n", iterations[i], periods[i]);
        }
    }
    
    char** fileNames = calloc(sizeof(char*), totalFiles);
    int k=0;
    for(int i=0;i<DIFF_TYPES_COUNT;i++) {
        if(iterations[i] > 0 && periods[i]) {
            for(int j=0; j < iterations[i]; j++, k++) {
                char* url = nextChangeFileName(&timestamp, periods[i]);
                fileNames[k] = calloc(sizeof(char), 5);
                sprintf(fileNames[k], "%04i", k);
                if(!downloadFile(url, fileNames[k])) {
                    fileNames[k] = NULL;
                    printf("Error downloading from %s. Stop downloading.", url);
                    free(url);
                    free(fileNames[k]);
                    break;
                }
                free(url);
            }
        }
    }
    
    if(!convertOsd2Omm(host, user, password, database, fileNames, totalFiles, polygonFile, fullMemory)) {
        writeTimestampMysql(host, user, password, database, timestamp);
    }
    
    for(k=0; k< totalFiles; k++) {
        remove(fileNames[k]);
        free(fileNames[k]);
    }
    
    free(fileNames);
    return 0;
}


static int updateLFromWeb(const char* inputFile, const char* polygonFile, char fullMemory) {
    time_t timestamp = readTimestamp(inputFile);
    time_t now;
    time(&now);
    struct tm local;
    memcpy(&local, gmtime(&now), sizeof(struct tm));
    now = timegm(&local);
    long differenceMinutes = (long)difftime(now, timestamp) / SECONDS_IN_MINUTE;
    //printf("Diff: %li\n", differenceMinutes);
    char* periods[DIFF_TYPES_COUNT] = {NULL, NULL, NULL};
    
    int iterations[DIFF_TYPES_COUNT] = {0,0,0};
    
    if (differenceMinutes/DAY > DAY_SLOWNESS) {
        periods[0] = "daily";
        iterations[0] = differenceMinutes / DAY - DAY_SLOWNESS;
        differenceMinutes -= iterations[0] * DAY;
    }
    
    if (differenceMinutes/HOUR > HOUR_SLOWNESS) {
        periods[1] = "hourly";
        iterations[1] = differenceMinutes / HOUR - HOUR_SLOWNESS;
        differenceMinutes -= iterations[1] * HOUR;
    } 
    
    if (differenceMinutes/MINUTE > MINUTE_SLOWNESS) {
        periods[2] = "minute";
        iterations[2] = differenceMinutes / MINUTE - MINUTE_SLOWNESS;
        differenceMinutes -= iterations[2] * MINUTE;
    } else {
        printf("DB is up to date.\n");
        remove("update.osc.gz");
        return 2;
    }
    
    int totalFiles = 0;
    for(int i=0;i<DIFF_TYPES_COUNT;i++) {
        if(iterations[i] > 0 && periods[i]) {
            totalFiles += iterations[i];
            printf("Running %i %s updates.\n", iterations[i], periods[i]);
        }
    }
    
    char** fileNames = calloc(sizeof(char*), totalFiles);
    int k=0;
    for(int i=0;i<DIFF_TYPES_COUNT;i++) {
        if(iterations[i] > 0 && periods[i]) {
            for(int j=0; j < iterations[i]; j++, k++) {
                char* url = nextChangeFileName(&timestamp, periods[i]);
                fileNames[k] = calloc(sizeof(char), 5);
                sprintf(fileNames[k], "%04i", k);
                if(!downloadFile(url, fileNames[k])) {
                    fileNames[k] = NULL;
                    printf("Error downloading from %s. Stop downloading.", url);
                    free(url);
                    free(fileNames[k]);
                    break;
                }
                free(url);
            }
        }
    }
    
    if(!convertOsd2Olm(inputFile, fileNames, totalFiles, polygonFile, fullMemory)) {
        writeTimestamp(inputFile, timestamp);
    }
        
    for(k=0; k< totalFiles; k++) {
        remove(fileNames[k]);
        free(fileNames[k]);
    }
    
    free(fileNames);
    return 0;
}

static int dbTimestamp(const char* inputFile) {
    OsmTimestamp timestamp = readTimestamp(inputFile);
    struct tm time;
    memcpy(&time, gmtime(&timestamp), sizeof(struct tm));
    printf("%04i-%02i-%02iT%02i:%02i:%02iZ", time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
    return 0;
}

static int dbTimestampMysql(const char*  host, const char* user, const char* password, const char* database) {
    OsmTimestamp timestamp = readTimestampMysql(host, user, password, database);
    struct tm time;
    memcpy(&time, gmtime(&timestamp), sizeof(struct tm));
    printf("%04i-%02i-%02iT%02i:%02i:%02iZ", time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
    return 0;
}

static int runUpdateSqlite3(const char* type, const char* inputFile, const char* polygonFile, char fullMemory) {
    if (strcmp(type, "init") == 0) {
        return initUpdates(inputFile);
    }
    if (strcmp(type, "run") == 0) {
        return updateLFromWeb(inputFile, polygonFile, fullMemory);
    }
    if (strcmp(type, "timestamp") == 0) {
        return dbTimestamp(inputFile);
    }
    return 1;
}

static int runUpdateMysql(const char* type, const char*  host, const char* user, const char* password, const char* database, const char* polygonFile, char fullMemory) {
    if (strcmp(type, "init") == 0) {
        return initUpdatesMysql(host, user, password, database);
    }
    if (strcmp(type, "run") == 0) {
        return updateLFromWebMysql(host, user, password, database, polygonFile, fullMemory);
    }
    if (strcmp(type, "timestamp") == 0) {
        return dbTimestampMysql(host, user, password, database);
    }
    return 1;
}


static int convertOsd2Omm(const char*  host, const char* user, const char* password, const char* database, char** changeFiles, int changeFilesCount, const char* polygonFile, char fullMemory) {
    CountryPolygon polygon;
    if (polygonFile) {
        readPolygon(polygonFile, &polygon);
    } else {
        polygon.name = database;
        polygon.segmentsCount = 0;
    }
    osd2omm converter;
    printf("Initialize converter...\n");
    initOsd2Omm(&converter, host, user, password, &polygon, fullMemory);
	printf("Done.\n");
    if(!changeFiles) {
		printf("Converting from stdin...\n");
        convertOsd2OmmFromStdin(&converter);
    } else {
        for(int fi=0; fi < changeFilesCount; fi++) {
            if(changeFiles[fi]) {
                printf("Converting from file %s...\n", changeFiles[fi]);
                convertOsd2OmmFromFile(&converter, changeFiles[fi]);
            }
        }
    }
	printf("Done.\n");
	printf("Finishing...\n");
    closeOsd2Omm(&converter);
	printf("Done.\n");
    return 0;
}

static int convertOsd2Olm(const char* inputFile, char** changeFiles, int changeFilesCount, const char* polygonFile, char fullMemory) {
    CountryPolygon polygon;
    if (polygonFile) {
        readPolygon(polygonFile, &polygon);
    } else {
        polygon.name = inputFile;
        polygon.segmentsCount = 0;
    }
    osd2olm converter;
    printf("Initialize converter...\n");
    initOsd2Olm(&converter, NULL, &polygon, fullMemory);
	printf("Done.\n");
    if(!changeFiles) {
		printf("Converting from stdin...\n");
        convertOsd2OlmFromStdin(&converter);
    } else {
        for(int fi=0; fi < changeFilesCount; fi++) {
            if(changeFiles[fi]) {
                printf("Converting from file %s...\n", changeFiles[fi]);
                convertOsd2OlmFromFile(&converter, changeFiles[fi]);
            }
        }
    }
	printf("Done.\n");
	printf("Finishing...\n");
    closeOsd2Olm(&converter);
	printf("Done.\n");
    return 0;
}

static int convertOlm2Mapper(const char* inputFile, const char* outputDirectory) {
    MapperConverter converter;
    initMapperConverter(&converter, newOlmReader(inputFile), outputDirectory);
    convertToMapper(&converter);
    return 0;
}

static int convertOmm2Mapper(const char* host, const char* user, const char* password, const char* database, const char* outputDirectory) {
    MapperConverter converter;
    initMapperConverter(&converter, newOmmReader(host, user, password, database), outputDirectory);
    convertToMapper(&converter);
    return 0;
}


static int convertObm2Mapper(const char* inputDirectory, const char* outputDirectory, int cacheNodes) {
    //printf("Converting Binary map from %s to mapper map in")
    MapperConverter converter;
    initMapperConverter(&converter, newObmReader(inputDirectory, cacheNodes), outputDirectory);
    convertToMapper(&converter);
    return 0;
}

static void printTags(PlainTags* tags) {
    for(int t=0;t<tags->count;t++) {
        printf("    %s = %s\n", tags->values[t].key, tags->values[t].value);
    }
}

static int testReader() {
    printf("Initializing reader...\n");
    OsmDbReader* reader = newObmReader("all/belarus", 1);
    printf("Done.\nReading node...\n");
    
    Node* node;
    if(node = nextNode(reader)) {
        printf("Node %i@(%lf, %lf) readed with tags: \n", node->info.id, doubleFromCoordiante(node->info.lat), doubleFromCoordiante(node->info.lon));
        printTags(&(node->tags));
    }
    printf("Done.\nReading way...\n");
    //    long offset = findObjectOffset(&(reader.waysIndex), 25322334);
    //printf("Way@%i\n", offset);
    //  fseek(reader.waysFile, offset, SEEK_SET);
    
    Way* way;
    if(way = nextWay(reader)) {
        printf("Way %i readed with tags: \n", way->info.id);
        
        printTags(&(way->tags));
        
        printf("Nodes:\n");
        for(int n=0;n<way->wayNodes.count;n++) {
            printf("    %i\n", way->wayNodes.values[n].id);
        }
    }
    printf("Done.\nReading relation...\n");
    Relation* relation;
    if(relation = nextRelation(reader)) {
        printf("Relation %i readed with tags: \n", relation->info.id);
        
        printTags(&(relation->tags));
        printf("Members:\n");
        for(int m=0;m<relation->relationMembers.count;m++) {
            printf("    %i (%s) as %s \n", relation->relationMembers.values[m].ref, relationMemberType2String(relation->relationMembers.values[m].type), relation->relationMembers.values[m].role);
        }
    }
    closeOsmDbReader(reader);
    printf("Done.\n");
    return 0;
}

static int testUtf() {
    UTF8* someUtf8String = (unsigned char*)"testяr";
    printf("String(utf8): %s\n", someUtf8String);
    printf("Length(utf8): %i\n", utf8length(someUtf8String));
    printf("Size(utf8):   %i\n", utf8size(someUtf8String));
    UTF8* otherUtf8String = utf8dup(someUtf8String);
    printf("Dup(utf8):    %s\n", otherUtf8String);
    printf("Eq(utf8):     %i\n", utf8equal(someUtf8String, otherUtf8String));
    
    UTF16* someUtf16String = utf8to16(someUtf8String);
    printf("String(utf16): %s\n", (char*)someUtf16String);
    printf("Length(utf16): %i\n", utf16length(someUtf16String));
    printf("Size(utf16):   %i\n", utf16size(someUtf16String));
    
    
    return 0;
}

static int printHelp(int help, const char* progname, ...) {
    if(help) {        
        va_list args;
        va_start(args, progname);
        void* argtable = va_arg(args, void*);
        printf("Usage: %s", progname);
        while (argtable) {
            arg_print_syntax(stdout,argtable,"\n");
            argtable = va_arg(args, void*);
            if (argtable) {
                printf("       %s", progname);
            }
        }
        va_end(args);
        printf("This program converts Openstreet map between different format\n");
        va_start(args, progname);
        argtable = va_arg(args, void*);
        while (argtable) {
            arg_print_glossary(stdout,argtable, "      %-25s %s\n");
            argtable = va_arg(args, void*);
        }
        
        va_end(args);
        return 0;
    }
    printf("Try '%s --help' for more information.\n", progname);
    return 0;
}

static int runTest(const char* testName) {    
    
    if(strcmp(testName, "reader")==0) {
        return testReader();
    }
    
    if(strcmp(testName, "utf")==0) {
        return testUtf();
    }
    
    if(strcmp(testName, "curl") == 0) {
        time_t timestamp = readTimestamp("minsk.sqlite");
        time_t now;
        time(&now);
        struct tm local;
        struct tm ts;
        memcpy(&local, gmtime(&now), sizeof(struct tm));
        memcpy(&ts, gmtime(&timestamp), sizeof(struct tm));
        now = timegm(&local);
        
        long difference = (long)difftime(now, timestamp);
        char* format = "%Y%m%d%H%M";
        char* timeStartString = calloc(sizeof(char), 15);
        char* timeEndString = calloc(sizeof(char), 15);
        strftime(timeStartString, 14, format, &local);
        strftime(timeEndString, 14, format, &ts);
        
        printf("LOC UTC: %s\n", timeStartString);
        printf("TS  UTC: %s\n", timeEndString);
        printf("   Diff: %li s\n", difference);
        printf("   Diff: %li m\n", difference/60);
        printf("   Diff: %li h\n", difference/3600);
    }
    
    return 0;
}


int main(int argc, char* argv[]) {
    /* XML -> sqlite syntax */
    struct arg_rex* s2l = arg_rex1(NULL, NULL, "s2l", NULL, REG_ICASE, "Convert from OpenStreetMap xml file format to sqlite DB.");
    struct arg_file* input_file1 = arg_file0("i", "input", "<input>", "Path to input xml file. If not present stdin will be used.");
    struct arg_file* polygons_dir1 = arg_file0("p", "polygons", "<input>", "Path to directory with polygons files to cut regions.");
    struct arg_file* output_dir1 = arg_file1("o", "output", "<output>", "Path to directory with converted files for each polygon.");
    struct arg_end* end1 = arg_end(20);
    
    void * argtable1[] = {
        s2l, input_file1, polygons_dir1, output_dir1, end1
    };
    int nerrors1;
    
    /* XML change -> sqlite syntax */
    struct arg_rex* d2l = arg_rex1(NULL, NULL, "d2l", NULL, REG_ICASE, "Updates sqlite DB with diff.");
    struct arg_file* input_file1a = arg_file1("i", "input", "<input>", "Path to sqlite DB.");
    struct arg_file* polygon_file1a = arg_file0("p", "polygon", "<input>", "Path to file with polygon to cut diffs.");
    struct arg_file* diff_file1a = arg_filen("c", "change", "<input>", 0, 10, "Path to directory with converted files for each polygon. Or converted file if polygons is not specified.");
    struct arg_lit* fullMemory1a = arg_lit0("m", "in-memory-cache", "If to read all ids in memory.");
    struct arg_end* end1a = arg_end(20);
    
    void * argtable1a[] = {
        d2l, input_file1a, polygon_file1a, diff_file1a, fullMemory1a, end1a
    };
    int nerrors1a;
    
    
    /* XML -> mysql syntax */
    struct arg_rex* s2m = arg_rex1(NULL, NULL, "s2m", NULL, REG_ICASE, "Convert from OpenStreetMap xml file format to mysql DB.");
    struct arg_file* input_file1b = arg_file0("i", "input", "<input>", "Path to input xml file. If not present stdin will be used.");
    struct arg_file* polygons_dir1b = arg_file0("p", "polygons", "<input>", "Path to directory with polygons files to cut regions.");
    struct arg_file* host1b = arg_file1("h", "host", "<input>", "Host of Mysql server.");
    struct arg_file* user1b = arg_file1("u", "user", "<input>", "User on mysql server.");
    struct arg_file* password1b = arg_file0("w", "password", "<input>", "Password on mysql server.");
    struct arg_file* database1b = arg_file0("d", "database", "<input>", "DB name.");
    struct arg_end* end1b = arg_end(20);
    
    void * argtable1b[] = {
        s2m, input_file1b, polygons_dir1b, host1b, user1b, password1b, database1b, end1b
    };
    int nerrors1b;
    
    /* XML change -> mysql syntax */
    struct arg_rex* d2m = arg_rex1(NULL, NULL, "d2m", NULL, REG_ICASE, "Updates mysql DB with diff.");
    struct arg_file* host1c = arg_file1("h", "host", "<input>", "Host of Mysql server.");
    struct arg_file* user1c = arg_file1("u", "user", "<input>", "User on mysql server.");
    struct arg_file* password1c = arg_file0("w", "password", "<input>", "Password on mysql server.");
    struct arg_file* database1c = arg_file0("d", "database", "<input>", "DB name.");
    struct arg_file* polygon_file1c = arg_file0("p", "polygon", "<input>", "Path to file with polygon to cut diffs.");
    struct arg_file* diff_file1c = arg_filen("c", "change", "<input>", 0, 10, "Path to directory with converted files for each polygon. Or converted file if polygons is not specified.");
    struct arg_lit* fullMemory1c = arg_lit0("m", "in-memory-cache", "If to read all ids in memory.");
    struct arg_end* end1c = arg_end(20);
    
    void * argtable1c[] = {
        d2m, host1c, user1c, password1c, database1c, polygon_file1c, diff_file1c, fullMemory1c, end1c
    };
    int nerrors1c;
    
    
    /* XML -> binary syntax */
    struct arg_rex* s2b = arg_rex1(NULL, NULL, "s2b", NULL, REG_ICASE, "Convert from OpenStreetMap xml file format to binary format.");
    struct arg_file* input_file2 = arg_file0("i", "input", "<input>", "Path to input xml file. If not present stdin will be used.");
    struct arg_file* polygons_dir2 = arg_file0("p", "polygons", "<input>", "Path to directory with polygons files to cut regions.");
    struct arg_file* output_dir2 = arg_file1("o", "output", "<output>", "Path to directory with directories with converted files for each polygon. Or directory with converted files if polygons is not specified.");
    struct arg_end* end2 = arg_end(20);
    
    
    void * argtable2[] = {
        s2b, input_file2, polygons_dir2, output_dir2, end2
    };
    int nerrors2;
    
    /* bianry -> mapper syntax */
    struct arg_rex* b2m = arg_rex1(NULL, NULL, "b2m", NULL, REG_ICASE, "Convert from binary map to mapper format.");
    struct arg_file* input_dir3 = arg_file1("i", "input", "<input>", "Path to directory with binary map.");
    struct arg_lit* memory_nodes3 = arg_lit0("m", "memory-nodes", "If to read all nodes in memory.");
    struct arg_file* output_dir3 = arg_file1("o", "output", "<output>", "Path to directory with converted files.");
    struct arg_end* end3 = arg_end(20);
    
    void * argtable3[] = {
        b2m, input_dir3, memory_nodes3, output_dir3, end3
    };
    int nerrors3;
    
    /* sqlite -> mapper syntax */
    struct arg_rex* l2m = arg_rex1(NULL, NULL, "l2m", NULL, REG_ICASE, "Convert from sqlite map to mapper format.");
    struct arg_file* input_file4 = arg_file1("i", "input", "<input>", "Path to file with sqlite map.");
    struct arg_file* output_dir4 = arg_file1("o", "output", "<output>", "Path to directory with converted files.");
    struct arg_end* end4 = arg_end(20);
    
    void * argtable4[] = {
        l2m, input_file4, output_dir4, end4
    };
    int nerrors4;
    
    /* mysql -> mapper syntax */
    struct arg_rex* m2m = arg_rex1(NULL, NULL, "m2m", NULL, REG_ICASE, "Convert from mysql map to mapper format.");
    struct arg_file* host4a = arg_file1("h", "host", "<input>", "Host of Mysql server.");
    struct arg_file* user4a = arg_file1("u", "user", "<input>", "User on mysql server.");
    struct arg_file* password4a = arg_file0("w", "password", "<input>", "Password on mysql server.");
    struct arg_file* database4a = arg_file1("d", "database", "<input>", "DB name.");
    struct arg_file* output_dir4a = arg_file1("o", "output", "<output>", "Path to directory with converted files.");
    struct arg_end* end4a = arg_end(20);

    void * argtable4a[] = {
        m2m, host4a, user4a, password4a, database4a, output_dir4a, end4a
    };
    int nerrors4a;
    
    /* test syntax */
    struct arg_rex* test = arg_rex1(NULL, NULL, "test", NULL, REG_ICASE, "Run tests.");
    struct arg_rex* testTarget = arg_rex1(NULL, NULL, "utf|reader|curl", NULL, REG_ICASE | REG_EXTENDED, "What to test.");
    struct arg_end* end5 = arg_end(20);
    
    void * argtable5[] = {
        test, testTarget, end5
    };
    int nerrors5;
    
    /* help syntax */
    struct arg_lit  *help6    = arg_lit0("h","help", "print this help and exit");
    struct arg_end  *end6     = arg_end(20);
    void* argtable6[] = {help6,end6};
    int nerrors6;
    
    /* update sqlite3 syntax */
    struct arg_rex* update = arg_rex1(NULL, NULL, "update", NULL, REG_ICASE, "Run update.");
    struct arg_rex* updateTarget = arg_rex1(NULL, NULL, "init|run|timestamp", NULL, REG_ICASE | REG_EXTENDED, "What to do.");
    struct arg_file* input_file7 = arg_file1("i", "input", "<input>", "Path to db file.");
    struct arg_lit* fullMemory7 = arg_lit0("m", "in-memory-cache", "If to read all ids in memory.");
    struct arg_file* polygon_file7 = arg_file0("p", "polygon", "<input>", "Path to polygon file.");
    struct arg_end* end7 = arg_end(20);
    
    void * argtable7[] = {
        update, updateTarget, input_file7, polygon_file7, end7
    };
    int nerrors7;
    
    struct arg_rex* updateMysql = arg_rex1(NULL, NULL, "updateMysql", NULL, REG_ICASE, "Run update.");
    struct arg_rex* updateMysqlTarget = arg_rex1(NULL, NULL, "init|run|timestamp", NULL, REG_ICASE | REG_EXTENDED, "What to do.");
    struct arg_file* host8 = arg_file1("h", "host", "<input>", "Host of Mysql server.");
    struct arg_file* user8 = arg_file1("u", "user", "<input>", "User on mysql server.");
    struct arg_file* password8 = arg_file0("w", "password", "<input>", "Password on mysql server.");
    struct arg_file* database8 = arg_file1("d", "database", "<input>", "DB name.");
    struct arg_lit* fullMemory8 = arg_lit0("m", "in-memory-cache", "If to read all ids in memory.");
    struct arg_file* polygon_file8 = arg_file0("p", "polygon", "<input>", "Path to polygon file.");
    struct arg_end* end8 = arg_end(20);
    
    void * argtable8[] = {
        updateMysql, updateMysqlTarget, host8, user8, password8, database8, polygon_file8, end8
    };
    int nerrors8;
    
    
    const char* progname = argv[0];
    
    /* verify all argtable[] entries were allocated sucessfully */
    if (arg_nullcheck(argtable1)!=0 ||
        arg_nullcheck(argtable1a)!=0 ||
        arg_nullcheck(argtable1b)!=0 ||
        arg_nullcheck(argtable1c)!=0 ||
        arg_nullcheck(argtable2)!=0 ||
        arg_nullcheck(argtable3)!=0 ||
        arg_nullcheck(argtable4)!=0 ||
        arg_nullcheck(argtable4a)!=0 ||
        arg_nullcheck(argtable5)!=0 ||
        arg_nullcheck(argtable6)!=0 ||
        arg_nullcheck(argtable7)!=0 ||
        arg_nullcheck(argtable8)!=0 ) {
        
        /* NULL entries were detected, some allocations must have failed */
        
        printf("%s: insufficient memory\n",progname);
        arg_freetable(argtable1,sizeof(argtable1)/sizeof(argtable1[0]));
        arg_freetable(argtable1a,sizeof(argtable1a)/sizeof(argtable1a[0]));
        arg_freetable(argtable1b,sizeof(argtable1b)/sizeof(argtable1b[0]));
        arg_freetable(argtable1c,sizeof(argtable1c)/sizeof(argtable1c[0]));
        arg_freetable(argtable2,sizeof(argtable2)/sizeof(argtable2[0]));
        arg_freetable(argtable3,sizeof(argtable3)/sizeof(argtable3[0]));
        arg_freetable(argtable4,sizeof(argtable4)/sizeof(argtable4[0]));
        arg_freetable(argtable4a,sizeof(argtable4a)/sizeof(argtable4a[0]));
        arg_freetable(argtable5,sizeof(argtable5)/sizeof(argtable5[0]));
        arg_freetable(argtable6,sizeof(argtable6)/sizeof(argtable6[0]));
        arg_freetable(argtable7,sizeof(argtable7)/sizeof(argtable7[0]));
        arg_freetable(argtable8,sizeof(argtable8)/sizeof(argtable8[0]));
        return 1;
    }
    
    nerrors1 = arg_parse(argc, argv,argtable1);
    nerrors1a = arg_parse(argc, argv,argtable1a);
    nerrors1b = arg_parse(argc, argv,argtable1b);
    nerrors1c = arg_parse(argc, argv,argtable1c);
    nerrors2 = arg_parse(argc, argv,argtable2);
    nerrors3 = arg_parse(argc, argv,argtable3);
    nerrors4 = arg_parse(argc, argv,argtable4);
    nerrors4a = arg_parse(argc, argv,argtable4a);
    nerrors5 = arg_parse(argc, argv,argtable5);
    nerrors6 = arg_parse(argc, argv,argtable6);
    nerrors7 = arg_parse(argc, argv,argtable7);
    nerrors8 = arg_parse(argc, argv,argtable8);
    int exitcode = 1;
    /* Execute the appropriate main<n> routine for the matching command line syntax */
    /* In this example program our alternate command line syntaxes are mutually     */
    /* exclusive, so we know in advance that only one of them can be successful.    */
    if (nerrors1==0)
        exitcode = convertOsm2Olm(input_file1->count ? input_file1->filename[0] : NULL, output_dir1->filename[0], polygons_dir1->count ? polygons_dir1->filename[0] : NULL);
    else if (nerrors1a ==0)
        exitcode = convertOsd2Olm(input_file1a->filename[0], diff_file1a->count ? (char**)diff_file1a->filename : NULL, diff_file1a->count, polygon_file1a->count ? polygon_file1a->filename[0] : NULL, fullMemory1a->count);
    else if (nerrors1b ==0)
        exitcode = convertOsm2Omm(input_file1b->count ? input_file1b->filename[0] : NULL, host1b->filename[0], user1b->filename[0], password1b->filename[0], database1b->count ? database1b->filename[0] : NULL, polygons_dir1b->count ? polygons_dir1b->filename[0] : NULL);
    else if (nerrors1c ==0)
        exitcode = convertOsd2Omm(host1c->filename[0], user1c->filename[0], password1c->filename[0], database1c->filename[0], diff_file1c->count ? (char**)diff_file1c->filename : NULL, diff_file1c->count, polygon_file1c->count ? polygon_file1c->filename[0] : NULL, fullMemory1c->count);
    else if (nerrors2==0)
        exitcode = convertOsm2Obm(input_file2->count ? input_file2->filename[0] : NULL, output_dir2->filename[0], polygons_dir2->count ? polygons_dir2->filename[0] : NULL);
    else if (nerrors3==0)
        exitcode = convertObm2Mapper(input_dir3->filename[0], output_dir3->filename[0], memory_nodes3->count);
    else if (nerrors4==0)
        exitcode = convertOlm2Mapper(input_file4->filename[0], output_dir4->filename[0]);
    else if (nerrors4a==0)
        exitcode = convertOmm2Mapper(host4a->filename[0], user4a->filename[0], password4a->filename[0], database4a->filename[0], output_dir4->filename[0]);
    else if (nerrors5==0)
        exitcode = runTest(testTarget->sval[0]);
    else if (nerrors6==0)
        exitcode = printHelp(help6->count, progname, argtable1, argtable1a, argtable1b, argtable1c, argtable2, argtable3, argtable4, argtable4a, argtable5, argtable6, argtable7, argtable8, NULL);
    else if (nerrors7==0)
        exitcode = runUpdateSqlite3(updateTarget->sval[0], input_file7->filename[0], polygon_file7->count ? polygon_file7->filename[0] : NULL, fullMemory7->count);
    else if (nerrors8==0)
        exitcode = runUpdateMysql(updateMysqlTarget->sval[0], host8->filename[0], user8->filename[0], password8->filename[0], database8->filename[0], polygon_file8->count ? polygon_file8->filename[0] : NULL, fullMemory8->count);
    else {
        /* We get here if the command line matched none of the possible syntaxes */
        if (s2l->count > 0) {
            /* here the cmd1 argument was correct, so presume syntax 1 was intended target */ 
            arg_print_errors(stdout,end1,progname);
            printf("usage: %s ", progname);
            arg_print_syntax(stdout,argtable1,"\n");
        } else if (d2l->count > 0) {
            /* here the cmd1 argument was correct, so presume syntax 1 was intended target */ 
            arg_print_errors(stdout,end1a,progname);
            printf("usage: %s ", progname);
            arg_print_syntax(stdout,argtable1a,"\n");
        } else if (s2m->count > 0) {
            /* here the cmd1 argument was correct, so presume syntax 1 was intended target */ 
            arg_print_errors(stdout,end1b,progname);
            printf("usage: %s ", progname);
            arg_print_syntax(stdout,argtable1b,"\n");
        } else if (d2m->count > 0) {
            /* here the cmd1 argument was correct, so presume syntax 1 was intended target */ 
            arg_print_errors(stdout,end1c,progname);
            printf("usage: %s ", progname);
            arg_print_syntax(stdout,argtable1c,"\n");
        } else if (s2b->count > 0) {
            /* here the cmd2 argument was correct, so presume syntax 2 was intended target */ 
            arg_print_errors(stdout,end2,progname);
            printf("usage: %s ", progname);
            arg_print_syntax(stdout,argtable2,"\n");
        } else if (b2m->count > 0) {
            /* here the cmd3 argument was correct, so presume syntax 3 was intended target */ 
            arg_print_errors(stdout,end3,progname);
            printf("usage: %s ", progname);
            arg_print_syntax(stdout,argtable3,"\n");
        } else if (l2m->count > 0) {
            /* here the cmd3 argument was correct, so presume syntax 3 was intended target */ 
            arg_print_errors(stdout,end4,progname);
            printf("usage: %s ", progname);
            arg_print_syntax(stdout, argtable4,"\n");
        } else if (m2m->count > 0) {
            /* here the cmd3 argument was correct, so presume syntax 3 was intended target */ 
            arg_print_errors(stdout,end4a,progname);
            printf("usage: %s ", progname);
            arg_print_syntax(stdout, argtable4a,"\n");
        } else if (test->count > 0) {
            /* here the cmd3 argument was correct, so presume syntax 3 was intended target */ 
            arg_print_errors(stdout,end5,progname);
            printf("usage: %s ", progname);
            arg_print_syntax(stdout, argtable5,"\n");
        } else if (update->count > 0) {
            /* here the cmd3 argument was correct, so presume syntax 3 was intended target */ 
            arg_print_errors(stdout,end7,progname);
            printf("usage: %s ", progname);
            arg_print_syntax(stdout, argtable7,"\n");
        } else if (updateMysql->count > 0) {
            /* here the cmd3 argument was correct, so presume syntax 3 was intended target */ 
            arg_print_errors(stdout,end8,progname);
            printf("usage: %s ", progname);
            arg_print_syntax(stdout, argtable8,"\n");
        } else {
            /* no correct cmd literals were given, so we cant presume which syntax was intended */
            printf("%s: missing <s2b|s2l|s2m|d2l|d2m|b2l|b2m|l2m|test|update|updateMysql|-h> command.\n",progname); 
            printf("usage  1: %s ", progname);  arg_print_syntax(stdout,argtable1,"\n");
            printf("usage  2: %s ", progname);  arg_print_syntax(stdout,argtable1a,"\n");
            printf("usage  3: %s ", progname);  arg_print_syntax(stdout,argtable1b,"\n");
            printf("usage  4: %s ", progname);  arg_print_syntax(stdout,argtable1c,"\n");
            printf("usage  5: %s ", progname);  arg_print_syntax(stdout,argtable2,"\n");
            printf("usage  6: %s ", progname);  arg_print_syntax(stdout,argtable3,"\n");
            printf("usage  7: %s",  progname);  arg_print_syntax(stdout,argtable4,"\n");
            printf("usage  8: %s",  progname);  arg_print_syntax(stdout,argtable4a,"\n");
            printf("usage  9: %s",  progname);  arg_print_syntax(stdout,argtable5,"\n");
            printf("usage 10: %s",  progname);  arg_print_syntax(stdout,argtable7,"\n");
            printf("usage 11: %s",  progname);  arg_print_syntax(stdout,argtable8,"\n");
            printf("usage 12: %s",  progname);  arg_print_syntax(stdout,argtable6,"\n");
        }
    }
    
    
    /* deallocate each non-null entry in each argtable */
    arg_freetable(argtable1,sizeof(argtable1)/sizeof(argtable1[0]));
    arg_freetable(argtable1a,sizeof(argtable1a)/sizeof(argtable1a[0]));
    arg_freetable(argtable1b,sizeof(argtable1b)/sizeof(argtable1b[0]));
    arg_freetable(argtable1c,sizeof(argtable1c)/sizeof(argtable1c[0]));
    arg_freetable(argtable2,sizeof(argtable2)/sizeof(argtable2[0]));
    arg_freetable(argtable3,sizeof(argtable3)/sizeof(argtable3[0]));
    arg_freetable(argtable4,sizeof(argtable4)/sizeof(argtable4[0]));
    arg_freetable(argtable4a,sizeof(argtable4a)/sizeof(argtable4a[0]));
    arg_freetable(argtable5,sizeof(argtable5)/sizeof(argtable5[0]));
    arg_freetable(argtable6,sizeof(argtable6)/sizeof(argtable6[0]));
    arg_freetable(argtable7,sizeof(argtable7)/sizeof(argtable7[0]));
    arg_freetable(argtable8,sizeof(argtable8)/sizeof(argtable8[0]));
    
    return exitcode;
}
