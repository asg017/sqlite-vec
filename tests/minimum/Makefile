#curl -q -o sqlite-amalgamation-3310100.zip https://www.sqlite.org/2020/sqlite-amalgamation-3310100.zip
#unzip https://www.sqlite.org/2020/sqlite-amalgamation-3310100.zip

dist/:
	mkdir -p dist
	touch $@

dist/sqlite-amalgamation-3310100: dist/
	rm -rf sqlite-amalgamation-3310100/ || true
	curl -q -o sqlite-amalgamation-3310100.zip https://www.sqlite.org/2020/sqlite-amalgamation-3310100.zip
	unzip -d dist/ sqlite-amalgamation-3310100.zip
	rm sqlite-amalgamation-3310100.zip
	touch $@

dist/t3310100: demo.c dist/sqlite-amalgamation-3310100
	gcc \
		-DSQLITE_CORE \
		-I dist/sqlite-amalgamation-3310100 \
		-I ../../ \
		$< dist/sqlite-amalgamation-3310100/sqlite3.c \
		../../sqlite-vec.c \
		-o $@

test:
	make dist/t3310100
	./dist/t3310100
