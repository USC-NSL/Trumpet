#!/bin/bash
#ask 1
#satisfaction 2
#return 3
#add_return 4
#all received 5
#addevent 6
#del event 7
#del return 8
grep "^[0-9]*:" $1 | sed -e '/hello/d' -e 's/^\([0-9]*\):/\1,/g' -e 's/ serverdata \([0-9]*\):/\1,/g' -e "s/ poll /1, /g" -e "s/ sat/2,/g" -e "s/ poll_return/3,/g" -e "s/ add_return/4,/g" -e 's/ all_data/0,5,0,/g' -e 's/ addevent/0,6,0,/g' -e 's/ delevent/0,7,0,/g'  -e 's/ del_return/8,/g'  -e 's/ time \([0-9]*\)\(.\)/\1,\2/g' -e 's/ event \([0-9]*\)/\1,/g' -e 's/ ctime \([0-9]*\)/\1,/g' -e 's/ \([0-9]*\):\([0-9]*\)//g'
