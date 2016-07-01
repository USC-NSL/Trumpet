#!/bin/bash
#send 1
#query 2
#add 3
#del 4
grep "^[0-9]*:" $1 | sed -e 's/^\([0-9]*\):/\1,/g' -e 's/ send/1,/g' -e 's/ poll/2,/g' -e 's/ add/3,0,/g' -e 's/ del/4,0,/g' -e 's/ time \([0-9]*\)/\1,/g' -e 's/ step \([0-9]*\)/\1,/g'  -e 's/ event \([0-9]*\)/\1,/g'  -e 's/ seq \([0-9]*\)/\1/g' -e '/[a-zA-Z]/d'
