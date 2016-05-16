// beej.us guide and provided multithread_server.c file were used as references in the following code for setting up socket 

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <stdbool.h>
#include <pthread.h>


#define BUFFER_SIZE 8192 // for reading/sending data
#define DEFAULT_PORT 80 // default port number for connecting to host 
#define NUM_THREADS 4
#define NUM_BYTES_PARSE_STATUS_CODE 256 // number of bytes to read in response that should be sufficient to parse status code

void print_usage_and_exit();
int start_server(int port);
void handle_new_client(int client_socket_fd);
void process_request(char buffer[], int client_socket_fd);
int count_colons(char* string);
void use_proxy(char* host, char* uri, char buffer[], int port, int client_socket_fd);
void print_buffer(char buffer[]);
void * connection_handler(void * server_socket_fd);
void send_error_msg_and_close(char buffer[], int client_socket_fd);
void parse_status_code(char * dest, const char * response);
bool valid_status_code(const char * status_code);
void get_first_line(char * dest, const char * response);
bool using_chunked_encoding(char response[]);

bool blacklist_enabled = false;

/**
* Processes command line args (port, blacklist file) and starts proxy server.
*/
int main(int argc, char **argv) {
	if (argc < 2 || argc > 3) {
		print_usage_and_exit();
	}
	
	if (argc == 3) {
		// Process blacklist file
		if (-1 == read_blacklist_file(argv[2])) {
			printf("Error opening/reading blacklist file %s.\n", argv[2]);
			return -1;
		}
		blacklist_enabled = true;
		printf("Finished reading blacklist file.\n");
	}

	// Create cache directory
	create_cache();
	printf("Cache created\n");

	// get port the proxy server will listen on
	int port_to_listen_on = atoi(argv[1]);

	// start the proxy server 
	return start_server(port_to_listen_on);
}


/**
* Start the proxy server by creating a socket and listening for connections on given port.
* If connection is accepted, will forward requests from client to host and forward data
* from host to client.
*/
int start_server(int port) {
	printf("Proxy server is using port %d\n", port); 

	// Create the TCP socket
	int socket_fd; 	// socket file descriptor
	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (-1 == socket_fd) {
		printf("Failed to create socket\n");
		return -1;
	}
	
	// Bind the socket to the port
	struct sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.s_addr = INADDR_ANY; // use my IPv4 address
	
	if (bind(socket_fd, (struct sockaddr *) &server, sizeof(server)) < 0) {
		printf("Failed to bind socket\n");
		return -1;
	}
	
	// Listen for incoming connection
	if (-1 == listen(socket_fd, 10)) {
		printf("Failed to listen for incoming connections\n");
		return -1;
	}
	
	// Create worker threads 
	pthread_t tid[NUM_THREADS];
	int err;
	int i = 0;
	while (i < NUM_THREADS) {
		err = pthread_create(&tid[i], NULL, &connection_handler, (void *) &socket_fd);
		if (0 != err) {
			printf("Error creating thread %d with error number %d\n", i, err);
		}
		i++;
	}
	for (i = 0; i < NUM_THREADS; i++) {
		pthread_join(tid[i], NULL);
	}
		
	printf("Waiting for incoming connection...\n");
	

	
	return 0;
}

/**
* Handles accepting a connection from a client.
*/
void * connection_handler(void * server_socket_fd) {
	// Accept connection
	while (true) {
		struct sockaddr_in client_addr;
		int client_addr_size = sizeof(client_addr);
		int client_socket_fd;
		client_socket_fd = accept(*(int *) server_socket_fd, (struct sockaddr *) &client_addr, &client_addr_size);
		if (-1 == client_socket_fd) {
			printf("Failed to accept incoming connection\n");
		} else {
			printf("Established a new connection.\n");
			handle_new_client(client_socket_fd);
		}
		printf("Waiting for incoming connection...\n");
	}
}

/**
* Send error message to client and close connection.
*/
void send_error_msg_and_close(char buffer[], int client_socket_fd) {
	send(client_socket_fd, buffer, strlen(buffer), 0);
	close(client_socket_fd);
}	


/*
* Receives request from new client and processes it.
*/
void handle_new_client(int client_socket_fd) {
	char buffer[BUFFER_SIZE]; // buffer for sending/receiving data
	memset(buffer, 0, BUFFER_SIZE);
	
	int recv_data = recv(client_socket_fd, buffer, BUFFER_SIZE, 0);
	if (-1 == recv_data) {
		printf("Error receiving data from client.\n");
		close(client_socket_fd);
		return;
	}
	if (0 == recv_data) {
		printf("Client closed connection.\n");
		return;
	}
	//printf("%s%s%s", "Received request:\n", buffer, "\n");
	process_request(buffer, client_socket_fd);
}

/*
* Process request if request is valid HTTP request.
*/
void process_request(char buffer[], int client_socket_fd) {	
	char buffer_copy[BUFFER_SIZE];
	strcpy(buffer_copy, buffer);
	
	// parse buffer for header, URI, protocol 
	char header[10], URI[BUFFER_SIZE], protocol[10];
	if (3 != sscanf(buffer_copy, "%s %s %s", &header, &URI, &protocol)) {
		memset(buffer, 0, BUFFER_SIZE);
		sprintf(buffer, "405 Method Not Allowed. Request not in correct format 'GET absoluteURI[:port] HTTP/1.1'. Note: only GET is allowed.\n");
		send_error_msg_and_close(buffer, client_socket_fd);
		return;
	}
	
	// Check for GET, HTTP/1.1 in request
	if (0 != strcmp("GET", header) || 0 != strcmp("HTTP/1.1", protocol)) {
		memset(buffer, 0, BUFFER_SIZE);
		sprintf(buffer, "405 Method Not Allowed. Request not in correct format 'GET absoluteURI[:port] HTTP/1.1'. Note: only GET is allowed.\n");
		send_error_msg_and_close(buffer, client_socket_fd);
		return;
	}
	
	// ASSUME: URI begins with http:// and does not have more than two colons
	
	// parse out port, host if any 
	char URI_copy[BUFFER_SIZE];
	strcpy(URI_copy, URI);
	
	char host[256];
	char host_and_request[BUFFER_SIZE];
	char request[BUFFER_SIZE];
	char port_as_string[6];
	int port = DEFAULT_PORT;
	int num_colons = count_colons(URI);
	char * strptr;
	
	// get host plus request
	strptr = strtok(URI_copy, ":");
	strptr = strtok(NULL, ":");
	strcpy(host, strptr);
	strcpy(host_and_request, strptr);
	
	// get port if given port number
	if (2 == num_colons) {
		strptr = strtok(NULL, ":");
		strcpy(port_as_string, strptr);
		port = atoi(port_as_string);
	}		
	
	// get host
	strptr = strtok(host, "//");
	strcpy(host,strptr);	
	
	// if blacklist enabled, check if host is blacklisted
	if (blacklist_enabled) {
		printf("checking blacklist...\n");
		// if host blacklisted, send 403 and close connection
		if (is_blacklisted(host)) {
			printf("Host is blacklisted.\nClosing connection to client.\n");
			memset(buffer, 0, BUFFER_SIZE);
			sprintf(buffer, "403 Forbidden.\n");
			send_error_msg_and_close(buffer, client_socket_fd);
			return;
		}
	}
	
	// get request (2 is for getting rid of leading "//")
	strcpy(request, host_and_request + 2 + strlen(host));	
	
	// Get new buffer copy 
	memset(buffer_copy, 0, BUFFER_SIZE);
	strcpy(buffer_copy, buffer);
	memset(buffer, 0, BUFFER_SIZE);	
	
	// Write proper HTTP request into buffer to send to host 
	sprintf(buffer, "GET %s HTTP/1.1\r\nHost: %s\r\n", request, host);
	char * request_without_first_line = strchr(buffer_copy, '\n');
	// Append fields after the GET line, if any 
	if (NULL != request_without_first_line) {
		strcat(buffer, ++request_without_first_line);
	}
	// Append necessary CLRF
	strcat(buffer, "\r\n\r\n");
	
	// Print out information about request 
	printf("Host: %s\n", host);
	printf("Port: %d\n", port);
	printf("Request: %s\n", request);
	
	// Before sending request, check the cache
	char uri[BUFFER_SIZE];
	memset(uri, 0, BUFFER_SIZE);
	strcpy(uri, host_and_request); 

	printf("Check if %s is cached...\n", uri);
	if (0 == is_request_cached(uri)) {
		printf("Request is cached, get it from cache!\n");
		// Fetch from host if error when retrieving from cache
		if (-1 == get_cache_file_for_request_and_send_to_client(uri, client_socket_fd)) {
			printf("Fetch from host...\n");
			use_proxy(host, uri, buffer, port, client_socket_fd);
		}
	} else {
		// send request to host, get response and send to client 
		printf("Request is NOT cached, ping host!\n");
		use_proxy(host, uri, buffer, port, client_socket_fd);
	}
}

/*
* Creates socket to host server, sends request which is contained in buffer, receives response, sends response back to client.
*/
void use_proxy(char* host, char* uri, char buffer[], int port, int client_socket_fd) {
	
	// Set up socket to host server 
	int host_socket_fd;
	host_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (-1 == host_socket_fd) {
		printf("Failed to create socket to host\n");
		memset(buffer, 0, BUFFER_SIZE);
		sprintf(buffer, "Internal Error 500.\n");
		send_error_msg_and_close(buffer, client_socket_fd);
		return;
	}	
	 
	struct sockaddr_in host_addr;
	host_addr.sin_family = AF_INET;
	host_addr.sin_port = htons(port);
	
	// resolve ip address of host
	struct hostent *host_entry;
	host_entry = gethostbyname(host);
	if (NULL == host_entry) {
		printf("Failed to resolve host.\n"); 
		memset(buffer, 0, BUFFER_SIZE);
		sprintf(buffer, "404 Not Found. Failed to resolve host.\n");
		send_error_msg_and_close(buffer, client_socket_fd);
		return;
	}
	bcopy(host_entry->h_addr, &host_addr.sin_addr.s_addr, host_entry->h_length);
	
	// connect to host server 
	if (-1 == connect(host_socket_fd, (const struct sockaddr *) &host_addr, sizeof(struct sockaddr))) {
		printf("Failed to connect to host server.\n");
		memset(buffer, 0, BUFFER_SIZE);
		sprintf(buffer, "502 Bad Gateway.\n");
		send_error_msg_and_close(buffer, client_socket_fd);		
		return;
	}
	printf("Connected to host server.\n");
	
	// send request 
	if (-1 == send(host_socket_fd, buffer, strlen(buffer), 0)) {
		printf("Failed to send request to host server.\n");
		memset(buffer, 0, BUFFER_SIZE);
		sprintf(buffer, "500 Internal Server Error.\n");
		send_error_msg_and_close(buffer, client_socket_fd);				
		return;
	}
	printf("Sent request to host.\n");
	
	// receive response and send to client
	int num_bytes_read;
	bool is_first_read;
	is_first_read = true;
	int sum = 0;
	int abort_caching = false;

	// generate temp cache_file filename: temp_xxx, where xxx is a random int
	char temp_cache_filename[sizeof(char) * (14 + sizeof(RAND_MAX))];
	generate_random_temp_filename(temp_cache_filename);

	do {
		memset(buffer, 0, BUFFER_SIZE);
		// receive response
		num_bytes_read = recv(host_socket_fd, buffer, BUFFER_SIZE, 0);
		if (-1 == num_bytes_read && is_first_read) {
			printf("Failed to receive response from host server.\n");
			memset(buffer, 0, BUFFER_SIZE);
			sprintf(buffer, "500 Internal Server Error.\n");
			send_error_msg_and_close(buffer, client_socket_fd);	
			return;
		}
		
		// On first read, parse status code from first line 
		if (is_first_read) {	
		
			char first_line[NUM_BYTES_PARSE_STATUS_CODE];
			get_first_line(first_line, buffer);
		
			char status_code[NUM_BYTES_PARSE_STATUS_CODE];
			parse_status_code(status_code, first_line);
			
			is_first_read = false;

			// print status code to server
			printf("%s\n", first_line);

			if (!valid_status_code(status_code)) {
				memset(buffer, 0, BUFFER_SIZE);
				sprintf(buffer, "%s\n", first_line);
				send_error_msg_and_close(buffer, client_socket_fd);
				return;
			}

			// print whether using chunked encoding
			if (using_chunked_encoding(buffer)) {
				printf("Using chunked encoding.\n");
			} else {
				printf("Not using chunked encoding.\n");
			}
		}
		
		printf("Response received from host.\n");
		// send to client 
		if (-1 == send(client_socket_fd, buffer, strlen(buffer), 0)) {
			printf("Failed to send response to client.\n");
			sprintf(buffer, "503 Service Unavailable.");
			send_error_msg_and_close(buffer, client_socket_fd);
			return;
		}
		printf("Sent data to client.\n");
		// cache response
		// Abort caching this request if error occurs in cache file create|write
		if (!abort_caching) {
			if (-1 == create_cache_file_for_request(uri, buffer, temp_cache_filename, (0 == num_bytes_read || -1 == num_bytes_read))) {
				abort_caching = true;
			}
		}

		if (0 == num_bytes_read) {
			printf("Host has closed the connection.\n");
			memset(buffer, 0, BUFFER_SIZE);
			break;
		}

		// keep track of bytes received
		sum = sum + num_bytes_read;
		//printf("Bytes received so far:	%d bytes\n", sum);
	} while (num_bytes_read > 0);	

	//printf("Total bytes received: %d\n", sum);
	
	// Close connection to host 
	close(host_socket_fd);
	printf("Closing connection to host.\n");
	
	// Close connection to client 
	close(client_socket_fd);
	printf("Closing connection to client.\n");
}

/**
* Returns true if using chunked encoding, otherwise false.
*/
bool using_chunked_encoding(char response[]) {
	return (NULL != strstr(response, "Transfer-Encoding: chunked"));
}



/**
* Gets first line of a char buffer.
*/
void get_first_line(char * dest, const char * response) {
	// copy response
	char * response_copy = (char *) malloc(sizeof(char) * strlen(response));
	strcpy(response_copy, response);
	
	// get first line
	char * strptr;
	strptr = strtok(response_copy, "\n");
	strcpy(dest, strptr);	
}

/**
* Sets status code from the response headers in the first response from the server.
*/
void parse_status_code(char * dest, const char * response) {
	// copy response 
	char * response_copy = (char *) malloc(sizeof(char) * strlen(response));
	strcpy(response_copy, response);
	
	// get second space-delimited word (the status code)
	char * strptr;
	strptr = strtok(response_copy, " ");
	strptr = strtok(NULL, " ");
	strcpy(dest, strptr);
}

/**
* Returns true if status code is 2XX, otherwise false
*/
bool valid_status_code(const char * status_code) {
	if ('2' == *status_code) {
		return true;
	}
	return false;
}

/**
* Print contents of given buffer ignoring the '\r' carriage return
*/
void print_buffer(char buffer[]) {
	int i;
	for (i = 0; i < strlen(buffer); i++) {
		if ('\r' != buffer[i]) {
			putchar(buffer[i]);
		}
	}
}

/**
* Returns number of colons (':') in given string 
*/
int count_colons(char *string) {
	int chptr = 0;
	int count = 0;
	while ('\0' != *(string + chptr)) {
		if (':' == *(string + chptr)) {
			count++;
		}
		chptr++;
	}		
	return count;
}

/**
* Prints usage info for the program and exits.
*/
void print_usage_and_exit() {
	printf("Usage: ./proxyFilter port_no [blacklist_file]\n");
	exit(-1);
}


