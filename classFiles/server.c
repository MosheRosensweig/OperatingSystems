//Updated 3:05 - This is to test the sever on my own
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#define VERSION 23
#define BUFSIZE 8096
#define ERROR      42
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404

//---------------
//  Micah Code
// ------------------

sem_t mutex;
sem_t emptySlots;
sem_t fullSlots;

//NOTE: I changed the code to semaphore version and deleted (I think)
//all obsolete code. I did not delete obsolete comments because that's
//a bigger job. Primary edits were in the buffer methods but the clean up
//was everywhere. The buffer methods currently won't work because they need
//to implement scanning for slots.


//-------------------------------------//
//			Moshe Written Code	  	   //
//-------------------------------------//

//THREAD SETUP
pthread_t * threads;
int maxNumThreads;

//BUFFER SETUP - for now just FIFO
#define ANY 0
#define FIFO 1
#define HPIC 2
#define HPHC 3
int schedule;
// The request struct now only needs to hold the fd and the hit number
// So when the child thread takes it out it can get access to that info
// I'm also storing the file - type even though we only need to know
// that in order to parse it, we won't need it afterwards, but for now
// why not
struct request_Struct{
	int file_fd;
	int hit;
	int fstr;//file extension ex: .txt //0 if html, 1 if picture
};
struct request_Struct * buffer_Structs;//the only buffer if there no priority, the html buffer if there is
struct request_Struct * buffer_StructsPIC;//If there is a priority, this is the picture buffer
int putInBuff		  = 0;
int takeFromBuff	  = 0;
int putInPicBuff	  = 0;
int takeFromPicBuff	  = 0;
int numOfReqsInBuf 	  = 0; //These two are only needed to speed up HPIC/HPHC, but technically the
int numOfReqsInPicBuf = 0; //semaphore is enough. These are basically queue.size() in java terms
int buffers;//number of buffers

//TODO implement these
struct request_Struct parseInput(int socketfd, int hit);
void putIntoBuffer(void * input, int schedule);
struct request_Struct takeFromBuffer(); //TODO this needs to be done right
//-------------------------------------//
struct {
	char *ext;
	char *filetype;
} extensions [] = {
	{"gif", "image/gif" },  
	{"jpg", "image/jpg" }, 
	{"jpeg","image/jpeg"},
	{"png", "image/png" },  
	{"ico", "image/ico" },  
	{"zip", "image/zip" },  
	{"gz",  "image/gz"  },  
	{"tar", "image/tar" },  
	{"htm", "text/html" },  
	{"html","text/html" },  
	{0,0} };

static int dummy; //keep compiler happy

void logger(int type, char *s1, char *s2, int socket_fd)
{
	int fd ;
	char logbuffer[BUFSIZE*2];

	switch (type) {
	case ERROR: (void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",s1, s2, errno,getpid()); 
		break;
	case FORBIDDEN: 
		dummy = write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n",271);
		(void)sprintf(logbuffer,"FORBIDDEN: %s:%s",s1, s2); 
		break;
	case NOTFOUND: 
		dummy = write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n",224);
		(void)sprintf(logbuffer,"NOT FOUND: %s:%s",s1, s2); 
		break;
	case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",s1, s2,socket_fd); break;
	}	
	/* No checks here, nothing can be done with a failure anyway */
	if((fd = open("nweb.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
		dummy = write(fd,logbuffer,strlen(logbuffer)); 
		dummy = write(fd,"\n",1);      
		(void)close(fd);
	}
	if(type == ERROR || type == NOTFOUND || type == FORBIDDEN) exit(3);
}

/* this is a child web server process, so we can exit on errors */
void * web(void * input)
{
	while(1){
	struct request_Struct bufToUse = takeFromBuffer();
	int fd  = bufToUse.file_fd;
	int hit = bufToUse.hit;
	
	int j, file_fd, buflen;
	long i, ret, len;
	char * fstr;
	static char buffer[BUFSIZE+1]; /* static so zero filled */

	ret =read(fd,buffer,BUFSIZE); 	/* read Web request in one go */
	if(ret == 0 || ret == -1) {	/* read failure stop now */
		logger(FORBIDDEN,"failed to read browser request","",fd);
	}
	if(ret > 0 && ret < BUFSIZE)	/* return code is valid chars */
		buffer[ret]=0;		/* terminate the buffer */
	else buffer[0]=0;
	for(i=0;i<ret;i++)	/* remove CF and LF characters */
		if(buffer[i] == '\r' || buffer[i] == '\n')
			buffer[i]='*';
	logger(LOG,"request",buffer,hit);
	if( strncmp(buffer,"GET ",4) && strncmp(buffer,"get ",4) ) {
		logger(FORBIDDEN,"Only simple GET operation supported",buffer,fd);
	}
	for(i=4;i<BUFSIZE;i++) { /* null terminate after the second space to ignore extra stuff */
		if(buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
			buffer[i] = 0;
			break;
		}
	}
	for(j=0;j<i-1;j++) 	/* check for illegal parent directory use .. */
		if(buffer[j] == '.' && buffer[j+1] == '.') {
			logger(FORBIDDEN,"Parent directory (..) path names not supported",buffer,fd);
		}
	if( !strncmp(&buffer[0],"GET /\0",6) || !strncmp(&buffer[0],"get /\0",6) ) /* convert no filename to index file */
		(void)strcpy(buffer,"GET /index.html");

	/* work out the file type and check we support it */
	buflen=strlen(buffer);
	fstr = (char *)0;
	for(i=0;extensions[i].ext != 0;i++) {
		len = strlen(extensions[i].ext);
		if( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
			fstr =extensions[i].filetype;
			break;
		}
	}
	if(fstr == 0) logger(FORBIDDEN,"file extension type not supported",buffer,fd);

	if(( file_fd = open(&buffer[5],O_RDONLY)) == -1) {  /* open the file for reading */
		logger(NOTFOUND, "failed to open file",&buffer[5],fd);
	}
	logger(LOG,"SEND",&buffer[5],hit);
	len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* lseek to the file end to find the length */
	      (void)lseek(file_fd, (off_t)0, SEEK_SET); /* lseek back to the file start ready for reading */
          (void)sprintf(buffer,"HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr); /* Header + a blank line */
	logger(LOG,"Header",buffer,hit);
	dummy = write(fd,buffer,strlen(buffer));
	
    /* Send the statistical headers described in the paper, example below
    
    (void)sprintf(buffer,"X-stat-req-arrival-count: %d\r\n", xStatReqArrivalCount);
	dummy = write(fd,buffer,strlen(buffer));
    */
    
    /* send file in 8KB block - last block may be smaller */
	while (	(ret = read(file_fd, buffer, BUFSIZE)) > 0 ) {
		dummy = write(fd,buffer,ret);
	}
	sleep(1);	/* allow socket to drain before signalling the socket is closed */
	close(fd);
	//TODO - implement semaphore version of the waiting... or my old version works too
	}//end of while loop
	return NULL;
}

//--------------------------//
//----- Moshe's Methods ----//
//--------------------------//

/*
 * Find the file type and return 
 */
struct request_Struct parseInput(int fd, int hit)
{
	printf("\nParse Begin!\n"); fflush(stdout);
	struct request_Struct newBuf;
	newBuf.file_fd = fd;
	newBuf.hit = hit;
	
	//TODO fix - Micah's code doesn't work
	newBuf.fstr = 0;
	
	/*
	static char buffer[BUFSIZE]; //could be smaller.
	int ret, buflen, len, i, j;
	j = 0;
	char* fstr;
	
	while ((ret = read(fd, buffer, 1))) {
	    if (!strncmp(buffer + (++j), "\n", 1)) {
	        break;
	    }
	}
	
	if(ret == 0 || ret == -1) {	//read failure stop now 
		logger(FORBIDDEN,"failed to read browser request","",fd);
	}
	
	buflen=strlen(buffer);
	fstr = (char *)0;
	for(i=0;extensions[i].ext != 0;i++) {
		len = strlen(extensions[i].ext);
		if( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
			fstr =extensions[i].filetype;
			if (i <= 4) {
			    newBuf.fstr = 1; //It's an image.
			}
			else {
			    newBuf.fstr = 0;
			}
			break;
		}
	}
	if(fstr == 0) logger(FORBIDDEN,"file extension type not supported",buffer,fd);
    */
    printf("\nParse End!\n"); fflush(stdout);
	return newBuf;
}

/*
 * Take the request, and based on scheduling, find an empty spot and put it in the buffer
 * In event of HPIC/HPHC, even though I have two buffers of size[buffer], the semaphore
 * will make sure I don't put in too many jobs across both buffers
 * How do I know which is the next available buffer? See code, it uses two pointers...
 * The takeFromBuff will never pass the putInBuff because of the semaphores
 */
void putIntoBuffer(void * input, int schedule)
{
	printf("Beginning of putInBuff number 0\n");
	//TODO - Manage schedualing
	struct request_Struct *newBuf = input;
	sem_wait(&mutex); 								//Check if can acess critical reigion.
	printf("Beginning of putInBuff number 1\n");
	sem_wait(&emptySlots); 							//See if can lower the number of empty spots. (i.e. not equal 0)
	printf("Beginning of putInBuff number 2\n");
	struct request_Struct req = *newBuf; 			//deference the request
	if(schedule == ANY || schedule == FIFO || req.fstr == 0){
		//If it's non-priority or it's HPIC/HPHC, but it's an html
		buffer_Structs[putInBuff%buffers] = req;
		putInBuff++;								//separated to be a bit clearer 
		numOfReqsInBuf++; 							//queue.size++
	}
	else{ //it's HPIC/HPHC and it's a picture
		buffer_StructsPIC[putInPicBuff%buffers] = req;
		putInPicBuff++;
		numOfReqsInPicBuf++;
	}
	sem_post(&fullSlots); 							//Raise the number of full slots.
	sem_post(&mutex);	 							//Indicate that someone else can get it.
}		

/*
 * Based on scheduling take from buffer
 * I have the following 2 extra counters: numOfReqsInBuf & numOfReqsInPicBuf so that I 
 * can quickly do HPIC/HPHC see below
 */
struct request_Struct takeFromBuffer()
{
	//TODO - Manage scheduling 								//See comments from putIntoBuffer
	sem_wait(&fullSlots);
	printf("\nTake from start\n");
	//sem_wait(&mutex);
	struct request_Struct bufToUse;
	switch(schedule){
	case ANY:
	case FIFO:
		bufToUse = buffer_Structs[takeFromBuff%buffers];
		takeFromBuff++; 							//separated for clarity
		numOfReqsInBuf--;							//queue.size--
		break;
	case HPHC: 										//html is prioritized 
		if(numOfReqsInBuf > 0){
			bufToUse = buffer_Structs[takeFromBuff%buffers];
			takeFromBuff++; 							
			numOfReqsInBuf--;							
		}
		else{
			bufToUse = buffer_Structs[takeFromPicBuff%buffers];
			takeFromPicBuff++; 							
			numOfReqsInPicBuf--;
		}
		break;
	case HPIC:										//pictures are prioritized
		if(numOfReqsInPicBuf > 0){
			bufToUse = buffer_Structs[takeFromPicBuff%buffers];
			takeFromPicBuff++; 							
			numOfReqsInPicBuf--;										
		}
		else{
			bufToUse = buffer_Structs[takeFromBuff%buffers];
			takeFromBuff++; 							
			numOfReqsInBuf--;
		}
		break;
	}
	sem_post(&emptySlots);
	//sem_post(&mutex);//TODO confirm that Micah Left this out by accident
	return bufToUse;
}

int main(int argc, char **argv)
{
	int i, port, listenfd, socketfd, hit;
	socklen_t length;
	static struct sockaddr_in cli_addr; /* static = initialised to zeros */
	static struct sockaddr_in serv_addr; /* static = initialised to zeros */

	if( argc < 6  || argc > 6 || !strcmp(argv[1], "-?") ) {
		(void)printf("USAGE:./server [portnum] [folder] [threads] [buffers] [schedalg] &\n");
		exit(0);
	}
	if( !strncmp(argv[2],"/"   ,2 ) || !strncmp(argv[2],"/etc", 5 ) ||
	    !strncmp(argv[2],"/bin",5 ) || !strncmp(argv[2],"/lib", 5 ) ||
	    !strncmp(argv[2],"/tmp",5 ) || !strncmp(argv[2],"/usr", 5 ) ||
	    !strncmp(argv[2],"/dev",5 ) || !strncmp(argv[2],"/sbin",6) ){
		(void)printf("ERROR: Bad top directory %s, see nweb -?\n",argv[2]);
		exit(3);
	}
	if(chdir(argv[2]) == -1){ 
		(void)printf("ERROR: Can't Change to directory %s\n",argv[2]);
		exit(4);
	}
	/* Become deamon + unstopable and no zombies children (= no wait()) */
	if(fork() != 0)
		return 0; /* parent returns OK to shell */
	(void)signal(SIGCHLD, SIG_IGN); /* ignore child death */
	(void)signal(SIGHUP, SIG_IGN); /* ignore terminal hangups */
	for(i=2;i<32;i++)//TODO set this back to 0
		(void)close(i);		/* close open files */
	(void)setpgrp();		/* break away from process group */
	logger(LOG,"nweb starting",argv[1],getpid());
	/* setup the network socket */
	if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
		logger(ERROR, "system call","socket",0);
	port = atoi(argv[1]);
	if(port < 0 || port >60000)
		logger(ERROR,"Invalid port number (try 1->60000)",argv[1],0);
	//------------------------------------------//
	//	 Start: Moshe's Edits to input scanning	//
	//------------------------------------------//
	//				argv[0]	  argv[1]	argv[2]	  argv[3]	argv[4]		argv[5]
	// Old Version: ./server [portnum] [folder] &
	// New Version: ./server [portnum] [folder] [threads] [buffers] [schedalg] &
	//THREAD SETUP
	

	maxNumThreads = atoi(argv[3]);
	if(maxNumThreads < 1){ 
		printf("Invalid number of threads");
		exit(1);
	}
	threads		 = malloc(sizeof(pthread_t) * maxNumThreads);
	//BUFFER SETUP
	buffers = atoi(argv[4]);
	if(buffers < 1){ 
		printf("Invalid number of buffers");
		exit(1);
	}
	//SCHEDULE  SETUP 
	if(!strcmp(argv[5], "ANY")) schedule = ANY;
	else if(!strcmp(argv[5], "FIFO")) schedule = FIFO;
	else if(!strcmp(argv[5], "HPIC")) schedule = HPIC;
	else if(!strcmp(argv[5], "HPHC")) schedule = HPHC;
	else{
		printf("Invalid scheduling parameter. Options are: \"ANY\", \"FIFO\", \"HPIC\", \"HPHC\".");
		exit(1);
	}
	
	//See older code for a diff. way to do buffers
	buffer_Structs 	  = malloc(sizeof(struct request_Struct) * buffers);
	buffer_StructsPIC = malloc(sizeof(struct request_Struct) * buffers);
	//MICAH ADDED CODE
	sem_init(&mutex, 0, 1);
	sem_init(&emptySlots, 0, buffers);
	sem_init(&fullSlots, 0, 0);
	
	//------------------------------------------//
	//	 End: Moshe's Edits to input scanning	//
	//------------------------------------------//
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);
	if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
		logger(ERROR,"system call","bind",0);
	if( listen(listenfd,64) <0)
		logger(ERROR,"system call","listen",0);
		
	//START ALL THE THREADS
	for(int j = 0; j < maxNumThreads; j++){
		int status;
		if((status = pthread_create(&threads[j], NULL, web, NULL)) != 0)
			logger(ERROR,"system call","pthread_create",0);
	}
	
	for(hit=1; ;hit++) {
		length = sizeof(cli_addr);
		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			logger(ERROR,"system call","accept",0);	
		//------------------//
		//		Plan		//
		//------------------//
		// 1] Start all the threads - above
		// 2] Take the input, and parse it and put it into the buffer_Structs based on scheduling
		// 3] Check if there are available threads, if:
		//		[a] yes - run that thread with the next available request
		//		[b] no  - yield until there is
		// 4] Start again

		//PARSE INPUT
		struct request_Struct newReq = parseInput(socketfd, hit); //TODO make this work!
		putIntoBuffer((void *)&newReq, schedule);
		printf("putIntoBuffer Passed!\n");
		//START WORKER CHILD THREAD
		//sleep(2); //TODO Why the sleep, I know it's a question. 
		/*	
		if((pid = fork()) < 0) {
			logger(ERROR,"system call","fork",0);
		}
		else {
			if(pid == 0) { 	// child 
				(void)close(listenfd);
				web(socketfd,hit); // never returns 
			} else { 	// parent 
				(void)close(socketfd);
			}
		}*/
	}
}
