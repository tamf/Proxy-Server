#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#define MAX_ENTRIES 100

FILE * file;
char * blacklist_entries[MAX_ENTRIES];
int num_entries;
int read_blacklist_file(char* filename);
bool is_blacklisted(char * host);
void to_lower_case(char * string);

/*
* Read each line (entry) of file into blacklist_entries and returns 0 on success, -1 on failure.
*/
int read_blacklist_file(char* filename) {
	
	// open file 
	file = fopen(filename, "r");
	if (NULL == file) {
		printf("Error opening file.\n");
		return -1;
	}
	
	// read each line (entry) into blacklist_entries
	int line_index = 0;
	char * line = (char *) malloc((MAX_ENTRIES + 3) * sizeof(char));
	int line_len;
	while (NULL != fgets(line, MAX_ENTRIES + 3, file) && line_index < MAX_ENTRIES) {
		blacklist_entries[line_index] = (char *) malloc((MAX_ENTRIES + 3) * sizeof(char));
		strcpy(blacklist_entries[line_index], line);
		
		// remove '\n' from entry
		line_len = strlen(blacklist_entries[line_index]);
		if ('\n' == *(blacklist_entries[line_index] + line_len - 1)) {
			*(blacklist_entries[line_index] + line_len - 1) = '\0';
		}
		
		// convert to lower case since we don't care about case 
		to_lower_case(blacklist_entries[line_index]);
				
		line_index++;
	}
	free(line);
	num_entries = line_index;
	
	return 0;
}

/*
* Returns true if host is blacklisted, otherwise false
*/
bool is_blacklisted(char * host) {
	char * host_copy = (char *) malloc(sizeof(char) * strlen(host));
	strcpy(host_copy, host);
	
	to_lower_case(host_copy);
	
	int i;
	for (i = 0; i < num_entries; i++) {
		if (NULL != strstr(host, blacklist_entries[i])) {
			return true;
		}
	}
	return false;
}	

/*
* Converts string to lower case
*/
void to_lower_case(char * string) {
	int i;
	for (i = 0; i < strlen(string); i++) {
		string[i] = tolower(string[i]);
	}
}




