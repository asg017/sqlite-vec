#!/bin/bash

python bench.py -n sift1m -i ../../sift/sift_base.fvecs -q ../../sift/sift_query.fvecs --qsample 100 -k 20 -x $1
