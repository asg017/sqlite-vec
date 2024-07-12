
VERSION=0.0.1-alpha.27

demo: demo.go sqlite3.vec.wasm
	go build -o $@ $<

sqlite3.vec.wasm:
	curl -L -o $@ https://github.com/asg017/sqlite-vec/releases/download/v$(VERSION)/sqlite-vec-$(VERSION)-ncruces-go.wasm

clean:
	rm demo sqlite3.vec.wasm

.PHONY: clean
