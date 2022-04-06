#!/bin/sh

URL="https://raw.githubusercontent.com/acpica/acpica/R03_31_22/source/include"
curl "$URL/platform/{acenv,acgcc,aclinux}.h" -o \#1.h "$URL/{actypes,actbl,actbl1,actbl2,actbl3}.h" -o \#1.h
