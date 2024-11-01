dist/.stammp:
	mkdir -p dist
	touch $@

dist/sqlite-amalgamation-3310100/.stamp: dist/.stammp
	rm -rf dist/sqlite-amalgamation-3310100/ || true
	curl -q -o sqlite-amalgamation-3310100.zip https://www.sqlite.org/2020/sqlite-amalgamation-3310100.zip
	unzip -d dist/ sqlite-amalgamation-3310100.zip
	rm sqlite-amalgamation-3310100.zip
	touch $@

dist/t3310100: demo.c dist/sqlite-amalgamation-3310100/.stamp ../../sqlite-vec.c
	gcc \
		-lm \
		-DSQLITE_CORE -DSQLITE_ENABLE_JSON1 \
		-I dist/sqlite-amalgamation-3310100 \
		-I ../../ \
		$< dist/sqlite-amalgamation-3310100/sqlite3.c \
		../../sqlite-vec.c \
		-o $@

dist/t3310100-threadsafe: demo.c dist/sqlite-amalgamation-3310100/.stamp ../../sqlite-vec.c
	gcc \
		-lm \
		-DSQLITE_CORE -DSQLITE_ENABLE_JSON1 -DSQLITE_THREADSAFE=0 \
		-I dist/sqlite-amalgamation-3310100 \
		-I ../../ \
		$< dist/sqlite-amalgamation-3310100/sqlite3.c \
		../../sqlite-vec.c \
		-o $@

test: dist/t3310100 dist/t3310100-threadsafe
	./dist/t3310100
	./dist/t3310100-threadsafe

clean:
	rm -rf dist/
