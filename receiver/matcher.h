#ifndef MATCHER_H
#define MATCHER_H 1
#include "flowentry.h"

struct table {
	struct flow mask;
	struct table * next;
	struct hashmap * map;
};


struct matcher {
	struct table * tables;
};

typedef bool (*matcher_dataequal_func)(void * data1, void * data2);

//runs func on any match
void matcher_match(struct matcher * m, struct flow * f, void ** dataarr, uint16_t * size);

//it may add duplicates
bool matcher_add (struct matcher * m, struct flow * f, struct flow * mask, void * data);

//deletes only one if it finds equal
bool matcher_remove (struct matcher * m, struct flow * f, struct flow * mask, void * data,  matcher_dataequal_func func, void ** removed);


void matcher_matchmask(struct matcher * m, struct flow * f, struct flow * mask, void ** temptable, uint16_t * num);

struct matcher * matcher_init(void);
void matcher_finish(struct matcher * m);

#endif /* matcher.h */

