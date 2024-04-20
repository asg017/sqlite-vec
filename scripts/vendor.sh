#!/bin/bash
mkdir -p vendor
curl -o sqlite-amalgamation.zip https://www.sqlite.org/2024/sqlite-amalgamation-3450300.zip
unzip -d
unzip sqlite-amalgamation.zip
mv sqlite-amalgamation-3450300/* vendor/
rmdir sqlite-amalgamation-3450300
rm sqlite-amalgamation.zip
