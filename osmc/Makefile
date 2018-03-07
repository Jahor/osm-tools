#Make osmc

LIB_SRCS = 2DTree.c MapperArea.c MapperTypes.c mapper.c osm.c 4DTree.c MapperAttribute.c MapperWay.c obm.c utf.c CountryPolygon.c MapperPoint.c collections.c olm.c utils.c SimpleStringIndex.c Tree16.c omm.c
LIB_SRCS_DIST = 2DTree.c MapperArea.c MapperTypes.c mapper.c omm.c osm.c 4DTree.c MapperAttribute.c MapperWay.c obm.c utf.c CountryPolygon.c MapperPoint.c collections.c olm.c utils.c Classes/SimpleStringIndex.c Classes/Tree16.c
SRCS = $(LIB_SRCS) osmc.c
HEADERS = $(LIB_SRCS, .c=.h)

CFLAGS = `xml2-config --cflags` `pkg-config --cflags mysqlclient`  `pkg-config --cflags argtable2` `pkg-config --cflags libcurl` `pkg-config --cflags sqlite3` -I./ -fms-extensions
LDFLAGS = `xml2-config --libs` `pkg-config --libs mysqlclient` `pkg-config --libs argtable2` `pkg-config --libs libcurl` `pkg-config --libs sqlite3`

osmc: $(SRCS) $(HEADERS)
		$(CC) $(CFLAGS) $(LDFLAGS) -std=c99 -o osmc $(SRCS)

dist: $(LIB_SRCS_DIST) Classes/osmc.c $(HEADERS,.c=.h)
	rm osmc-src.zip
	mkdir dist
	cp 2DTree.c dist/
	cp MapperArea.c dist/
	cp MapperTypes.c dist/
	cp mapper.c dist/
	cp osm.c dist/
	cp 4DTree.c dist/
	cp MapperAttribute.c dist/
	cp MapperWay.c dist/
	cp obm.c dist/
	cp utf.c dist/
	cp CountryPolygon.c dist/
	cp MapperPoint.c dist/
	cp collections.c dist/
	cp olm.c dist/
	cp utils.c dist/
	cp Classes/SimpleStringIndex.c dist/
	cp Classes/Tree16.c dist/Tree16.c
	cp Classes/osmc.c dist/
	cp Makefile dist/
	cp 2DTree.h dist/
	cp MapperArea.h dist/
	cp MapperTypes.h dist/
	cp mapper.h dist/
	cp osm.h dist/
	cp 4DTree.h dist/
	cp MapperAttribute.h dist/
	cp MapperWay.h dist/
	cp obm.h dist/
	cp omm.c dist/
	cp omm.h dist/
	cp utf.h dist/
	cp CountryPolygon.h dist/
	cp MapperPoint.h dist/
	cp collections.h dist/
	cp olm.h dist/
	cp utils.h dist/
	cp Classes/SimpleStringIndex.h dist/
	cp Classes/Tree16.h dist/
	zip osmc-src.zip dist/*
	-rm -r dist

clean:    
	-rm -r dist
	-rm osmc