VERSION=$(shell cat ../../VERSION)

deps: sqlite-vec.c sqlite-vec.h sqlite3ext.h sqlite3.h

sqlite-vec.c: ../../sqlite-vec.c
		cp $< $@
		mv sqlite-vec.c src/

sqlite-vec.h: ../../sqlite-vec.h
		cp $< $@
		mv sqlite-vec.h src/

sqlite3ext.h: ../../vendor/sqlite3ext.h
		cp $< $@
		mv sqlite3ext.h src/

sqlite3.h: ../../vendor/sqlite3.h
		cp $< $@
		mv sqlite3.h src/

.PHONY: deps
