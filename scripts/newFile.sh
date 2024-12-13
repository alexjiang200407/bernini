#!/usr/bin/env bash

for file in $@; do
	if ! test -f "src/$file.cpp"
	then
		touch "src/$file.cpp"
	fi
	if ! test -f "include/$file.h"
	then
		touch "include/$file.h"
		echo "#pragma once" >> "include/$file.h"
	fi
done