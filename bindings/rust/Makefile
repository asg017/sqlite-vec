VERSION=$(shell cat ../../VERSION)

deps: Cargo.toml sqlite-vec.c sqlite-vec.h

Cargo.toml: ../../VERSION Cargo.toml.tmpl
		VERSION=$(VERSION) envsubst < Cargo.toml.tmpl > $@

sqlite-vec.c: ../../sqlite-vec.c
		cp $< $@

sqlite-vec.h: ../../sqlite-vec.h
		cp $< $@

.PHONY: deps
