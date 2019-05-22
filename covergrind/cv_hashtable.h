/*
 * Hash table interface specifications
 *
 */  

#ifndef cv_hashtable
#define cv_hashtable

typedef struct _entry {
	struct _entry *next;    
   	Addr   address;
   	UChar* function_name;
   	Int   count;
} entry;

void insert_entry_HT (VgHashTable* table, Addr key, HChar* function_name);

void print_HT (VgHashTable* table);

#endif