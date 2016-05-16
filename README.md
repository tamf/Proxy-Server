# Proxy-Server

Implementation of a web proxy server using the Unix socket API. Also includes a filter for blocking blacklisted websites. Based on HTTP/1.1 specification in RFC 2616 for GET proxy request. Server is multi-threaded to handle multiple clients simultaneously. Simple caching of content is also implemented. Client is responsible for handling chunked encoding.

Collaborated with project partner Sisi Guo.

# Starting the proxy server:

./proxyFilter port_no [blacklist_file]

port_no is the port number that the proxy server will listen on. blacklist_file is an optional argument which is the name of the file containing blacklisted websites/substrings. The file must be in the same directory and each entry is to be separated by new line. No empty lines should exist in the blacklist file.


# Sending a request to the proxy server:

GET absoluteURI[:port] HTTP/1.1

The port is optional, default port 80. absolute is the URI which cannot contain colons. E.g. of valid absoluteURI: www.reddit.com
