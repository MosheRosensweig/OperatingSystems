/* Generic */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>

/* Network */
#include <netdb.h>
#include <sys/socket.h>

#define BUF_SIZE 100

struct arg_struct {
    char* host;
    char* portnum;
    char* files[2];
    int hasSecondFile;
}; 

pthread_barrier_t myBarrier;

int FIFO = 0;

void *getThread(void * input);

sem_t mutex;

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
  
  int clientfd;
    
  char* filename;
  int fileupto = 0;
  
  
  while(1) {
      if (FIFO) sem_wait(&mutex);

      filename = args->files[fileupto];
      if (args->hasSecondFile) {
        fileupto = !fileupto;
      }
              
      // Establish connection with <hostname>:<port>
      clientfd = establishConnection(getHostInfo(args->host, args->portnum));
      if (clientfd == -1) {
        fprintf(stderr,
                "[main:73] Failed to connect to: %s:%s%s \n",
                args->host, args->portnum, filename);
        return NULL;
  
      }
    
        GET(clientfd, filename);    
    
        if (FIFO) sem_post(&mutex);
        
        pthread_barrier_wait(&myBarrier);    
    
        char buf[BUF_SIZE];
        while (recv(clientfd, buf, BUF_SIZE, 0) > 0) {
            fputs(buf, stdout);
            fflush(stdout);
            memset(buf, 0, BUF_SIZE);
        }
    
        close(clientfd);
    }
	return NULL;
}

int main(int argc, char **argv) {

  if (argc < 6) {
    fprintf(stderr, "USAGE: client [host] [portnum] [threads] [schedalg] [filename1] [filename2]\n");
    return 1;
  }
  
  
  int numberOfThreads = atoi(argv[3]);
	if(numberOfThreads < 1){ 
		printf("Invalid number of threads");
		exit(1);
	}
	pthread_t threads[numberOfThreads];
	
	if(!strcmp(argv[4], "FIFO")) FIFO = 1;
	
	pthread_barrier_init(&myBarrier, NULL, numberOfThreads);
    
    if (FIFO) sem_init(&mutex, 0, 1);
    
    struct arg_struct args;
    args.host = argv[1];
    args.portnum = argv[2];
    args.files[0] = argv[5];
    printf("%s", args.files[0]);
    args.hasSecondFile = 0; //Probs not needed.
    if (argc > 6) {
        args.hasSecondFile = 1;
        args.files[1] = argv[6];
    }
    
    for (int i = 0; i < numberOfThreads; i++) {
        pthread_create(&threads[i], NULL, getThread, (void*) &args);
    }
    while (1) sched_yield();
  return 0;
}
