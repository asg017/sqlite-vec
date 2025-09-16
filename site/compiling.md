<script setup>
import { data } from './project.data.ts';
</script>

# Compiling `sqlite-vec`

`sqlite-vec` is is easy to compile yourself! It's a single C file with no dependencies, so the process should be straightforward.

## From Source

To compile `sqlite-vec` as a loadable SQLite extension, you can `git clone` the source repository and run the following commands:

```bash
git clone https://github.com/asg017/sqlite-vec
cd sqlite-vec
./scripts/vendor.sh
make loadable
```

The `./scripts/vendor.sh` command will download a recent version of  [SQLite's amalgammation builds](https://www.sqlite.org/amalgamation.html), to ensure you have an up-to-date `sqlite3ext.h` available on your system. 

Then `make loadable` will generate the `sqlite-vec.h` file and a dynamically loadable library at `dist/vec.$SUFFIX`. The suffix will be `.dylib` for MacOS, `.so` for Linux, and `.dll` for Windows.


## From the amalgamation build

The "amalgamation" build of `sqlite-vec` is a `.zip` or `.tar.gz` archive with the pre-configured `sqlite-vec.c` and `sqlite-vec.h` source files. 

The amalgamation builds can be found in [`sqlite-vec` Releases](https://github.com/asg017/sqlite-vec/releases). You can also download the latest amalgamation build with this command:

```-vue
wget https://github.com/asg017/sqlite-vec/releases/download/v{{data.VERSION}}/sqlite-vec-{{data.VERSION}}-amalgamation.zip
unzip sqlite-vec-{{data.VERSION}}-amalgamation.zip
```

There will now be `sqlite-vec.c` and `sqlite-vec.h` available in your current directory. To compile it manually, follow the [official SQLite extension compilation instructions](https://www.sqlite.org/loadext.html#compiling_a_loadable_extension), which will be something like:

```bash
# Linux 
gcc -g -fPIC -shared sqlite-vec.c -o vec0.so

# MacOS
gcc -g -fPIC -dynamiclib sqlite-vec.c -o vec0.dylib

# Windows, MSVC compiler
cl sqlite-vec.c -link -dll -out:sqlite-vec.dll

# Windows, MinGW
gcc -g -shared sqlite-vec.c -o vec0.dll
```

Different platforms, compiler, or architectures may require different compilation flags.

## Compile-time options

There are a few compilation options available for `sqlite-vec`, but they're currently unstable and may change in the future. They aren't tracked with [`sqlite-vec`'s semantic versioning policy ](./versioning.md), so options may break in patch version updates.

The current compile-time flags are:

- `SQLITE_VEC_ENABLE_AVX`, enables AVX CPU instructions for some vector search operations
- `SQLITE_VEC_ENABLE_NEON`, enables NEON CPU instructions for some vector search operations
- `SQLITE_VEC_OMIT_FS`, removes some obsure SQL functions and features that use the filesystem, meant for some WASM builds where there's no available filesystem
- `SQLITE_VEC_STATIC`, meant for statically linking `sqlite-vec` 
