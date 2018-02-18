//Updated 6:48 Sunday
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
#include <sched.h>
#define VERSION 23
#define BUFSIZE 8096
#define ERROR      42
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404

//-------------------------------------//
//			Moshe Written Code	  	   //
//-------------------------------------//
struct arg_struct {
    int listenfd;
    int threadNumber;
}; 
void putIntoBuffer(void * input, int schedule);

//THREAD SETUP
pthread_t * threads;
int * threadSwitch; //determine if threads should be work or not
int maxNumThreads;
int numberOfChildThreadsInUse = 0;
int nextAvaliableThread = 0;

//BUFFER SETUP - for now just FIFO
#define ANY 0
#define FIFO 1
#define HPIC 2
#define HPHC 3
struct request_Struct{
	char * buffer;
	int file_fd;
	int fd;
	int hit;
	char * fstr;//file extension ex: .txt
};
struct request_Struct * buffer_Structs;
int putInBuff = 0;
int takeFromBuff = 0;
int buffers;//number of buffers
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
struct request_Struct parseInput(int fd, int hit)
{
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
		if( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) { //Compare the end of the string i.e the file extension
			fstr =extensions[i].filetype;
			break;
		}
	}
	if(fstr == 0) logger(FORBIDDEN,"file extension type not supported",buffer,fd);

	if(( file_fd = open(&buffer[5],O_RDONLY)) == -1) {  /* open the file for reading */
		logger(NOTFOUND, "failed to open file",&buffer[5],fd);
	}
	
	//--------------------------------------------------//
	//	Here's Where I step into the Critical Region	//
	//--------------------------------------------------//
	// 1] Put the info into the buffer Based on scheduling
	// 2] Take out of the buffer based on scheduling
	// 3] Exit the critical Region
	
	//PUT INTO BUFFER
	struct request_Struct newBuf;
	newBuf.buffer = buffer;
	newBuf.file_fd = file_fd;
	newBuf.fd = fd;
	newBuf.hit = hit;
	newBuf.fstr = fstr;
	/* Moved this to another method - hopefully it works
	if(putInBuff == buffers) putInBuff = 0; // make array into circular queue
	buffer_Structs[putInBuff++] = newBuf;
	*/
	return newBuf;
	//Above this is called from the parent
	}
	
	/*
	 * Attempting to decouple things a bit - hopefully this works
	 */
	void putIntoBuffer(void * input, int schedule){
		//TODO - Manage schedualing
		struct request_Struct *newBuf = input;
		if(putInBuff == buffers) putInBuff = 0; // make array into circular queue
		buffer_Structs[putInBuff++] = *newBuf;
	}

struct request_Struct takeFromBuffer()
{
	//TODO - Manage scheduling
	if(takeFromBuff == buffers) takeFromBuff = 0; // make array into circular queue
	struct request_Struct bufToUse = buffer_Structs[takeFromBuff++];
	return bufToUse;
}

/*
 * The child thread code
 */
void * child(void * input){ // for now runChild calls child, but I'm leaving it setup in case I want to call it directly from pthread_create
	int threadNum = *(int *)input;
	printf("\nChild #%d was created.", threadNum);
	fflush(stdout);
	while(1){
		while(threadSwitch[threadNum] == 0) sched_yield();
		long len, ret;
		//TAKE FROM BUFFER
		//TODO LOCK THIS
		struct request_Struct bufToUse = takeFromBuffer();
		//TODO UNLOCK THIS
	
		/* Decoupled it
		if(takeFromBuff == buffers) takeFromBuff = 0; // make array into circular queue
		struct request_Struct bufToUse = buffer_Structs[takeFromBuff++];
		*/
		// Done with critical region //
	
		logger(LOG,"SEND",&bufToUse.buffer[5],bufToUse.hit);
		len = (long)lseek(bufToUse.file_fd, (off_t)0, SEEK_END); /* lseek to the file end to find the length */
	   	   (void)lseek(bufToUse.file_fd, (off_t)0, SEEK_SET); /* lseek back to the file start ready for reading */
      	    (void)sprintf(bufToUse.buffer,"HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, bufToUse.fstr); /* Header + a blank line */
		logger(LOG,"Header",bufToUse.buffer,bufToUse.hit);
		dummy = write(bufToUse.fd,bufToUse.buffer,strlen(bufToUse.buffer));
	
  	  /* Send the statistical headers described in the paper, example below
    
  	  (void)sprintf(buffer,"X-stat-req-arrival-count: %d\r\n", xStatReqArrivalCount);
		dummy = write(fd,buffer,strlen(buffer));
  	  */
    
   	 /* send file in 8KB block - last block may be smaller */
		while (	(ret = read(bufToUse.file_fd, bufToUse.buffer, BUFSIZE)) > 0 ) {
			dummy = write(bufToUse.fd,bufToUse.buffer,ret);
		}
		sleep(1);	/* allow socket to drain before signalling the socket is closed */
		close(bufToUse.fd);
		//exit(1); //this is no longer a process so we cannot exit
	}
	return NULL;
}

void runChild(int listenfd, int childNum){
	(void)close(listenfd);
	threadSwitch[childNum] = 1; //turn on the child
}

/*
 * If there are no free threads, yield
 * If there are free threads, return the next one
 */
int getNextAvailableThread(){
	while(numberOfChildThreadsInUse == maxNumThreads) sched_yield();
	int next = nextAvaliableThread++%maxNumThreads;
	numberOfChildThreadsInUse++;
	return next;
}

int main(int argc, char **argv)
{
	//printf("Moshe's code\n\n\n\n");
	int i, port, /*pid,*/ listenfd, socketfd, hit;
	socklen_t length;
	static struct sockaddr_in cli_addr; /* static = initialised to zeros */
	static struct sockaddr_in serv_addr; /* static = initialised to zeros */

	if( argc < 6  || argc > 6 || !strcmp(argv[1], "-?") ) {
		(void)printf("hint: nweb Port-Number Top-Directory\t\tversion %d\n\n"
	"\tnweb is a small and very safe mini web server\n"
	"\tnweb only servers out file/web pages with extensions named below\n"
	"\t and only from the named directory or its sub-directories.\n"
	"\tThere is no fancy features = safe and secure.\n\n"
	"\tExample: nweb 8181 /home/nwebdir &\n\n"
	"\tOnly Supports:", VERSION);
		for(i=0;extensions[i].ext != 0;i++)
			(void)printf(" %s",extensions[i].ext);

		(void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
	"\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
	"\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n"  );
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
printf("\nGot to here");
fflush(stdout);
	for(i=0;i<32;i++)
		(void)close(i);		/* close open files */
	(void)setpgrp();		/* break away from process group */
printf("\nGot to here2");
fflush(stdout);
	//------------------------------------------//
	//	 Start: Moshe's Edits to input scanning	//
	//------------------------------------------//
	//				argv[0]	  argv[1]	argv[2]	  argv[3]	argv[4]		argv[5]
	// Old Version: ./server [portnum] [folder] &
	// New Version: ./server [portnum] [folder] [threads] [buffers] [schedalg] &
	//THREAD SETUP
	int maxNumThreads = atoi(argv[3]);
	if(maxNumThreads < 1){ 
		printf("Invalid number of threads");
		exit(1);
	}
	threads		 = malloc(sizeof(pthread_t) * maxNumThreads);
	threadSwitch = malloc(sizeof(int ) 		* maxNumThreads);
	//BUFFER SETUP
	buffers = atoi(argv[4]);
	if(buffers < 1){ 
		printf("Invalid number of buffers");
		exit(1);
	}
	//SCHEDULE  SETUP 
	int schedule; 
	if(!strcmp(argv[5], "ANY")) schedule = ANY;
	else if(!strcmp(argv[5], "FIFO")) schedule = FIFO;
	else if(!strcmp(argv[5], "HPIC")) schedule = HPIC;
	else if(!strcmp(argv[5], "HPHC")) schedule = HPHC;
	else{
		printf("Invalid scheduling parameter. Options are: \"ANY\", \"FIFO\", \"HPIC\", \"HPHC\".");
		exit(1);
	}
	//See older code for a diff. way to do buffers
	buffer_Structs = malloc(sizeof(struct request_Struct) * buffers);
	printf("Input was: \nargv[0] = %s, argv[1] = %s, argv[2] = %s, argv[3] = %s, argv[4] = %s, argv[5] = %s\n", 
		argv[0],  argv[1],  argv[2],  argv[3], argv[4], argv[5]);
	fflush(stdout);
	//------------------------------------------//
	//	 End: Moshe's Edits to input scanning	//
	//------------------------------------------//
	logger(LOG,"nweb starting",argv[1],getpid());
	/* setup the network socket */
	if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
		logger(ERROR, "system call","socket",0);
	port = atoi(argv[1]);
	if(port < 0 || port >60000)
		logger(ERROR,"Invalid port number (try 1->60000)",argv[1],0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);
	if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
		logger(ERROR,"system call","bind",0);
	if( listen(listenfd,64) <0)
		logger(ERROR,"system call","listen",0);
	for(hit=1; ;hit++) {
		length = sizeof(cli_addr);
		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			logger(ERROR,"system call","accept",0);
		
//----> Look Here TO Make Changes	
		//------------------//
		//		Plan		//
		//------------------//
		// 1] Start all the threads
		// 2] Take the input, and parse it and put it into the buffer_Structs based on scheduling
		// 3] Check if there are available threads, if:
		//		[a] yes - run that thread with the next available request
		//		[b] no  - yield until there is
		// 4] Start again
		
		//START ALL THE THREADS
		for(int i = 0; i < maxNumThreads; i++){
			int status;
			if((status = pthread_create(&threads[i], NULL, child, (void *)&i)) != 0)
				logger(ERROR,"system call","pthread_create",0);
		}
		//PARSE INPUT
		struct request_Struct newBuf = parseInput(socketfd, hit);
		//TODO   lock the buffer
		putIntoBuffer((void *)&newBuf, schedule);
		//TODO unlock the buffer
		//START WORKER CHILD THREAD
		int availableThreadNum = getNextAvailableThread();
		runChild(listenfd, availableThreadNum);
		//REPEAT
	}
}

/*
Questions I still need answers for:
1]	What "close"ing should I do in the children. Is that a remnant of multiprocessing code
	or is it still שייך?
2] 
*/