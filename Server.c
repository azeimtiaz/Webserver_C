#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ctype.h>
// #include <strings.h>
// #include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define BUF_SIZE 1024
#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void error(const char *);
int init_connect(unsigned short *);
void accept_req(int);
int read_line(int, char *);
void cannot_implement(int);
void not_found(int);
void send_file(int , const char *);
void exec_cgi(int, const char *, const char *, const char *);
void http_header(int, const char *);
void cat(int, FILE *);
void bad_request(int);
void cannot_exec(int);

/***
 * Prints error and terminates program
 * **/
void error(const char *msg) {
    //interprets error no and outputs the error description using sterror
    perror(msg); 
    exit(1);
}

int init_connect(unsigned short *port) {
    int sockfd=0;
    struct sockaddr_in serv_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        error("Error opening socket\n");
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(*port);

    if(bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("bind");
    
    if(*port == 0) {
        int servlen = sizeof(serv_addr);
        if(getsockname(sockfd, (struct sockaddr *)&serv_addr, &servlen) == -1)
            error("getsockname");
        *port = ntohs(serv_addr.sin_port);
    }

    if(listen(sockfd, 5) < 0)
        error("listen");
    
    return(sockfd);
}

void accept_req(int client) {
    
    int numchars;
    char buffer[BUF_SIZE];
    char method[255];
    char url[255];
    char path[512];
    unsigned long i, j;
    struct stat st;
    int cgi = 0;        // to check if this is a cgi program
    char *queryStr = NULL;
    
    numchars = read_line(client, buffer);
    i, j = 0;

    while((!isspace(buffer[j])) && (i < sizeof(method)-1)) {
        method[i] = buffer[j];
        i++; j++;
    }
    method[i] = '\0';

    if(strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        cannot_implement(client);
        return;
    }

    if(strcasecmp(method, "POST") == 0)
        cgi = 1;

    i = 0;
    while((isspace(buffer[j])) && (j < BUF_SIZE))
        j++;

    while((!isspace(buffer[j])) && (i < sizeof(url)-1) && (j < BUF_SIZE)) {
        url[i] = buffer[j];
        i++; j++;
    }
    url[i] = '\0';

    if(strcasecmp(method, "GET") == 0) {
        queryStr = url;

        while((*queryStr != '?') && (*queryStr != '\0'))
            queryStr++;

        if(*queryStr == '?') {
            cgi = 1;
            *queryStr = '\0';
            queryStr++;
        }
    }

    sprintf(path, "htdocs%s", url);
    if(path[strlen(path)-1] == '/')
        strcat(path, "index.html");

    if(stat(path, &st) == -1) {
        while((numchars > 0) && strcmp("\n", buffer))   // read and discard headers
            numchars = read_line(client, buffer);
        not_found(client);
    } else {
        if((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");

        if((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
            cgi = 1;
        
        if(!cgi)
            send_file(client, path); 
        else
            exec_cgi(client, path, method, queryStr);
    }
    close(client);
}

int read_line(int client, char *buffer) {
    int i = 0;
    char c = '\0';
    int n;

    while((i < BUF_SIZE-1) && (c != '\n')) {
        n = recv(client, &c, 1, 0);
        if(n > 0) {
            if(c == '\r') {
                n = recv(client, &c, 1, MSG_PEEK);
                if((n > 0) && (c == '\n'))
                    recv(client, &c, 1, 0);
                else
                    c = '\n';
            }
            buffer[i] = c;
            i++;
        } else 
            c ='\n';
    }
    buffer[i] = '\0';

    return(i);
}

void cannot_implement(int client) {
    char buffer[BUF_SIZE];

    sprintf(buffer, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, SERVER_STRING);
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "Content-Type: text/html\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "</TITLE></HEAD>\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "</BODY></HTML>\r\n");
    send(client, buffer, strlen(buffer), 0);
}

void not_found(int client) {
    char buffer[BUF_SIZE];

    sprintf(buffer, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, SERVER_STRING);
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "Content-Type: text/html\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "<HTML><TITLE>Not found</TITLE>\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "<BODY><P>The server could not fulfill\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "your request because the resource specified\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "is unavailable or nonexistent.\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "</BODY></HTML>\r\n");
    send(client, buffer, strlen(buffer), 0);
}

void send_file(int client, const char *filename) {
    FILE *resource = NULL;
    int numchars = 1;
    char buffer[BUF_SIZE];

    buffer[0] = 'A'; buffer[1] = '\0';

    while((numchars > 0) && strcmp("\n", buffer))
        numchars = read_line(client, buffer);

    resource = fopen(filename, "r");
    if(resource == NULL)
        not_found(client);
    else {
        http_header(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

void exec_cgi(int client, const char *path, const char *method, const char *queryStr) {
    char buffer[BUF_SIZE];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int cont_len = -1;

    buffer[0] = 'A'; buffer[1] = '\0';

    if(strcasecmp(method, "GET") == 0)      // if GET method
        while ((numchars > 0) && strcmp("\n", buffer))          // read and discard headers
            numchars = read_line(client, buffer);
    else {                                  // if POST method
        numchars = read_line(client, buffer);
        while((numchars > 0) && strcmp("\n", buffer)) {
            buffer[15] = '\0';
            if (strcasecmp(buffer, "Content-Length:") == 0)
                cont_len = atoi(&(buffer[16]));
            numchars = read_line(client, buffer);
        }
        if(cont_len == -1) {
            bad_request(client);
            return;
        }
    }

    sprintf(buffer, "HTTP/1.0 200 OK\r\n");
    send(client, buffer, strlen(buffer), 0);

    if(pipe(cgi_output) < 0) {      // if output pipe is not created
        cannot_exec(client);
        return;
    }
    if(pipe(cgi_input) < 0) {       // if input pipe is not created
        cannot_exec(client);
        return;
    }

    if((pid = fork()) < 0 ) {       // if couldn't create child process
        cannot_exec(client);
        return;
    }

    if(pid == 0) {     // Child CGI script
        char method_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], 1);     // replacing stdout_fd with output_fd[1] (writing data in pipe)
        dup2(cgi_input[0], 0);      // replacing stdin_fd with input_fd[0] (read data in pipe)
        
        close(cgi_output[0]);       // close read_fd in output pipe
        close(cgi_input[1]);        // close write_fd in input pipe
        
        sprintf(method_env, "REQUEST_METHOD=%s", method);       // create environment variable for method
        putenv(method_env);

        if(strcasecmp(method, "GET") == 0) {   // if GET
            sprintf(query_env, "QUERY_STRING=%s", queryStr);        // create environment variable for query
            putenv(query_env);
        } else {                                // if POST
            sprintf(length_env, "CONTENT_LENGTH=%d", cont_len);     // create environment variable for content length
            putenv(length_env);
        }

        execl(path, path, NULL);        // execute shell script
        exit(0);

        } else {            // Parent CGI script
            close(cgi_output[1]);
            close(cgi_input[0]);
            if(strcasecmp(method, "POST") == 0)
            for (i = 0; i < cont_len; i++) {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
            while(read(cgi_output[0], &c, 1) > 0)
                send(client, &c, 1, 0);

            close(cgi_output[0]);
            close(cgi_input[1]);
            waitpid(pid, &status, 0);
    }
}

void http_header(int client, const char *filename) {
    char buffer[BUF_SIZE];
    (void)filename;

    strcpy(buffer, "HTTP/1.0 200 OK\r\n");
    send(client, buffer, strlen(buffer), 0);
    strcpy(buffer, SERVER_STRING);
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "Content-Type: text/html\r\n");
    send(client, buffer, strlen(buffer), 0);
    strcpy(buffer, "\r\n");
    send(client, buffer, strlen(buffer), 0);
}

void cat(int client, FILE *resource) {
    char buffer[BUF_SIZE];

    fgets(buffer, BUF_SIZE, resource);
    while(!feof(resource)) {
        send(client, buffer, strlen(buffer), 0);
        fgets(buffer, BUF_SIZE, resource);
    }
}

void bad_request(int client) {
    char buffer[BUF_SIZE];
 
    sprintf(buffer, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buffer, BUF_SIZE, 0);
    sprintf(buffer, "Content-type: text/html\r\n");
    send(client, buffer, BUF_SIZE, 0);
    sprintf(buffer, "\r\n");
    send(client, buffer, BUF_SIZE, 0);
    sprintf(buffer, "<P>Your browser sent a bad request, ");
    send(client, buffer, BUF_SIZE, 0);
    sprintf(buffer, "such as a POST without a Content-Length.\r\n");
    send(client, buffer, BUF_SIZE, 0);
}

void cannot_exec(int client) {
    char buffer[BUF_SIZE];

    sprintf(buffer, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "Content-type: text/html\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "<P>Error prohibited CGI execution.\r\n");
    send(client, buffer, strlen(buffer), 0);
}

int main(void) {

    int serv_sock = -1;
    int cli_sock = -1;
    unsigned short port = 0;
    struct sockaddr_in client_addr;
    socklen_t clilen;

    serv_sock = init_connect(&port);

    printf("Server running on port %d\n", port);
    
    while(1) {
        cli_sock = accept(serv_sock, (struct sockaddr *) &client_addr, &clilen);
        if(cli_sock < 0)
            error("accept");
        accept_req(cli_sock);
    }
    
    close(serv_sock);
    // close(cli_sock);
    return 0;
}