#include "matcher.h"
#include "util.h"

bool matcherlist_head_finish(void * data, void * aux __attribute((unused)));
bool matcherlist_head_flowequal(void * data1, void * data2, void * aux __attribute__((unused)));
bool matcherlist_head_equal(void * data1, void * data2, void * aux __attribute__((unused)));
struct table * findtable(struct matcher * m, struct flow * mask);

void table_maskinit(void * m, void * data, void * aux);
void table_finish(struct table * tbl);
bool table_add(struct table * tbl, struct flow * f, void * data);
bool table_remove(struct table * tbl, struct flow * f, void * data, matcher_dataequal_func func, void ** removed);
bool table_finish2(void * data, void * aux);
bool table_maskequal(void * data1, void * data2, void * aux);

struct matcherlist_entry{
        struct matcherlist_entry * next;
        void * data;
};

struct matcherlist_head{
        struct flow f;
        hashmap_elem e;
	struct matcherlist_entry mle;
};


bool matcherlist_head_finish(void * data, void * aux __attribute((unused))){
	struct matcherlist_head * mlh = (struct matcherlist_head *) data;
	struct matcherlist_entry * mle, * mle2;
	for (mle = mlh->mle.next; mle != NULL; mle = mle2){
		mle2 = mle->next;
		FREE(mle);
	}
	return true;
}

bool matcherlist_head_flowequal(void * data1, void * data2, void * aux __attribute__((unused))){
        struct flow * f = (struct flow *) data1;
        struct matcherlist_head * mlh = (struct matcherlist_head *) data2;
        return flow_equal(f, &mlh->f);
}

bool matcherlist_head_equal(void * data1, void * data2, void * aux __attribute__((unused))){
        struct matcherlist_head * mlh1 = (struct matcherlist_head *) data1;
        struct matcherlist_head * mlh2 = (struct matcherlist_head *) data2;
        return flow_equal(&mlh1->f, &mlh2->f);
}

static inline int matcher_tablematch(struct table * tbl, struct flow * f2, void ** dataarr, uint16_t * size){
	struct flow f;
	uint16_t i = 0;
	flow_mask(&f, f2, &tbl->mask);
	struct matcherlist_head * mlh = hashmap_get2(tbl->map, &f, flow_hash(&f), matcherlist_head_flowequal, NULL);
	if (mlh != NULL){
		struct matcherlist_entry * mle;
		for (mle = &mlh->mle; mle != NULL; mle = mle->next){
			if (unlikely(i >= *size)){
				break;
			}
			*(dataarr + i) = mle->data;
			i++;
		}
	}
	return i;
}

void matcher_matchmask(struct matcher * m, struct flow * f2, struct flow * mask, void ** dataarr, uint16_t * size){
	struct table * tbl = findtable(m, mask);
	if (tbl == NULL){
		*size = 0;
		return;
	}

	*size = matcher_tablematch(tbl, f2, dataarr, size);
}

void matcher_match(struct matcher * m, struct flow * f2, void ** dataarr, uint16_t * size){
	//go through tables
	struct table * tbl;
	uint16_t s = *size;
	for (tbl = m->tables; tbl != NULL; tbl = tbl->next){
		*size -= matcher_tablematch(tbl, f2, dataarr, size);	
	}

	*size = s - *size;
}

inline struct table * findtable(struct matcher * m, struct flow * mask){
	return (struct table *) hashmap_get2(m->masktable, mask, flow_hash(mask), table_maskequal, NULL);
}

//it may add duplicates
bool matcher_add (struct matcher * m, struct flow * f, struct flow * mask, void * data){
	if (data == NULL){
		fprintf(stderr, "Matcher cannot add null data\n"); 
		return false;
	}
	//find the table
        struct table * tbl2 = findtable(m, mask);
        if (tbl2 == NULL){ //add table
		tbl2 = hashmap_add2(m->masktable, mask, flow_hash(mask), table_maskequal, table_maskinit, NULL);
               // tbl2 = table_init(mask);
                if (m->tables == NULL){
                        m->tables = tbl2;
                }else{
                        struct table * tbl;
			for (tbl = m->tables; tbl->next != NULL; tbl = tbl->next);
                        tbl->next = tbl2;
                }
        }
	return table_add(tbl2, f, data);
}

//deletes only one if it finds equal
bool matcher_remove(struct matcher * m, struct flow * f, struct flow * mask, void * data, matcher_dataequal_func func, void ** removed){
	struct table * tbl = findtable(m, mask);
        if (tbl != NULL){
                if (table_remove(tbl, f, data, func, removed)){
			return true;
                }else{
                	fprintf(stderr, "Matcher: Entry not found in the table to be removed");
		}
        }else{
                fprintf(stderr, "Matcher: Table not found to be removed");
        }
	return false;
}

struct matcher * matcher_init(void){
	struct matcher * m = (struct matcher *)MALLOC (sizeof(struct matcher));
	uint16_t entry_size = entry_size_64(sizeof(struct table));
	m->masktable = hashmap_init(1<<10, 1<<10, entry_size, offsetof(struct table, e), NULL);
	m->tables = NULL;
	return m;
}

void matcher_finish(struct matcher * m){
	hashmap_apply(m->masktable, table_finish2, NULL);
	hashmap_finish(m->masktable);
/*	struct table * tbl;
	struct table * tbl2;
	int i = 0;
	for (tbl = m->tables; tbl != NULL; tbl = tbl2){
		tbl2 = tbl->next;
		table_finish(tbl);
		i++;
	}*/
	FREE(m);
}


///////////////////////////////// TABLE /////////////////////////

void table_maskinit(void * m, void * data, void * aux __attribute__((unused))){
	struct flow * mask = (struct flow *) m;
	struct table * tbl = (struct table *) data;
	flow_fill(&tbl->mask, mask);
	
	uint16_t entry_size = entry_size_64(sizeof(struct matcherlist_head));
	tbl->map = hashmap_init(1<<14, 1<<13, entry_size, offsetof(struct matcherlist_head, e), NULL);
	tbl->next = NULL;
}

bool table_finish2(void * data, void * aux __attribute__((unused))){
	table_finish((struct table*)data);
	return true;
}

inline void table_finish(struct table * tbl){
	hashmap_apply(tbl->map, matcherlist_head_finish, NULL);
	hashmap_finish(tbl->map);
	FREE(tbl);
}

bool table_maskequal(void * data1, void * data2, void * aux __attribute__((unused))){
	return flow_equal((struct flow *) data1, &((struct table *) data2)->mask);
}

bool table_add(struct table * tbl, struct flow * f, void * data){
	struct flow f2;
	flow_mask(&f2, f, &tbl->mask);
	struct matcherlist_head * mlh = hashmap_get2(tbl->map, &f2, flow_hash(&f2), matcherlist_head_flowequal, NULL);
	if (mlh == NULL){
		struct matcherlist_head mlh2;
		mlh = &mlh2;
		flow_fill(&mlh->f, &f2);
		mlh->mle.data = data;
		mlh->mle.next = NULL;
		hashmap_add2(tbl->map, mlh, flow_hash(&f2), matcherlist_head_equal, NULL, NULL);
		return false;
	}else{
		struct matcherlist_entry * mle2 = (struct matcherlist_entry *) MALLOC(sizeof(struct matcherlist_entry));
		mle2->next = NULL;
		mle2->data = data;

		struct matcherlist_entry * mle;
		for (mle = &mlh->mle; mle->next != NULL; mle = mle->next);

		mle->next = mle2;
		return true;
	}
}

bool table_remove(struct table * tbl, struct flow * f, void * data, matcher_dataequal_func func, void ** removed){
	struct flow f2;
	flow_mask(&f2, f, &tbl->mask);
	struct matcherlist_head * mlh = hashmap_get2(tbl->map, &f2, flow_hash(&f2), matcherlist_head_flowequal, NULL);
	if (mlh != NULL){
		if (func(mlh->mle.data, data)){
			*removed = mlh->mle.data;
			if (mlh->mle.next == NULL){
				hashmap_remove(tbl->map, mlh);
			}else{
				struct matcherlist_entry * mle2 = mlh->mle.next; //copy data of next and remove it
				mlh->mle.data = mle2->data;
				mlh->mle.next = mle2->next;
				FREE(mle2);
			}
			return true;
		}else{
			struct matcherlist_entry * mle;
			struct matcherlist_entry * last_mle = &mlh->mle;
			for (mle = last_mle->next; mle != NULL; last_mle = mle, mle = mle->next){
				if (func(mle->data, data)){
					last_mle->next = mle->next;
					*removed = mle->data;
					FREE(mle);
					return true;
				}
			}
		}

		fprintf(stderr, "No matching data is found in the table entry to be removed");
	}else{
		fprintf(stderr, "No data is found in the table to be removed");
	}
	return false;
}
