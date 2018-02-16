/* Generic */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* Network */
#include <netdb.h>
#include <sys/socket.h>

#define BUF_SIZE 100


struct arg_struct {
    int clientfd;
    char* filename;
    int whichThread;
}; 
void *getThread(void * input);

// Get host information (used to establishConnection)
struct addrinfo *getHostInfo(char* host, char* port) {
  int r;
  struct addrinfo hints, *getaddrinfo_res;
  // Setup hints
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if ((r = getaddrinfo(host, port, &hints, &getaddrinfo_res))) {
    fprintf(stderr, "[getHostInfo:21:getaddrinfo] %s\n", gai_strerror(r));
    return NULL;
  }

  return getaddrinfo_res;
}

// Establish connection with host
int establishConnection(struct addrinfo *info) {
  if (info == NULL) return -1;

  int clientfd;
  for (;info != NULL; info = info->ai_next) {
    if ((clientfd = socket(info->ai_family,
                           info->ai_socktype,
                           info->ai_protocol)) < 0) {
      perror("[establishConnection:35:socket]");
      continue;
    }

    if (connect(clientfd, info->ai_addr, info->ai_addrlen) < 0) {
      close(clientfd);
      perror("[establishConnection:42:connect]");
      continue;
    }

    freeaddrinfo(info);
    return clientfd;
  }

  freeaddrinfo(info);
  return -1;
}


// Send GET request
void GET(int clientfd, char *path) {
  char req[1000] = {0};
  sprintf(req, "GET %s HTTP/1.0\r\n\r\n", path);
  send(clientfd, req, strlen(req), 0);
}

void * getThread(void * input){
	
	struct arg_struct *args = input;
	int clientfd = args->clientfd;
	char * filename = args->filename;
	
	printf("\n\nThread #%d state: %d, %s\n\n", args->whichThread, clientfd, filename);
    
    GET(clientfd, filename);
    
    char buf[BUF_SIZE];
    while (recv(clientfd, buf, BUF_SIZE, 0) > 0) {
    fputs(buf, stdout);
    memset(buf, 0, BUF_SIZE);
    }
	return NULL;
}

int main(int argc, char **argv) {
  int clientfd;

  if (argc < 6) {
    fprintf(stderr, "USAGE: client [host] [portnum] [threads] [schedalg] [filename1] [filename2]\n");
    return 1;
  }
  

  // Establish connection with <hostname>:<port>
  clientfd = establishConnection(getHostInfo(argv[1], argv[2]));
  if (clientfd == -1) {
    fprintf(stderr,
            "[main:73] Failed to connect to: %s:%s%s \n",
            argv[1], argv[2], argv[3]);
    return 3;
  }
  
  int numberOfThreads = atoi(argv[3]);
	if(numberOfThreads < 1){ 
		printf("Invalid number of threads");
		exit(1);
	}
	pthread_t threads[numberOfThreads];
	int threadNum = -1; //intentionally set to -1

  for (int i = 5; i < argc; i++) {
    struct arg_struct args;
    args.whichThread = i - 5;
    args.clientfd = clientfd;
    args.filename = argv[i];
    pthread_create(&threads[++threadNum], NULL, getThread, (void *)&args);
    pthread_join(threads[threadNum], NULL); //TODO: Waiting for multiple threads.
  }
  
  close(clientfd);
  
  return 0;
}
