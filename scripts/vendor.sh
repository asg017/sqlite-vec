#!/bin/bash
mkdir -p vendor
curl -o sqlite-amalgamation.zip https://www.sqlite.org/2026/sqlite-amalgamation-3530200.zip
unzip sqlite-amalgamation.zip
mv sqlite-amalgamation-3530200/* vendor/
rmdir sqlite-amalgamation-3530200
rm sqlite-amalgamation.zip
