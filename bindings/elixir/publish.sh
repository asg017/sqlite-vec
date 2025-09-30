makefile=$(readlink -f Makefile)
rm Makefile
cp "$makefile" Makefile

version=$(readlink -f VERSION)
rm VERSION
cp "$version" VERSION

sqlite_vec_c=$(readlink -f sqlite-vec.c)
rm sqlite-vec.c
cp "$sqlite_vec_c" sqlite-vec.c

sqlite_vec_h_tmpl=$(readlink -f sqlite-vec.h.tmpl)
rm sqlite-vec.h.tmpl
cp "$sqlite_vec_h_tmpl" sqlite-vec.h.tmpl

mix hex.publish
