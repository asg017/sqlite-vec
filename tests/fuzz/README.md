```
ASAN_OPTIONS=detect_leaks=1 ./targets/vec0_create \
  -dict=./vec0-create.dict -max_total_time=5 \
  ./corpus/vec0-create
```

```
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
export LDFLAGS="-L/opt/homebrew/opt/llvm/lib"
export CPPFLAGS="-I/opt/homebrew/opt/llvm/include"


LDFLAGS="-L/opt/homebrew/opt/llvm/lib/c++ -Wl,-rpath,/opt/homebrew/opt/llvm/lib/c++"
```
