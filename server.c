#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <fcntl.h> 
#include <sys/stat.h>
#include <sys/poll.h>
#include <time.h>

#define BUFSIZE 1024
void *client_handler(void* client_pointer);
void file_send(int client, char* filename, int http_10);
void send_headers(int client, char* filename);
void send_content_type(int client, char* output, int output_size);
void close_client(int client);
void send_date(int client);
int client_count = 0;
typedef struct client_handler_args {
  int fd;
} Client_Args;
char* doc_root;


int main(int argc, char **argv) {
  struct sockaddr_in myaddr;
  int sock;
  socklen_t clientlen;
  struct sockaddr clientaddr;
  int client;
  pthread_t tid;
  int portNum;


  if (argc == 1) {
    portNum = 8886;
    doc_root = "\0";
  } else if (argc == 3) {
    if (!strcmp(argv[1],"-port")) {
      portNum = atoi(argv[2]);
      doc_root = "\0";
    } else if (!strcmp(argv[1],"-document_root")) {
      doc_root = malloc(BUFSIZE * sizeof(char));
      strcpy(doc_root,argv[2]);
      portNum = 8886;
    } else {
      printf("Not enough command line arguments\n");
      exit(1);
    }
  } else if (argc == 5) {
    if (!strcmp(argv[1],"-port")) {
      portNum = atoi(argv[2]);
      if (!strcmp(argv[3],"-document_root")) {
        doc_root = malloc(BUFSIZE * sizeof(char));
        strcpy(doc_root,argv[4]);
      } else {
        printf("Not enough command line arguments\n");
        exit(1);
      }
    } else if (!strcmp(argv[1],"-document_root")) {
      doc_root = malloc(BUFSIZE * sizeof(char));
      strcpy(doc_root,argv[2]);
      if (!strcmp(argv[3],"-port")) {
        portNum = atoi(argv[4]);
      } else {
        printf("Not enough command line arguments\n");
        exit(1);
      }
    } else {
      printf("Not enough command line arguments\n");
      exit(1);
    }
  } else {
    printf("%d Not enough command line arguments\n",argc);
    exit(1);
  }
  if (strlen(doc_root) > 0) {
    if (doc_root[0] == '/') {
      doc_root++;
    } if (doc_root[strlen(doc_root)-1] != '/') {
      strcat(doc_root,"/");
    }
  }
  //Opens a socket on a given port
  sock = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
  myaddr.sin_port = htons(portNum);
  myaddr.sin_family = AF_INET;
  myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  //binds the socket
  if (bind(sock,(struct sockaddr*)&myaddr,sizeof(myaddr)) < 0) {
    printf("failed to bind");
    exit(1);
  }

  //Starts listening on the socket
  if (listen(sock,15)<0) {
    printf("Failed to listen\n");
    exit(1);
  }

  //Infinite loop where our server is waiting for connections
  while (1) {
    clientlen = sizeof(clientaddr);

    //Accepts and processes a client
    client = accept(sock,(struct sockaddr *)&clientaddr, &clientlen);
    if (client < 0) {
      printf("failed to accept\n");
      continue;
    }
    printf("Client connected\n");
    client_count += 1;
    Client_Args* c = malloc(sizeof(Client_Args));
    c->fd = client;

    //Starts a thread to handle our client
    if (pthread_create(&tid,NULL,client_handler,c) != 0) {
      printf("failed to make thread \n");
      close_client(client);
      continue;
    }
  }  
}

void *client_handler(void* client_pointer) {
  int message_buf_size = 8192;
  char buf[message_buf_size];
  int recVal;
  int sendVal;
  Client_Args client_struct = *((Client_Args*)client_pointer);
  int client = client_struct.fd;
  int http_10 = 1;
  int bad_request = 0;
  char* filename = malloc(BUFSIZE*sizeof(char));
  //Whole thread in an infite loop to allow for persistence
  while (1) {
    bzero(buf,BUFSIZE);
    
    //Sets a timeout for our client to send us a message
    struct timeval tv;
    if (15 - (client_count/10) >= 5) {
      tv.tv_sec =  15 - (client_count/10);
    } else {
      tv.tv_sec = 5;
    }
    tv.tv_usec = 0;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    
    //Receives a message from client (buf contains the message)
    recVal = recv(client,buf,message_buf_size,0);

    //Checks for a timeout, ends thread if it happens
    if (recVal < 0) {
      close_client(client);
      printf("TIMEOUT\n");
      free(filename);
      return NULL;
    }

    //finds the index where the string ends
    int last_char;
    for (int i = 0; i < message_buf_size; i++) {
      if (buf[i] == '\0') {
	last_char = i;
	break;
      }
    }

    //Ensures that input ends with two returns, waits for another response if it does not
    //Have to check for "\n" and "\r\n" options
    if (buf[last_char-1] == '\n' || buf[last_char-1] == '\r') {
      if (buf[last_char-1] == '\n' && buf[last_char-2] == '\r') {
	if ((buf[last_char-3] != '\n' || buf[last_char-4] !=  '\r') && (buf[last_char-3] != '\r') && (buf[last_char-3] != '\n')) {
	  char new_buf[BUFSIZE];
	  recVal = recv(client,new_buf,BUFSIZE,0);
	  for (int i = 0; i < BUFSIZE; i++) {
	    if (new_buf[i] == '\0') {
	    buf[last_char + i] = '\0';
	    break;
	    } else {
	      buf[last_char + i] = new_buf[i];
	    }
	  }
	}
      } else {
	if(buf[last_char-2] != '\n' || buf[last_char-2] == '\r') {
	  char new_buf[BUFSIZE];
	  recVal = recv(client,new_buf,BUFSIZE,0);
	  for (int i = 0; i < BUFSIZE; i++) {
	    if (new_buf[i] == '\0') {
	      buf[last_char + i] = '\0';
	      break;
	    } else {
	      buf[last_char + i] = new_buf[i];
	    }
	  }
	} else {
	  send(client, "HTTP/1.0 400 Bad Request\n",strlen("HTTP/1.0 400 Bad Request"),0);
	  send_date(client);
	  send(client,"Content-Type: text/plain\n",strlen("Content-Type: text/plain\n"),0);
	  send(client,"Content-Length: 16\r\n\n",strlen("Content-Length: 14\r\n\n"),0);
	  send(client,"400 Bad Request\n",strlen("400 Bad Request\n"),0);
	  close_client(client);
	  free(filename);
	  return NULL;
	}
      }
    }
    printf("message received %s\n",buf);

    //Checks we are receiving a GET request
    for (int i = 0; i < 4; i++) {
      if (i == 0 && buf[i] != 'G') {
	bad_request = 1;
	break;
      } else if (i == 1 && buf[i] != 'E') {
	bad_request = 1;
	break;
      } else if (i == 2 && buf[i] != 'T') {
	bad_request = 1;
	break;
      } else if (i == 3 && buf[i] != ' ') {
	bad_request = 1;
	break;
      }
    }
    //If error in the request, raise an error
    if (bad_request) {
      send(client, "HTTP/1.0 400 Bad Request\n",strlen("HTTP/1.0 400 Bad Request\n"),0);
      send_date(client);
      send(client,"Content-Type: text/plain\n",strlen("Content-Type: text/plain\n"),0);
      send(client,"Content-Length: 16\r\n\n",strlen("Content-Length: 14\r\n\n"),0);
      send(client,"400 Bad Request\n",strlen("400 Bad Request\n"),0);
      //Close the client since we don't yet know if this is a 1.1 or 1.0 request
      close_client(client);
      free(filename);
      return NULL;
    }

    //Finds the end of the filename in the request
    int end_file_name = -1;
    for(int i = 4; i<message_buf_size; i++){
      
      if(buf[i]!= ' '){
        filename[i-4] = buf[i];
    }
      else{
        filename[i-4] = '\0';
	end_file_name = i;
        break;
      }
      
    }

    //Checks for if we are getting a 1.0 or 1.1 request
    char reference[9] = " HTTP/1.0";
    for (int i = end_file_name; i < end_file_name + 9; i++) {
      if (i == end_file_name + 8) {
	if (buf[i] == '0') {
	  http_10 = 1;
	} else if (buf[i] == '1') {
	  http_10 = 0;
	} else {
	  bad_request = 1;
	  break;
	}
      } else {
	if (reference[i-end_file_name] != buf[i]) {
	  bad_request = 1;
	  break;
	}
      }
    }

    //Checks for if a Host header is present
    char host[5];
    int host_present = 0;
    for (int i = 4; i < message_buf_size; i++) {
      if (i == 4) {
	host[0] = buf[0]; host[1] = buf[1]; host[2] = buf[2]; host[3] = buf[3]; host[4] = buf[4];
      } else {
	host[0] = host[1]; host[1] = host[2]; host[2] = host[3]; host[3] = host[4]; host[4] = buf[i];
      }
      if (buf[i] == '\0') {
	break;
      }
      if (host[0] == 'H' && host[1] == 'o' && host[2] == 's' && host[3] == 't' && host[4] == ':') {
	host_present = 1;
	break;
      }
    }

    //Checks for if we have gotten a bad request yet, returns an error if we have
    if (bad_request) {
      send(client, "HTTP/1.0 400 Bad Request\n",strlen("HTTP/1.0 400 Bad Request"),0);
      send_date(client);
      send(client,"Content-Type: text/plain\n",strlen("Content-Type: text/plain\n"),0);
      send(client,"Content-Length: 16\r\n\n",strlen("Content-Length: 14\r\n\n"),0);
      send(client,"400 Bad Request\n",strlen("400 Bad Request\n"),0);
      
      if (!http_10) {
	continue;
      }
      close_client(client);
      free(filename);
      return NULL;
    }

    //If making a 1.1 connection, requires a host header, returning an error if not present
    printf("buf %s \n",buf);
    if (!http_10 && !host_present) {
      send(client, "HTTP/1.1 400 Bad Request\n",strlen("HTTP/1.1 400 Bad Request"),0);
      send_date(client);
      send(client,"Content-Type: text/plain\n",strlen("Content-Type: text/plain\n"),0);
      send(client,"Content-Length: 16\r\n\n",strlen("Content-Length: 14\r\n\n"),0);
      send(client,"400 Bad Request\n",strlen("400 Bad Request\n"),0);
      continue;
    } else {

      //If we have passed all checks, send the file
      file_send(client,filename,http_10);

      //If we have a 1.0 connection, break the loop, ending the connection, otherwise we persist
      if (http_10) {
	break;
      }
    }
  }
  close_client(client);
  free(filename);
  return NULL;
}


void file_send(int client, char* part_filename, int http_10) {
  int fd, nread;
  char buf[1024];
  struct stat stat_buf;
  char* filename;

  //If we get a / request, make it into html. Makes sure filename doesn't start with '/'
  if (!strcmp(part_filename,"/index.html") | !strcmp(part_filename,"/")) {
    part_filename = "index.html";
  } else if (part_filename[0] == '/') {
    part_filename++;
  }

  //Verifies our client isn't accessing above the document root
  char last_seen = part_filename[0];
  for (int i = 1; i < strlen(part_filename); i++) {
    if (last_seen == '.' && part_filename[i] == '.') {
      if (http_10) {
	send(client, "HTTP/1.0 403 Forbidden\n",strlen("HTTP/1.0 403 Forbidden\n"),0);
      } else {
	send(client, "HTTP/1.1 403 Forbidden\n",strlen("HTTP/1.1 403 Forbidden\n"),0); }
      send_date(client);
      send(client,"Content-Type: text/plain\n",strlen("Content-Type: text/plain\n"),0);
      send(client,"Content-Length: 14\r\n\n",strlen("Content-Length: 14\r\n\n"),0);
      send(client,"403 Forbidden\n",strlen("403 Forbidden\n"),0);
      return;
    }
    last_seen = part_filename[i];
  }

  //Concatenates docroot and our file name
  filename = malloc(sizeof(char) * BUFSIZE * 2);
  if (strlen(doc_root) > 0) {
    strcpy(filename,doc_root);
    strcat(filename,part_filename);
  } else {
    strcpy(filename,part_filename);
  }
  printf("filename is %s \n",filename);
  //Opens a file descriptor for our requested file
  fd = open(filename,O_RDONLY);

  //If we failed to open the file, return a not found
  if (fd == -1) {
    if (http_10) {
      send(client, "HTTP/1.0 404 Not Found\n",strlen("HTTP/1.0 404 Not Found\n"),0);
    } else {
      send(client, "HTTP/1.1 404 Not Found\n",strlen("HTTP/1.1 404 Not Found\n"),0);
    }
    send_date(client);
    send(client,"Content-Type: text/plain\n",strlen("Content-Type: text/plain\n"),0);
    send(client,"Content-Length: 14\r\n\n",strlen("Content-Length: 14\r\n\n"),0);
    send(client,"404 Not Found\n",strlen("404 Not Found\n"),0);
    free(filename);
    return;
  }

  //Checks permissions on the requested file, returning forbidden if so
  stat(filename, &stat_buf);
  //And perms with 4 to see if we have the read permission for other users
  if (!(stat_buf.st_mode & 4)) {
    if (http_10) {
      send(client, "HTTP/1.0 403 Forbidden\n",strlen("HTTP/1.0 403 Forbidden\n"),0);
    } else {
      send(client, "HTTP/1.1 403 Forbidden\n",strlen("HTTP/1.1 403 Forbidden\n"),0); }
    send_date(client);
    send(client,"Content-Type: text/plain\n",strlen("Content-Type: text/plain\n"),0);
    send(client,"Content-Length: 14\r\n\n",strlen("Content-Length: 14\r\n\n"),0);
    send(client,"403 Forbidden\n",strlen("403 Forbidden\n"),0);
    free(filename);
    return;
  }
  
  //Have now checked for all errors, and haven't returned yet, means we can send back an ok status code
  if (!http_10) {
    send(client, "HTTP/1.1 100 Continue\r\n\r\n",strlen("HTTP/1.1 100 Continue\r\n\r\n"),0);
    send(client, "HTTP/1.1 200 OK\r\n", strlen("HTTP/1.1 200 OK\r\n"),0);
  } else {
    send(client, "HTTP/1.0 200 OK\r\n", strlen("HTTP/1.0 200 OK\r\n"),0);
  }
  printf("HTTP/1.0 200 OK\r\n");

  //Sends the headers
  send_headers(client,filename);

  //After headers are sent, send the file and close the file descriptor
  bzero(buf,sizeof(buf));
  while((nread = read(fd,buf,sizeof(buf))) > 0) {
    send(client,buf,nread,0);
    bzero(buf,sizeof(buf));
  }
  free(filename);
  printf("done sending\n");
  close(fd);
  
}

void send_headers(int client, char* filename) {
  int reached_dot;
  char* output = malloc(5 * sizeof(char));
  int nread, total_bytes, fd;
  char buf[BUFSIZE];

 //Send date and time GMT/UST 
  struct tm* ptr;
  time_t timeNow = time(NULL);
  ptr = gmtime(&timeNow);
  char timeString[64];
  strcpy(timeString, asctime(ptr));
  char dateString[128];
  strcpy(dateString, "Date: ");
  strcat(dateString,timeString);
  send(client,dateString,strlen(dateString),0);
  //Finds the requested file type, puts it in a string
  reached_dot = 0;
  int ind = 0;
  int string_index = strlen(filename) - 1;
  while (ind < 5) {
      if (filename[string_index--] == '.') {
	break;
      } else {
	output[ind++] = filename[string_index--];
      }
  }
  //Added the filename backwards to output, need to reverse it
  for (int i = 0; i < strlen(output)/2; i++) {
    char temp = output[i];
    output[i] = output[strlen(output)-i-1];
    output[strlen(output)-i-1] = temp;
  }
  
  //Parses and sends string containing file type
  send_content_type(client,output,strlen(output));
  total_bytes = 0;

  //Opens file descriptor
  fd = open(filename,O_RDONLY);

  //Counts number of bytes in file
  while((nread = read(fd,buf,sizeof(buf))) > 0) {
    total_bytes += nread;
  }
  //Send Content-Length: NUM_BYTES
  int digits = 0;
  int temp_bytes = total_bytes;
  while (temp_bytes > 0) {
    digits += 1;
    temp_bytes = temp_bytes / 10;
  }
  char cl_buf[(strlen("Content-Length: ")) + (strlen("\r\n\n"))  + digits];
  snprintf(cl_buf, 21+digits, "Content-Length: %d\r\n\n", total_bytes);

  //After generating a string, send it
  send(client,cl_buf,strlen(cl_buf),0);
  free(output);
}

void send_content_type(int client, char* output, int output_size) {
  int html_t = 0; int txt_t = 0; int jpg_t = 0; int gif_t = 0;
  char html[4] = "html"; char txt[3] = "txt"; char jpg[3] = "jpg"; char gif[3] = "gif";
  for (int i = 0; i < output_size; i++) {
    if (i == 0) {
      //Sets corresponding booleans based on what type of content it is
      if (output[i] == 'h') {
	html_t = 1;
      } else if (output[i] == 't') {
	txt_t = 1;
      } else if (output[i] == 'j') {
	jpg_t = 1;
      } else if (output[i] == 'g') {
	gif_t = 1;
      }
    } else {
      //Checks for if a given file type lines up with the string
      if (html_t && (html[i] != output[i])) {
	html_t = 0;
      } else {
	if (i>2) {
	  break;
	}
	if (txt_t && (txt[i] != output[i])) {
	  txt_t = 0;
	} else if	(jpg_t && (jpg[i] != output[i])) {
	  jpg_t = 0;
	} else if	(gif_t && (gif[i] != output[i])) {
	  gif_t = 0;
	}
      }
    }
  }
  if (html_t) {
    send(client,"Content-Type: text/html\r\n",strlen("Content-Type: text/html\r\n"),0);
  } else if (txt_t) {
    send(client,"Content-Type: text/plain\r\n",strlen("Content-Type: text/plain\r\n"),0);
  } else if (jpg_t) {
    send(client,"Content-Type: image/jpeg\r\n",strlen("Content-Type: image/jpeg\r\n"),0);
  } else if (gif_t) {
    send(client,"Content-Type: image/gif\r\n",strlen("Content-Type: image/gif\r\n"),0);
  }
}

void close_client(int client) {
  //Gracefully closes client
  shutdown(client,0);
  close(client);
  client_count -= 1;
}

void send_date(int client) {
  //Sends the date in GMT time
  struct tm* ptr;
  time_t timeNow = time(NULL);
  ptr = gmtime(&timeNow);
  char timeString[64];
  strcpy(timeString, asctime(ptr));
  char dateString[128];
  strcpy(dateString, "Date: ");
  strcat(dateString,timeString);
  send(client,dateString,strlen(dateString),0);
}
