#!/bin/bash

python bench.py -n gist -i ../../gist/gist_base.fvecs -q ../../gist/gist_query.fvecs --sample 750000 --qsample 200 -k 20 -x $1
