#include<stdio.h>
#include<stdlib.h>
#include<stdbool.h>
#include<string.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<sys/ioctl.h>
#include<net/if.h>
#include<unistd.h> 
#include<dirent.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<poll.h>
#include<ctype.h>
#include<unistd.h>
#include<sys/wait.h>
#include <fcntl.h>
#include "utils/base64.h"

#define BUFF_SIZE 1024
#define ERR_EXIT(a){ perror(a); exit(1); }

#define BUFFER_SIZE 4096
#define BIG_SIZE 210000000

char buffer[BUFF_SIZE];
char buffer_file[BIG_SIZE];
char SERVER_IP[32];
int PORT;

void *Memmem(const void *haystack, size_t haystack_len, const void *needle, size_t needle_len) {
    if (needle_len == 0) {
        return (void *)haystack;
    }

    if (needle_len > haystack_len) {
        return NULL;
    }

    const char *h = haystack;
    const char *n = needle;
    size_t h_len = haystack_len - needle_len + 1;

    for (size_t i = 0; i < h_len; ++i) {
        if (h[i] == n[0] && memcmp(&h[i], needle, needle_len) == 0) {
            return (void *)&h[i];
        }
    }

    return NULL;
}

char* percentEncode(const char* input) {
    // Allocate memory for the encoded string
    size_t inputLength = strlen(input);
    char* encoded = (char*)malloc(3 * inputLength + 1); // Each character may be encoded as "%XY", so we need 3 times the space

    if (encoded == NULL) {
        perror("Memory allocation error");
        exit(EXIT_FAILURE);
    }

    // Iterate through the input string and percent encode special characters
    size_t j = 0;
    for (size_t i = 0; i < inputLength; i++) {
        if (isalnum(input[i]) || input[i] == '-' || input[i] == '_' || input[i] == '.' || input[i] == '~') {
            // Characters allowed in a URL as per RFC 3986
            encoded[j++] = input[i];
        } else {
            // Percent encode special characters
            sprintf(encoded + j, "%%%02X", (unsigned char)input[i]);
            j += 3;
        }
    }

    // Null-terminate the encoded string
    encoded[j] = '\0';

    return encoded;
}

void send_put_request(int client_socket, char *file_name, int flag, char *encode){
    int html_file = open(file_name, O_RDONLY);
    int file_size = lseek(html_file, 0, SEEK_END);
    lseek(html_file, 0, SEEK_SET);
    memset(buffer_file,0,sizeof(buffer_file));
    read(html_file, buffer_file, file_size);
    //buffer_file[file_size] = '\0';
    close(html_file);
    //printf("%s\n",buffer_file);
    char *request_header = malloc(sizeof(char)*BUFF_SIZE);
    char *request_body = malloc(sizeof(char)*BIG_SIZE);
    char type[16], type2[8];
    if(strstr(file_name, "mp4") != NULL){
        strcpy(type,"video/mp4");
        strcpy(type2,"video");
    }
    else{
        strcpy(type,"text/plain");
        strcpy(type2,"file");
    }
    //for(int i=0;i<file_size;i++) printf("%c",buffer_file[i]);
    
    int body_len = strlen(
        "------WebKitFormBoundaryEANdbRV5UPI4pB1\r\n"
        "Content-Disposition: form-data; name=\"upfile\"; filename=\"\"\r\n"
        "Content-Type: \r\n\r\n"
        "\r\n"
        "------WebKitFormBoundaryEANdbRV5UPI4pB1\r\n"
        "Content-Disposition: form-data; name=\"submit\"\r\n\r\n"
        "Upload\r\n"
        "------WebKitFormBoundaryEANdbRV5UPI4pB1--\r\n"
    ) + strlen(file_name) + strlen(type) + file_size;

    //char *tmp = malloc(sizeof(char)*1024);
    sprintf(request_body,
        "------WebKitFormBoundaryEANdbRV5UPI4pB1\r\n"
        "Content-Disposition: form-data; name=\"upfile\"; filename=\"%s\"\r\n"
        "Content-Type: %s\r\n\r\n",file_name,type
    );
    int tmp_end = strlen(request_body);
    for(int i=0;i<file_size;i++){
        request_body[tmp_end+i] = buffer_file[i];
    }
    strcpy(request_body+tmp_end+file_size, "\r\n------WebKitFormBoundaryEANdbRV5UPI4pB1\r\n"
        "Content-Disposition: form-data; name=\"submit\"\r\n\r\n"
        "Upload\r\n"
        "------WebKitFormBoundaryEANdbRV5UPI4pB1--\r\n");

    sprintf(request_header,
        "POST /api/%s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "USER-Agent: CN2023Client/1.0\r\n"
        "Connection: keep-alive\r\n"
        "Authorization: Basic %s\r\n" 
        "Content-Type: multipart/form-data; boundary=----WebKitFormBoundaryEANdbRV5UPI4pB1\r\n"
        "Content-Length: %d\r\n\r\n",type2, SERVER_IP, PORT, encode, body_len
    );

    //for(int i=0;i<body_len;i++) printf("%c",request_body[i]);
    //printf("%d\n%d\n",file_size,body_len);
    //printf("%s",request_header);
    //printf("%s",request_body);
    
    send(client_socket, request_header, strlen(request_header), 0);
    //send(client_socket, request_body, body_len, 0);
    int cur_size = 0;
    while(1){
        if(body_len - cur_size < 20000){
            send(client_socket, request_body+cur_size, body_len - cur_size, 0);
            break;
        }
        else{
            send(client_socket, request_body+cur_size, 20000, 0);
            cur_size += 20000;
        }
    }
    memset(buffer, 0, sizeof(buffer));
    int bytes = read(client_socket, buffer, sizeof(buffer));

    //printf("%s",buffer);
    free(request_header);
    free(request_body);
    if(Memmem(buffer, bytes, "200", strlen("200")) != NULL) printf("Command succeeded.\n");
    else fprintf(stderr, "Command failed.\n");
}

void send_get_request(int client_socket, char *file_name){
    char *request_header = malloc(sizeof(char)*1024);
   
    char *encode = percentEncode(file_name);
    //printf("file name :%s, after encode: %s\n");
    sprintf(request_header,
        "GET /api/file/%s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "USER-Agent: CN2023Client/1.0\r\n"
        "Connection: keep-alive\r\n\r\n",encode, SERVER_IP, PORT
    );

    //printf("%s", request_header);
    send(client_socket, request_header, strlen(request_header), 0);
    
    //read
    char len[10];
    int file_size = 0;
    int ct = 0, i = 0;
    char *response = malloc(sizeof(char)*4096);
    
    int flag = 1;
    while(1){
        int ct = read(client_socket, response+i, 1);
        //printf("%c\n", response[i]);
        if(ct == 1 && i > 4 && (response[i] == '\n' && response[i-1] == '\r' && response[i-2] == '\n' && response[i-3] == '\r')){
            if(Memmem(response, i, "200", strlen("200")) == NULL){
                fprintf(stderr, "Command failed.\n");
                free(response);
                return;
            }
            char *tmp1 = Memmem(response, i, "Content-Length: ", strlen("Content-Length: "));
            tmp1 += 16;
            while(tmp1[0] != '\r'){
                file_size = file_size * 10 + (tmp1[0] - '0');
                tmp1 ++;   
            }
            break;
        }
        i += ct;
    }
    free(response);
    char *file = malloc(sizeof(char)*(file_size+1));
    for(int i=0;i<file_size;i++){
        int ct = read(client_socket, file+i, 1);
        if(ct == 0) i --;
    }
    
    mkdir("./files", 0777);
    char *path = malloc(sizeof(char)*128);
    sprintf(path, "./files/%s", file_name);
    int output_file = open(path, O_RDWR | O_CREAT , S_IRUSR | S_IWUSR);
    write(output_file, file, file_size);
    free(file);
    close(output_file);

    printf("Command succeeded.\n");
}

int main(int argc , char *argv[]){
    
    if(!(argc == 3 || argc == 4)){
        ERR_EXIT("Usage: ./client [host] [port] [username:password]");
    }
    
    int client_socket;
    struct sockaddr_in server_addr;
    char buffer[BUFF_SIZE];
    //int host = atoi(argv[1]);
    
    strcpy(SERVER_IP,argv[1]);
    PORT = atoi(argv[2]);
    char auth[64];
    unsigned char *encode;
    if(argc == 4){
        strcpy(auth,argv[3]);
        size_t output_len;
        encode = base64_encode(auth,strlen(auth),&output_len);
        encode[output_len] = '\0';
    }
    //printf("%s\n",encode);
    
    // Get socket file descriptor
    if((client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        ERR_EXIT("socket()");
    }

    // Set server address
    bzero(&server_addr,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(PORT);

    // Connect to the server
    if(connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        ERR_EXIT("connect()");
    }
    
    //send password and get validation
    while(1){
        char req[128];
        printf("> ");
        scanf(" %[^\n]", req);
        //printf("%s\n",req);
        if(strcmp(req,"quit") == 0){
            printf("Bye.\n");
            break;
        }
        else if(strstr(req,"put") != NULL){
            if(strncmp(req,"putv",4) == 0){ //putv
                if(strlen(req) == 4){
                    printf("Usage: putv [file]\n");
                }
                else{
                    char *file_name = strchr(req, ' ');
                    file_name += 1;
                    struct stat fileStat;
                    // Check if the file exists
                    if(stat(file_name, &fileStat) == 0){
                        send_put_request(client_socket, file_name, 1, encode);
                    } 
                    else {
                        fprintf(stderr, "Command failed.\n");
                    }
                }
            }
            else if(strncmp(req,"put",3) == 0){ //put
                if(strlen(req) == 3){
                    printf("Usage: put [file]\n");
                }
                else{
                    char *file_name = strchr(req, ' ');
                    file_name += 1;
                    struct stat fileStat;
                    // Check if the file exists
                    if(stat(file_name, &fileStat) == 0){
                        send_put_request(client_socket, file_name, 0, encode);
                    } 
                    else {
                        fprintf(stderr, "Command failed.\n");
                    }
                }
            }
            else{
                fprintf(stderr, "Command failed.\n");
            }            
        }
        else if(strstr(req,"get") != NULL){
            if(strlen(req) == 3){
                printf("Usage: get [file]\n");
            }
            else{
                char *file_name = strchr(req, ' ');
                file_name += 1;
                send_get_request(client_socket, file_name);
            }
        }
        else{
            fprintf(stderr, "Command not found.\n");
        }
    }
    close(client_socket);
    return 0;
}