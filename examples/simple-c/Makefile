demo: demo.c
	gcc \
		-O3 -DSQLITE_CORE \
		-I../../ -I../../vendor \
		demo.c ../../sqlite-vec.c ../../vendor/sqlite3.c \
		-o $@
