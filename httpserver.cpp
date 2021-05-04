/*
*	Name:Ryanul Haque, Aaron Mandel
*	Course: CSE130
*	user ID: ryhaque, aamandel
*	Assignment 2: httpserver.c

*/

#include <arpa/inet.h> //inet_addr
#include <dirent.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h> //threads
#include <fcntl.h>
#include <unistd.h> // write
#include <string.h> // memset
#include <stdlib.h> // atoi
#include <stdbool.h> // true, false
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include "Queue.h"
#include "Queue.cpp"

#define BUFFER_SIZE 32000
#define request_size 4096
#define MAXNUMTHREADS 50
#define MAXNUMFILES 100

struct httpObject {
    /*
        Create some object 'struct' to keep track of all
        the components related to a HTTP message
        NOTE: There may be more member variables you would want to add
    */
    char method[5];         // PUT, HEAD, GET
    char filename[11];      // what is the file we are worried about
    char httpversion[9];    // HTTP/1.1
    int content_length; // example: 13
    uint8_t buffer[BUFFER_SIZE];
};

//struct to hold data for threads to share
struct shared_data {
    pthread_t threads[MAXNUMTHREADS];  //array of threads
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    Queue taskQueue;

    int numFiles;
    char *known_files[MAXNUMFILES];
    pthread_mutex_t files_mutex[MAXNUMFILES];
    pthread_mutex_t new_file_mutex;    //global mutex for creating new files with PUT

    int redundancy;

    int n;                             //number of threads initialized
    const char* port;                  //port number
    const char* address;               //address
};

//--------------------REDUNDANCY STUFF------------------------------//

int shouldCopy(char* name){
    if(strcmp(name, "httpserver.cpp") == 0){
        return 0;
    }
    if(strcmp(name, "httpserver.o") == 0){
        return 0;
    }
    if(strcmp(name, "httpserver") == 0){
        return 0;
    }
    if(strcmp(name, "Makefile") == 0){
        return 0;
    }
    if(strcmp(name, "Queue.cpp") == 0){
        return 0;
    }
    if(strcmp(name, "Queue.h") == 0){
        return 0;
    }



    if(strcmp(name, "copy1") == 0){
        return 0;
    }

    if(strcmp(name, "copy2") == 0){
        return 0;
    }

    if(strcmp(name, "copy3") == 0){
        return 0;
    }
    if(strcmp(name, "..") == 0){
        return 0;
    }
    if(strcmp(name, ".") == 0){
        return 0;
    }

    return 1;
}

void copydata(char* cd_file, char* copy_to_dir){

    int src_fd,dst_fd;
    char buffer[BUFFER_SIZE];
    char pathname[100];

    sprintf(pathname, "./%s/%s", copy_to_dir, cd_file);

    src_fd = open(cd_file, O_RDONLY);
    dst_fd = open(pathname, O_CREAT | O_WRONLY | O_TRUNC, 0777);

    
    if(src_fd == -1){
        warn("%s", cd_file);
    }
    else if(dst_fd == -1){
        warn("%s", pathname);
    }
    else{
        int sz=1;
        while(sz>0){
            sz = read(src_fd, buffer, BUFFER_SIZE);
            write(dst_fd, buffer, sz); 
            
            if(sz<0){
                warn("%s",cd_file);
            }   
        }
    }

    close(src_fd);
    close(dst_fd);

}

int filecmp(char* fpath1, char* fpath2){
    char buffer1[10];
    char buffer2[10];

    int fd_one = open(fpath1, O_RDONLY);
    int fd_two = open(fpath2, O_RDONLY);
    
    if(fd_one == -1){
        warn("%s", fpath1);
    }
    else if(fd_two == -1){
        warn("%s", fpath2);
    }
    else{
        int sz_one=1;
        int sz_two=1;
        int stillSame = 1;
        while(sz_one>0 && sz_two>0){
            sz_one = read(fd_one, buffer1, 1);
            sz_two = read(fd_two, buffer2, 1);
            if(memcmp(buffer1, buffer2, 1)){
                stillSame = 0;
            }
            
            if(sz_one<0){
                warn("%s", fpath1);
            }   
            if(sz_two<0){
                warn("%s", fpath2);
            }
        }
        return stillSame;
    }

    close(fd_one);
    close(fd_two);
    return 0;

}

/*
    \brief 1. Want to read in the HTTP message/ data coming in from socket
    \param client_sockd - socket file descriptor
    \param message - object we want to 'fill in' as we read in the HTTP message
*/
void read_http_response(ssize_t client_sockd, struct httpObject* message) {

    //Start constructing HTTP request based off data from client socket
    ssize_t bytes = 0;
    bytes = recv(client_sockd, message->buffer, BUFFER_SIZE, 0); //buffer stores Request
    
    char nuBuff[BUFFER_SIZE];
    nuBuff[bytes] = '\0';

    strcpy(nuBuff, (const char*) message->buffer);
    char* header;
    header = strtok(nuBuff, "\r\n"); //header = PUT /file HTTP/1.1\0
    header = strtok(NULL, "\r\n" );
    
    //parse buffer
    int bad_req_check = sscanf((const char*) message->buffer, "%s /%s %s", message->method, message->filename, message->httpversion);

    if(bad_req_check != 3){
       
        printf("Bad Request from Client\n");

        char response[request_size];
        message->content_length = 0;
        sprintf(response, "%s 400 Bad Request\r\nContent-Length:%d\r\n\r\n", message->httpversion, message->content_length);
        write(client_sockd, response, strlen(response));
        close(client_sockd);
        return;message->content_length = 0;

    }

    while(header != NULL){
        char str1[request_size];
        char str2[request_size];
        int check_str = sscanf(header, "%[^:]:%s",str1, str2);
        
        if(check_str < 2 && strcmp(header, "/*") != 0){
            printf("Bad Request\n");

            char response[request_size];
            message->content_length = 0;
            sprintf(response, "%s 400 Bad Request\r\nContent-Length:%d\r\n\r\n", message->httpversion, message->content_length);
            write(client_sockd, response, strlen(response));
            close(client_sockd); 
            return;  
        }
        if(strstr(str1, "Content-Length") != NULL ){
            
            if(sscanf(str2,"%d", &message->content_length) != 1){
                printf("Bad Request\n");
            
                char response[request_size];
                message->content_length = 0;
                sprintf(response, "%s 400 Bad Request\r\nContent-Length:%d\r\n\r\n", message->httpversion, message->content_length);
                write(client_sockd, response, strlen(response));
                close(client_sockd);
            }
            
        }
        header = strtok(NULL, "\r\n" ); //Tokenizez the next NULL character (cause token is at NULL rn,so read next line)
    }

    return;
}

int valid_filename( struct httpObject* message ){

	//Ensure filename is ASCII	
    	int i = 0;
    	int numOfAlpha = 0;
    	int numOfdigits = 0;
    	
    	int invalid = 0;

    	while(message->filename[i] != '\0'){
    		if(isalpha(message->filename[i]) != 0){
    			numOfAlpha++;
    		}
    		else if(isdigit(message->filename[i]) != 0){
    			numOfdigits++;
    		}else{
    			invalid = -1;
    		}

    		i++;	
    	}

    	if( (numOfAlpha + numOfdigits <= 10) && (invalid == 0) ){
    		//it is valid
    		return 1;
    	}else{
    		return -1;
    	}

}

int known_file(char* filename, struct shared_data* data){
    for(int i=0; i<data->numFiles; i++){
        if(strcmp(filename, data->known_files[i]) == 0){
            return i;
        }
    }
    return -1;
}

/*
    \brief 2. Want to process the message we just recieved
*/
void process_request(ssize_t client_sockd, struct httpObject* message, struct shared_data* shared) {
    printf("Processing Request and generating response\n\n");

    char response[request_size];

    if(strlen(message->method) > 3){
        printf("method: UNKNOWN\n");
        printf("valid filename returns %d\n", valid_filename(message));
        warn("%s", message->filename);
        printf("Server Error\n");
        message->content_length = 0;
        snprintf(response, request_size, "%s 500 Internal Server Error\r\nContent-Length:%d\r\n\r\n", message->httpversion, message->content_length);
        write(client_sockd, response, strlen(response));
        close(client_sockd);
    }else if(strcmp(message->method,"PUT") == 0){
    	printf("method: PUT\n");
    	printf("valid filename returns %d\n", valid_filename(message));
    	//Ensure filename is ASCII

        //incompatible types in assignment of ‘const char [1]’ to ‘char [4096]’
    	if(valid_filename(message) == 1){ //we good

    		printf("Valid filename\n");
            
            //if we have a mutex for this filename
            int fileIndex = known_file(message->filename, shared);
            if(fileIndex != -1){
                //lock mutex
                pthread_mutex_lock(&shared->files_mutex[fileIndex]);
                //printf("locked file: %s\n", shared->known_files[fileIndex]);
            }else { //if we do not know this file, lock the global mutex to create the file
                pthread_mutex_lock(&shared->new_file_mutex);
                shared->known_files[shared->numFiles] = message->filename;
                shared->files_mutex[shared->numFiles] = PTHREAD_MUTEX_INITIALIZER;
                pthread_mutex_lock(&shared->files_mutex[shared->numFiles]);
                shared->numFiles++;
            }
    		
    		int fd = open(message->filename, O_CREAT | O_RDWR | O_TRUNC,0644);

    		//printf("PUT content length = %d\n",message->content_length);

    		if(fd == -1){ //fd<0
    			warn("%s", message->filename);
    			if(errno == 2 || errno == 9){
    				printf("No such file or directory\n");
    				message->content_length = 0;
    				sprintf(response, "%s 404 File Not Found\r\nContent-Length:%d\r\n\r\n", message->httpversion, message->content_length);
    				//return response;
                    write(client_sockd, response, strlen(response));
    				close(client_sockd);

    			}else if(errno == 13){
    				printf("Permission denied\n");

    				message->content_length = 0;
    				sprintf(response, "%s 403 Forbidden\r\nContent-Length:%d\r\n\r\n", message->httpversion, message->content_length);
    				//return response;
                    write(client_sockd, response, strlen(response));
    				close(client_sockd);

    			}else{
    				printf("Server Error\n");

    				message->content_length = 0;
    				sprintf(response, "%s 500 Internal Server Error\r\nContent-Length:%d\r\n\r\n", message->httpversion, message->content_length);
    				//return response;
                    write(client_sockd, response, strlen(response));
    				close(client_sockd);
    			}
    			

    		}else{
    			//Now flow data from Client_sockd to new fd
    			//char data[BUFFER_SIZE];
    			printf("File Created\n");
    			
    			sprintf(response, "%s 201 Created\r\n", message->httpversion);
                //return response;
    			write(client_sockd, response, strlen(response));

    			char data[BUFFER_SIZE];
    			int bytes = 1;
    			while(message->content_length > 0){

    				bytes = read(client_sockd, data, BUFFER_SIZE);
    				write(fd,data, bytes);

    				message->content_length = message->content_length - bytes;
    			}
                //unlock global mutex and new file mutex
                if(fileIndex == -1){
                    pthread_mutex_unlock(&shared->new_file_mutex);
                    pthread_mutex_unlock(&shared->files_mutex[shared->numFiles-1]);
                }
                //copy data into folders if redundancy is enabled
                if(shared->redundancy == 1){
                    copydata(message->filename, (char*)"copy1");
                    copydata(message->filename, (char*)"copy2");
                    copydata(message->filename, (char*)"copy3");
                }
    		}

    		close(fd);
    		close(client_sockd);

            if(fileIndex != -1){ //in the file array
                //unlock mutex
                pthread_mutex_unlock(&shared->files_mutex[fileIndex]);
            }
    		
    	}else if(valid_filename(message) == -1){
    		printf("Bad Request\n");

    		message->content_length = 0;
    		sprintf(response, "%s 400 Bad Request\r\nContent-Length:%d\r\n\r\n", message->httpversion, message->content_length);
    		//return response;
            write(client_sockd, response, strlen(response));
            close(client_sockd);
    	}

    }else if(strcmp(message->method,"GET") == 0){ 
    	//printf("In GET method\n");

    	if(valid_filename(message) == -1){
    		printf("Bad Request\n");

    		message->content_length = 0;
    		sprintf(response, "%s 400 Bad Request\r\nContent-Length:%d\r\n\r\n", message->httpversion, message->content_length);
    		//return response;
            write(client_sockd, response, strlen(response));
    		close(client_sockd);

    	}else if(valid_filename(message) == 1){
    		printf("Now it is a valid filename\n");

            //if we have a mutex for this filename
            int fileIndex = known_file(message->filename, shared);
            if(fileIndex != -1){
                //lock mutex
                pthread_mutex_lock(&shared->files_mutex[fileIndex]);
                printf("locked file: %s\n", shared->known_files[fileIndex]);
            }
    		
            int fd;
            int tempErrno;
            if(shared->redundancy != 1){ //if no redundancy for get
    		    fd = open(message->filename, O_RDONLY, 0644);
                tempErrno = errno;
            }else{
                char fileTest1[request_size];
                char fileTest2[request_size];
                char fileTest3[request_size];
                sprintf(fileTest1, "./copy1/%s", message->filename);
                sprintf(fileTest2, "./copy2/%s", message->filename);
                sprintf(fileTest3, "./copy3/%s", message->filename);
                if(filecmp(fileTest1, fileTest2) == 1 || filecmp(fileTest1, fileTest3) == 1){
                    fd = open(fileTest1, O_RDONLY, 0644); 
                }else if(filecmp(fileTest2, fileTest3) == 1){
                    fd = open(fileTest3, O_RDONLY, 0644);
                }else{
                    fd = -1;
                    tempErrno = -1;
                }
            }
		
			struct stat File_Info;
    		int info = fstat(fd, &File_Info); // stores info about the file from fd
    		message->content_length = (ssize_t)File_Info.st_size;

    		//printf("Content length = %d\n", message->content_length);

    		if(fd == -1 || info == -1){
   
    			warn("%s", message->filename);
    			//printf("errno get value: %d\n",tempErrno );
    			if(tempErrno == 9 || tempErrno == 2){
    				printf("No such file or directory\n");

    				message->content_length = 0;
    				sprintf(response, "%s 404 File Not Found\r\nContent-Length:%d\r\n\r\n", message->httpversion, message->content_length);
    				//return response;
                    write(client_sockd, response, strlen(response));
    				close(client_sockd);

    			}else if(tempErrno == 13){
    				printf("Permission denied\n");

    				message->content_length = 0;
    				sprintf(response, "%s 403 Forbidden\r\nContent-Length:%d\r\n\r\n", message->httpversion, message->content_length);
    				//return response;
                    write(client_sockd, response, strlen(response));
    				close(client_sockd);

    			}else if(tempErrno != 2 || tempErrno != 13){
    				printf("Server Error\n");

    				message->content_length = 0;
    				sprintf(response, "%s 500 Internal Server Error\r\nContent-Length:%d\r\n\r\n", message->httpversion, message->content_length);
    				//return response;
                    write(client_sockd, response, strlen(response));
    				close(client_sockd);
    			}

    		}else{

    			char data[BUFFER_SIZE];
    		
    			sprintf(response, "%s 200 OK\r\nContent-Length:%d\r\n\r\n", message->httpversion, message->content_length);
                write(client_sockd, response, strlen(response));
                
    			int bytes = 1;
    			while(bytes>0){
    			
    				bytes = read(fd, data, BUFFER_SIZE);
    				write(client_sockd, data, bytes);
    				//reset here cause now it's filled up so you still need to read more
    				if(strlen(data) == BUFFER_SIZE){
    					memset(data,0,sizeof(data));	
    				}	
    			}

    		}
    		close(fd);
    		close(client_sockd);

            if(fileIndex != -1){
                //unlock mutex
                pthread_mutex_unlock(&shared->files_mutex[fileIndex]);
            }

    	}

    }    
    return;
}

//--------------------THREAD STUFF----------------------------------//

//worker threads:
void *threadProcess(void *data) {
    struct shared_data* shared = (shared_data*) data;
    /*
    int nThreads = shared->n;
    int myIndex = -1;
    for(int i=0; i<nThreads; i++){
        if(pthread_self() == shared->threads[i]){
            myIndex = i;
        }
    }*/ 
    while(true){
        int clientSocket = -1;
        pthread_mutex_lock(&shared->queue_mutex);
        if(isEmpty(shared->taskQueue)){
            pthread_cond_wait(&shared->queue_cond, &shared->queue_mutex);
        }
        if(!isEmpty(shared->taskQueue)){
            clientSocket = (int)(intptr_t)(getFront(shared->taskQueue));
            Dequeue(shared->taskQueue);
        }
        pthread_mutex_unlock(&shared->queue_mutex);
        if(clientSocket != -1){
            //read request
            struct httpObject message;
            read_http_response(clientSocket, &message);

            //process request and construct response
            process_request(clientSocket, &message, shared);
        }
    }
    pthread_exit(NULL);
}

//main function acts as dispatcher thread
int main(int argc, char** argv) {
    
    //server variables:
    const char* port;         //port number
    const char* address;      //address
    int numThreads = 4;       //the number of threads (4 by default)
    int red_enable = 0;       //redundancy y/n

    //---------------------------------------------

    //code to set numThreads
    int options;

    while( (options = getopt(argc, argv, "N:r")) != -1){ // N 4
    	switch (options){
    		case 'N' :
    			//printf("You want to do something with numthreads = %s\n", optarg);
    			numThreads = atoi(optarg);
    			break;
    		case 'r' :
    			//printf("You want to do something with redundancy files = %s\n", optarg);
    			red_enable = 1; //enable red
    			break;
    	}
    }
    //for extra arguments namely port number and address 
    for(; optind < argc; optind++){
    	//printf("Extra arguments: %s\n", argv[optind]);
    	char str1[50];
    	char str2[50];
    	sscanf(argv[optind], "%[^:]:%s",str1, str2);
    	
    	//printf("str1: %s\n",str1);
    	//printf("str2: %s\n",str2);
    	if(strcmp(str1, "localhost")==0){
    		//printf("Formatted address and port number\n");
    		address = "127.0.0.1";
    		port = str2;
    	}else{
    		address = argv[optind-1];
        	port  = argv [optind];	
    	}
    		
    }
    
    if(strcmp(address, "localhost") == 0){
            address = "127.0.0.1";
    }else{ 
    	address = "127.0.0.1";
    }

    //printf("address = %s\n", address);
    //printf("port = %s\n", port);

    //---------------------------------------------

    char *files[MAXNUMFILES];      //array of valid filenames in directory
    int numFiles = 0;              //number of valid filenames in directory
    char *fileName;                //variable to temporarily hold filenames

    DIR *folder;
    
    struct dirent *entry; //for current directory
    
    //piazza post specified that we should assume these folders already exist with files in them
    /*if(red_enable == 1){
        mkdir("./copy1", 0777);
        mkdir("./copy2", 0777);
        mkdir("./copy3", 0777);
    }*/

    folder = opendir("."); //open current directory
    
    if(folder == NULL){
        perror("Unable to read directory");
        return 1;
    }
         
    while ((entry = readdir(folder)) != NULL) { //as you read fm curr dir
        
        if(shouldCopy(entry->d_name) == 1){ 
            if(red_enable == 1){
                /*printf("current directory files: %s\n", entry->d_name);
                
                copydata(entry->d_name, (char*)"copy1");
                copydata(entry->d_name, (char*)"copy2");
                copydata(entry->d_name, (char*)"copy3");*/
            }
            fileName = entry->d_name;
            files[numFiles] = fileName;
            numFiles++;
        }
    }

    closedir(folder);
    
    
    //create shared data
    struct shared_data thread_data;
    thread_data.redundancy = red_enable;
    thread_data.n = numThreads;
    thread_data.port = port;
    thread_data.address = address;
    thread_data.queue_mutex = PTHREAD_MUTEX_INITIALIZER;
    thread_data.queue_cond = PTHREAD_COND_INITIALIZER;
    thread_data.new_file_mutex = PTHREAD_MUTEX_INITIALIZER;
    thread_data.taskQueue = newQueue();
    thread_data.numFiles = 0;
    while(files[thread_data.numFiles] != NULL){
        thread_data.known_files[thread_data.numFiles] = files[thread_data.numFiles];
        thread_data.files_mutex[thread_data.numFiles] = PTHREAD_MUTEX_INITIALIZER;
        thread_data.numFiles++;
    }

    for(int i=0; i<numThreads; i++){
        if(pthread_create(&thread_data.threads[i], NULL, threadProcess, &thread_data) !=0){
            perror("cannot create thread");
        }

    }

    //Create sockaddr_in with server information:
    struct sockaddr_in server_addr; //server_addr object of struct 'sockaddr_in'
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;   //address family i.e. AF_INET(IP)
    server_addr.sin_port = htons(atoi(port)); //user defined port
    server_addr.sin_addr.s_addr = inet_addr(address); //Address of socket, MUST be a param for httpserver
    socklen_t addrlen = sizeof(server_addr);

    //create socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("cannot create socket()");
    }

    //bind address to socket
    if( bind( listen_fd, (struct sockaddr *) &server_addr, addrlen ) < 0 ){
        perror("bind() error");
    }
   
    //listen for incoming connections
    if ( listen(listen_fd, 500) < 0) {
        perror("listen() error");
    }

    
    //Connect with client
    struct sockaddr client_addr; //sockadd defined somewhere
    socklen_t client_addrlen;

    while (true) {
        printf("[+] server is waiting...\n\n");

        /*
        Accept Connection: Grabs the first request on the queue of pending requests 
        and creates new socket for that connection. 
        Client_addr gets filled with the address of the client doing the connect
        */

        int client_sockd = accept(listen_fd, &client_addr, &client_addrlen); 
        if(client_sockd<0){
            perror("accept() error");
        }

        if(client_sockd != -1){
            pthread_mutex_lock(&thread_data.queue_mutex);
            Enqueue(thread_data.taskQueue, reinterpret_cast<void *>(client_sockd));
            pthread_cond_signal(&thread_data.queue_cond);
            pthread_mutex_unlock(&thread_data.queue_mutex);
        }
    }

    return EXIT_SUCCESS;
}
