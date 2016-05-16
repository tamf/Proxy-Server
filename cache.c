#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#define BUFFER_SIZE 8192 // for reading/sending data

void create_cache();
int is_request_cached(char* uri);

int create_cache_file_for_request(char* uri, char buffer[], char* temp_filename, bool is_req_end);
int get_cache_file_for_request_and_send_to_client(char* uri, int client_socket_fd);
int delete_cache_file_for_request(char* uri);

char* get_filename_from_uri(char* uri);
void generate_random_temp_filename(char* temp);
char* hash(char* uri);


/*
* Create the cache directory
*/
void create_cache() {
	mkdir("./cache/", 0700);

	// seed srand for use in random temp_filename generation 
	srand(time(NULL));
}

/*
* Checks if request to uri is cached as a cache_file in the cache directory
*/
int is_request_cached(char* uri) {
	char* filename = get_filename_from_uri(uri);
	return access(filename, F_OK);
}

/*
* Create a cache_file entry in the cache directory for the request to uri
*/
int create_cache_file_for_request(char* uri, char buffer[], char* temp_filename, bool is_req_end) {
	// get_filename_from_uri(uri)
	// open filename... set cache_file_fd, create if not exist
	// write in data from the buffer... write in append mode starting from the EOF
	// close cache_file_fd
	int cache_file_fd = open(temp_filename, O_WRONLY | O_CREAT | O_APPEND, S_IRWXU);
	int write_succ = write(cache_file_fd, buffer, strlen(buffer));

	// delete cache_file if error occurs on create|write
	if ((-1 == cache_file_fd) || (-1 == write_succ)) {
		printf("Error occured in caching request to - attempting to delete its cache file %s...\n", uri, temp_filename);
		if (-1 == delete_cache_file_for_request(uri)) {
			printf("Unable to delete %s: %s\n", temp_filename, strerror(errno));
			return -1;
		}
		printf("Cache file %s deleted.\n", temp_filename);
		return -1;
	}

	// cache_file size
	struct stat sb;
	stat(temp_filename, &sb);
	// printf("File size:                %lld bytes\n", (long long) sb.st_size);

	close(cache_file_fd);
	printf("Data cached in temp_file %s\n", temp_filename);

	// rename temp_filename to hashed filename from the request uri
	if (is_req_end) {
		char* filename = get_filename_from_uri(uri);
		rename(temp_filename, filename);
		printf("Temp_cache file %s renamed to final cache file %s\n", temp_filename, filename);
	}
	return 0;
}

/*
* Get the cache_file contents for request to uri
*/
int get_cache_file_for_request_and_send_to_client(char* uri, int client_socket_fd) {
	// get_filename_from_uri(uri)
	// open filename... set cache_file_fd
	// read in data from filename to buffer
	// buffer of data will be sent to client via send in proxyFilter.c
	// close cache_file_fd
	char* filename = get_filename_from_uri(uri);
	int cache_file_fd = open(filename, O_RDONLY);

	// do not attempt to fetch from cache_file and delete it if error on open
	if (-1 == cache_file_fd) {
		printf("Error occured in retrieving cached data for request to %s - attempting to delete its cache file %s...\n", uri, filename);
		if (-1 == delete_cache_file_for_request(uri)) {
			printf("Unable to delete %s: %s\n", filename, strerror(errno));
			return -1;
		}
		printf("Cache file %s deleted.\n", filename);
		return -1;
	}

	int num_bytes_read;
	do {
		char buffer[BUFFER_SIZE];
		memset(buffer, 0, BUFFER_SIZE);
		num_bytes_read = read(cache_file_fd, buffer, BUFFER_SIZE);
		if (num_bytes_read > 0) {
			send(client_socket_fd, buffer, BUFFER_SIZE, 0); // send data in buffer to client
		}
	} while (num_bytes_read > 0);

	close(cache_file_fd);
	printf("Request retrieved from cache.\n");
	
	close(client_socket_fd);
	printf("Closing connection to client.\n");
	return 0;
}

/*
* Deletes the cache_file for request to uri from the cache directory
*/
int delete_cache_file_for_request(char* uri) {
	char * filename = get_filename_from_uri(uri);
	return remove(filename);
}

/*
* Get the filename for request uri
*/
char* get_filename_from_uri(char* uri) {
	return hash(uri);
}

/*
* Randomly generate a temporary filename
*/
void generate_random_temp_filename(char* temp) {
	int r = rand();
	sprintf(temp, "./cache/temp_%d", r);
}

/*
* Hashes uri into a filename
*/
char* hash(char* uri) {
	// initialize hash with a prime number
	int hash = 7;
	int i;

	for (i = 0; i < strlen(uri); i++) {
		hash = hash*31 + uri[i];
	}

	char* filename = (char*) malloc(256);
	sprintf(filename, "./cache/%u\0", hash);
	return filename;
}

