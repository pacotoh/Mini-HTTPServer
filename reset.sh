#!/bin/bash

`truncate -s 0 webserver.log`
`pkill web_sstt`
`gcc web_sstt.c -o web_sstt`
`./web_sstt $1 .`
