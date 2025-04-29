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

#define ERR_EXIT(a){ perror(a); exit(1); }
#define BUFFER_SIZE 4096
#define MAX_CLIENTS 128

const char *ok_response = "HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\nContent-Type: text/html\r\nContent-Length: 3\r\n\r\nOK\n";
const char *unauth_response = "HTTP/1.1 401 Unauthorized\r\nServer: CN2023Server /1.0\r\nWWW-Authenticate: Basic realm=\"B10902126\"\r\nContent-Type: text/plain\r\nContent-Length: 13\r\n\r\nUnauthorized\n";
const char *not_found = "HTTP/1.1 404 Not Found\r\nServer: CN2023Server /1.0\r\nContent-Type: text/plain\r\nContent-Length: 10\r\n\r\nNot Found\n";
const char *not_allow = "HTTP/1.1 405 Method Not Allowed\r\nServer: CN2023Server/1.0\r\nAllow: GET\r\nContent-Length: 0\r\n\r\n";
const char *internal = "HTTP/1.1 500 Internal Server Error\r\nServer: CN2023Server/1.0\r\nContent-Length: 0\r\n\r\n";

char buffer[BUFFER_SIZE];
char buffer_file[BUFFER_SIZE*4];
int max_fd;
int file_list_cond = 1;
int video_list_cond = 1;
int clientSockets[1024];
int bufSize = 0;
char video[128];
char mpd_path[128];
bool pass = false;

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

void Clean(int client_socket){
    //clean
    char *contentLengthHeader = strstr(buffer, "Content-Length:");
    if(contentLengthHeader != NULL){
        int contentLength;
        sscanf(contentLengthHeader, "Content-Length: %d", &contentLength);
        char *head_end = Memmem(buffer, bufSize, "\r\n\r\n", strlen("\r\n\r\n"));
        head_end += strlen("\r\n\r\n");
        int header_size = (int)(head_end - buffer);
        int len = header_size + contentLength - bufSize;
        char trash[2];
        //printf("%d=%d+%d-%d\n",len,header_size,contentLength, bufSize);
        for(int i=0;i<len;i++){
            read(client_socket, trash, 1);
            //printf("%c",trash);
        }
        printf("clean len:%d\n", len);
    }
}

char hex_to_char(char c1, char c2) {
    char result = 0;
    if (isdigit(c1)) {
        result = c1 - '0';
    } else {
        result = toupper(c1) - 'A' + 10;
    }
    result *= 16;
    if (isdigit(c2)) {
        result += c2 - '0';
    } else {
        result += toupper(c2) - 'A' + 10;
    }
    return result;
}

void url_decoding(char *url){
    int length = strlen(url);
    char *decoded_url = (char *)malloc(length + 1);
    int j = 0;

    for (int i = 0; i < length; ++i) {
        if (url[i] == '%' && isxdigit(url[i + 1]) && isxdigit(url[i + 2])) {
            decoded_url[j] = hex_to_char(url[i + 1], url[i + 2]);
            i += 2;
        } else {
            decoded_url[j] = url[i];
        }
        ++j;
    }

    decoded_url[j] = '\0';
    strcpy(url, decoded_url);
    free(decoded_url);
    //printf("%s\n",url);
}

char* url_encoding(const char* input) {
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

bool password_authentiaction(char *tmp1){
    char *tmp = malloc(sizeof(char)*128);
    strncpy(tmp, tmp1, 128);
    tmp += 21;
    char *token = strtok(tmp, "\r\n");

    size_t output_len;
    unsigned char *decode = base64_decode(token,strlen(token),&output_len);
    decode[output_len] = '\0';

    //printf("%s\n",decode);

    int secret_file = open("./secret", O_RDONLY);

    int file_size = lseek(secret_file, 0, SEEK_END);
    lseek(secret_file, 0, SEEK_SET);
    memset(buffer_file,0,sizeof(buffer_file));
    read(secret_file, buffer_file, file_size);
    buffer_file[file_size] = '\0';
    close(secret_file);


    //printf("secret list:\n%s\n",buffer_file);
    char *pos;
    if((pos = Memmem(buffer_file, file_size, decode, output_len)) != NULL){
        char *p_head = strstr(pos, ":");
        p_head ++;
        int len = 0;
        while(p_head[len] != '\n' && p_head[len] != '\0') len ++;
        
        char *pw = strstr(decode, ":");
        pw ++;
        //printf("%d %d\n",len, strlen(pw));
        if(len == strlen(pw)) return true;
        else return false;
    }
    else{
        return false;
    }
}

int set_file_size_and_buffer(int html_file){ //FILE *html_file
    
    int file_size = lseek(html_file, 0, SEEK_END);
    lseek(html_file, 0, SEEK_SET);
    memset(buffer_file,0,sizeof(buffer_file));
    ssize_t bytesRead = read(html_file, buffer_file, file_size);
    
    buffer_file[file_size] = '\0';
    close(html_file);
    //printf("file size:%d\n%s",file_size,buffer_file);
    return file_size;
}

char *replaceFileList(char *htmlContent, const char *replacement, size_t *new_file_size, int flag) {
    char *type = malloc(sizeof(char)*32);
    if(flag == 0) strcpy(type,"<?FILE_LIST?>");
    else strcpy(type,"<?VIDEO_LIST?>");
    char *pos = strstr(htmlContent, type);
    //printf("%s\n",htmlContent);
    //printf("%s\n%s\n%s\n",replacement,type,pos);
    
    if (pos != NULL) {
        size_t lenBefore = pos - htmlContent;
        size_t lenAfter = strlen(pos + strlen(type));
        size_t newLen = lenBefore + strlen(replacement) + lenAfter;
        *new_file_size = newLen;
        
        char *newHtmlContent = (char *)malloc(newLen + 1);
        if (newHtmlContent == NULL) {
            perror("Memory allocation error");
            exit(EXIT_FAILURE);
        }
    
        strncpy(newHtmlContent, htmlContent, lenBefore);
        strcpy(newHtmlContent + lenBefore, replacement);
        strcpy(newHtmlContent + lenBefore + strlen(replacement), pos + strlen(type));

        newHtmlContent[newLen] = '\0';
        free(htmlContent);
        return newHtmlContent;
    }
    else{
        ERR_EXIT("replace");
    }
}

void refresh_file(int html_file, int output_file, int flag){ //FILE *html_file, FILE *output_file, int flag

    int file_size = lseek(html_file, 0, SEEK_END);
    lseek(html_file, 0, SEEK_SET);
    char *content = (char *)malloc(file_size + 1);
    ssize_t bytesRead = read(html_file, content, file_size);
    /*if(fread(content, 1, file_size, html_file) < 0){
        ERR_EXIT("fread");
    }*/
    content[file_size] = '\0'; // Null-terminate the content
    //fclose(html_file);
    close(html_file);
    
    DIR *dir;
    struct dirent *entry;
    size_t new_file_size;
    char on_list[4096] = "";
    char *new_content;
    // Open the directory
    if(flag == 0){ //files
        dir = opendir("./web/files");
        if (dir == NULL) {
            perror("Error opening directory");
            return;
        }
        // Read the directory entries
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG && strcmp(entry->d_name,".DS_Store") != 0) {  // Check if it is a regular file
                //printf("%s\n", entry->d_name);
                char tmp[128];
                char *encode_name = url_encoding(entry->d_name);
                sprintf(tmp,"<tr><td><a href=\"/api/file/%s\">%s</a></td></tr>",encode_name,entry->d_name);
                strcat(on_list,tmp);
            }
        }
        closedir(dir);
        new_content = replaceFileList(content, on_list, &new_file_size, flag); 
    } 
    else if(flag == 1){ //video dir
        dir = opendir("./web/videos");
        if (dir == NULL) {
            perror("Error opening directory");
            return;
        }
        // Read the directory entries
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name,".DS_Store") != 0 && strcmp(entry->d_name,".") != 0 && strcmp(entry->d_name,"..") != 0){  // Check if it is a regular file
                //printf("%s\n", entry->d_name);
                char tmp[128];
                sprintf(tmp,"<tr><td><a href=\"/video/%s\">%s</a></td></tr>",entry->d_name,entry->d_name);
                strcat(on_list,tmp);
            }
        }
        closedir(dir);
        new_content = replaceFileList(content, on_list, &new_file_size, flag); 
    }
    else{
        //new_content = replaceFileList(content, on_list, &new_file_size, flag); 
        char *tmp1 = "<?VIDEO_NAME?>";
        char *pos1 = strstr(content, tmp1);
        char *tmp2 = "<?MPD_PATH?>";
        char *pos2 = strstr(content, tmp2);
        
        size_t lenBefore = pos1 - content;
        size_t lenCenter = pos2 - (pos1 + strlen(tmp1));
        size_t lenAfter = strlen(pos2 + strlen(tmp2));
        size_t newLen = lenBefore + lenCenter + lenAfter + strlen(video) + strlen(mpd_path);
        char *newHtmlContent = (char *)malloc(newLen + 5);
        if (newHtmlContent == NULL) {
            perror("Memory allocation error");
            exit(EXIT_FAILURE);
        }
    
        strncpy(newHtmlContent, content, lenBefore);
        strncpy(newHtmlContent + lenBefore, video, strlen(video));
        strncpy(newHtmlContent + lenBefore + strlen(video), pos1 + strlen(tmp1), lenCenter);
        strncpy(newHtmlContent + lenBefore + strlen(video)+ lenCenter, mpd_path, strlen(mpd_path));
        //strncpy(newHtmlContent + lenBefore + strlen(video) + lenCenter + strlen(mpd_path), pos2 + strlen(tmp2),lenAfter);
        strcpy(newHtmlContent + lenBefore + strlen(video) + lenCenter + strlen(mpd_path), pos2 + strlen(tmp2));

        newHtmlContent[newLen] = '\0';
        new_content = newHtmlContent;
        new_file_size = newLen;
    }

    //fwrite(new_content, 1, new_file_size, output_file);
    ssize_t bytes_written = write(output_file, new_content, new_file_size);
    //printf("%d\n%d\n%s\n",new_file_size,bytes_written,new_content);
    free(new_content);
}

void show_files(int client_socket, char *file, int flag){
    memset(buffer,0,sizeof(buffer));
    if(flag == 0) sprintf(buffer,"./web/files/%s",file);
    else sprintf(buffer,"./web/videos/%s",file);

    //if(access(buffer, F_OK) != -1){//if exist
        
        int read_file = open(buffer, O_RDONLY);
        int file_size = lseek(read_file, 0, SEEK_END);
        lseek(read_file, 0, SEEK_SET);
        char *content = (char *)malloc(file_size + 1);
        ssize_t bytesRead = read(read_file, content, file_size);
        //printf("file len:%d\n",file_size);
        //printf("%s\n",content);
        //content[file_size] = '\0';
        close(read_file);
        char *header = malloc(sizeof(char)*1024);
        if(flag == 0){
            sprintf(header,"HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n",file_size);
        }
        else{
            //send player html file
            sprintf(header,"HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\nContent-Type: video/mp4\r\nContent-Length: %d\r\n\r\n",file_size);
        }
        send(client_socket, header, strlen(header), 0);

        int cur_size = 0;
        while(1){
            if(file_size - cur_size < 20000){
                send(client_socket, content+cur_size, file_size - cur_size, 0);
                break;
            }
            else{
                send(client_socket, content+cur_size, 20000, 0);
                cur_size += 20000;
            }
        }
    //}
}

void send_mpd(int client_socket, char *file_path){
    
    int MPD_file = open(file_path, O_RDONLY);
    int file_size = lseek(MPD_file, 0, SEEK_END);
    lseek(MPD_file, 0, SEEK_SET);
    char *content = (char *)malloc(file_size + 1);
    ssize_t bytesRead = read(MPD_file, content, file_size);
    content[file_size] = '\0'; 
    close(MPD_file);

    char *header = malloc(sizeof(char)*1024);
    sprintf(header,"HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\nContent-Type: application/dash+xml\r\nContent-Length: %d\r\n\r\n",file_size);
    send(client_socket, header, strlen(header), 0);

    int cur_size = 0;
    while(1){
        if(file_size - cur_size < 20000){
            send(client_socket, content+cur_size, file_size - cur_size, 0);
            break;
        }
        else{
            send(client_socket, content+cur_size, 20000, 0);
            cur_size += 20000;
        }
    }

}

void show_video(int client_socket){
    memset(mpd_path,0,128);
    sprintf(mpd_path,"\"/api/video/%s/dash.mpd\"",video);

    int html_file = open("./web/player.rhtml", O_RDONLY);
    int output_file = open("./web/output_player.rhtml", O_RDWR | O_CREAT , S_IRUSR | S_IWUSR);
    ftruncate(output_file, 0);
    refresh_file(html_file, output_file, 2);
    int file_size = set_file_size_and_buffer(output_file);
    

    char response[BUFFER_SIZE] = {'\0'};
    sprintf(response,"HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n%s\0",file_size,buffer_file);
    send(client_socket, response, strlen(response), 0);
}

void convert_to_dash(char *file_name, char *file_path){
    char *pt = strstr(file_name, ".");
    char *dir = malloc(sizeof(char)*64);
    strncpy(dir, file_name,(int)(pt-file_name));
    dir[(int)(pt-file_name)] = '\0';
    char *to_path = malloc(sizeof(char)*128);
    sprintf(to_path,"./web/videos/%s",dir);

    mkdir(to_path, 0700);
    //double fork
    pid_t firstChild, grandchild;
    // First fork
    
    firstChild = fork();
    if(firstChild < 0) {
        perror("Error in first fork");
        exit(EXIT_FAILURE);
    }
    if(firstChild > 0) {
        wait(NULL); 
        printf("main process running\n");
    }else{
        setsid();
        grandchild = fork();
        if(grandchild < 0) {
            perror("Error in second fork");
            exit(EXIT_FAILURE);
        }
        if(grandchild > 0) {
            exit(EXIT_SUCCESS);
        }else{
            if(execlp("./dash.sh", "dash.sh", file_path, to_path, (char *)NULL) == -1) printf("dash error\n");
            else printf("run sh finish\n");
            exit(EXIT_SUCCESS);
        }
    }
    
    int html_file = open("./web/listv.rhtml", O_RDONLY);
    int output_file = open("./web/output_video.rhtml", O_RDWR | O_CREAT , S_IRUSR | S_IWUSR);
    ftruncate(output_file, 0);
    refresh_file(html_file, output_file, 1);
    close(output_file);
}

void get_body_and_upload(int client_socket, int flag){
    char *contentLengthHeader = strstr(buffer, "Content-Length:");
    //printf("%s",buffer);
    if (contentLengthHeader != NULL) {
        // Extract the content length
        int contentLength;
        sscanf(contentLengthHeader, "Content-Length: %d", &contentLength);
        // Read the body based on content length
        char *body = malloc(sizeof(char)*(contentLength+1));
        ssize_t bytesRead = 0;
        int ct = 0, k = 0;
        //read body
        //printf("content_LEN: %d\n",contentLength);
        char *head_end;
        if((head_end = Memmem(buffer, bufSize, "\r\n\r\n", strlen("\r\n\r\n"))) != NULL){
            head_end += strlen("\r\n\r\n");
            int cur_read = head_end - buffer;
            while(cur_read < bufSize){
                body[k] = head_end[k];
                //printf("%c",body[k]);
                k ++;
                cur_read ++;
                bytesRead ++;
                //printf("%d\n",bytesRead);
            }
        }
        
        char *bound_pos = Memmem(buffer, bufSize, "boundary=", strlen("boundary="));
        bound_pos += 9;
        char *next_line = Memmem(bound_pos, bufSize-(bound_pos-buffer), "\r\n", strlen("\r\n"));
        int bound_len = next_line - bound_pos;
        //printf("%d\n",bound_len);
        char *bound = malloc(sizeof(char)*(bound_len + 1));
        strncpy(bound,bound_pos,bound_len);
        char boundary[128] = "--";
        strcat(boundary,bound);
        bound_len += 2;
        boundary[bound_len] = '\0';
        //printf("%s\n",boundary);
        //printf("%d %d\n",bytesRead, contentLength);

        while(bytesRead < contentLength){
            ct = read(client_socket, body+k, 1);
            //printf("%c",body[k]);
            k ++;
            bytesRead += ct;
        }
        //printf("byteRead:%d contentLength:%d\n",bytesRead,contentLength);
        //printf("%s\n",body);

        //find head and tail
        char *head = Memmem(body,contentLength,"\r\n\r\n",strlen("\r\n\r\n")) + strlen("\r\n\r\n");
        char *tail = Memmem(head,contentLength-(head-body),boundary,strlen(boundary)) - strlen("\r\n");

        const char *find1 = "filename=\"";
        int find1_len = strlen(find1); 
        char *find1_pt = Memmem(body,contentLength,find1,find1_len);
        find1_pt += find1_len;
        int length = 0;
        while(1){
            if(find1_pt[length] == '\"') break;
            length ++;
        }
        //file size
        int file_size = (int)(tail-head);
        char *file_name = malloc(length + 1);
        strncpy(file_name, find1_pt, length);
        file_name[length] = '\0';
        //printf("%d\n",(int)(head-body));
        //printf("%d\n",(int)(tail-body));
        /*printf("\n%s\n",file_name);
        for(int i=0;i<file_size;i++){
            printf("%c",head[i]);
        }*/

        char path[128];
        if(flag){ //mp4
            sprintf(path,"./web/tmp/%s",file_name);
            int write_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            ssize_t bytes_written = write(write_fd, head, file_size);
            close(write_fd);
            //if(access(path, F_OK) == -1) ERR_EXIT("upload"); 

            char response[BUFFER_SIZE];
            sprintf(response,"HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\nContent-Type: text/\r\nContent-Length: 15\r\n\r\nVideo Uploaded\n");
            send(client_socket, response, strlen(response), 0);
            convert_to_dash(file_name,path);
        }
        else{ //file
            sprintf(path,"./web/files/%s",file_name);
            printf("open write path: %s\n",path);

            int write_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            ssize_t bytes_written = write(write_fd, head, file_size);
            close(write_fd);
            //printf("write done\n");
            printf("byte write:%d\n",bytes_written);

            int html_file = open("./web/listf.rhtml", O_RDONLY);
            int output_file = open("./web/output_file.rhtml", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            ftruncate(output_file, 0);
            refresh_file(html_file, output_file, 0);
            

            char response[BUFFER_SIZE];
            sprintf(response,"HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\nContent-Type: text/\r\nContent-Length: 14\r\n\r\nFile Uploaded\n");
            send(client_socket, response, strlen(response), 0);
        }
        
        //output_file = fopen("./web/output.rhtml", "r");
        //file_size = set_file_size_and_buffer(output_file);
    }
}

void send_html(int client_socket,int op){
    //FILE *html_file = NULL;
    int html_file;
    int file_size = 0;
    if(op == 1){
        //html_file = fopen("./web/index.html", "r");
        html_file = open("./web/index.html", O_RDONLY);
        file_size = set_file_size_and_buffer(html_file);
        //printf("%s",buffer_file);
    }
    else if(op == 2){
        if(file_list_cond == 1){ //file list update
            html_file = open("./web/listf.rhtml", O_RDONLY);
            int output_file = open("./web/output_file.rhtml", O_RDWR | O_CREAT , S_IRUSR | S_IWUSR);
            ftruncate(output_file, 0);
            refresh_file(html_file, output_file, 0);
            file_size = set_file_size_and_buffer(output_file);
            file_list_cond = 0;
        }
        else{ //file remain
            int output_file = open("./web/output_file.rhtml", O_RDONLY);
            file_size = set_file_size_and_buffer(output_file);
        }
    }
    else if(op == 3){
        if(video_list_cond == 1){ //video list update
            html_file = open("./web/listv.rhtml", O_RDONLY);
            int output_file = open("./web/output_video.rhtml", O_RDWR | O_CREAT , S_IRUSR | S_IWUSR);
            ftruncate(output_file, 0);
            refresh_file(html_file, output_file, 1);
            file_size = set_file_size_and_buffer(output_file);
            video_list_cond = 0;
        }
        else{ //file remain
            //FILE *output_file = fopen("./web/output_video.rhtml", "r");
            int output_file = open("./web/output_video.rhtml", O_RDONLY);
            file_size = set_file_size_and_buffer(output_file);
        }
    }
    else if(op == 4){
        //html_file = fopen("./web/uploadf.html", "r");
        html_file= open("./web/uploadf.html", O_RDONLY);
        file_size = set_file_size_and_buffer(html_file);
    }
    else if(op == 5){
        //html_file = fopen("./web/uploadv.html", "r");
        html_file= open("./web/uploadv.html", O_RDONLY);
        file_size = set_file_size_and_buffer(html_file);
    }
    //printf("%s",buffer_file);
    char response[BUFFER_SIZE] = {'\0'};
    sprintf(response,"HTTP/1.1 200 OK\r\nServer: CN2023Server/1.0\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n%s",file_size,buffer_file);
    send(client_socket, response, strlen(response), 0);
}

void parse_request(int client_socket){
    //printf("%s",buffer);
    //printf("~~~~~~~~\n");
    char method[5];
    if(buffer[0] == 'G') strcpy(method,"GET");
    else if(buffer[0] == 'P') strcpy(method,"POST");
    char *firstSpace = strchr(buffer, ' ');
    char *secondSpace = strchr(firstSpace+1, ' ');
    size_t length = secondSpace - (firstSpace + 1);
    char *path = malloc(length + 1);
    strncpy(path, firstSpace + 1, length);
    path[length] = '\0';
    //printf("%s %s\n",method,path);

    url_decoding(path);
    
    char *stat_start = strstr(buffer,"Connection:");
    char *stat_end = strstr(stat_start,"\r\n");
    stat_start += 12;
    int tmp_len = stat_end - stat_start;
    char *connection = (char*)malloc(sizeof(tmp_len+1));
    strncpy(connection,stat_start,tmp_len);
    connection[tmp_len] = '\0';

    if(strcmp(path,"/favicon.ico") == 0){
         
    }
    else if(strcmp(path,"/") == 0){ // root
        //printf("Substring 1 found\n");
        if(strcmp(method,"GET") == 0) send_html(client_socket,1);
        else send(client_socket, not_allow, strlen(not_allow), 0);
    }else if(strstr(path,"file") != NULL){ //file op
        if((strcmp(path,"/file") == 0 || strcmp(path,"/file/") == 0)){ //file list
            if(strcmp(method,"GET") == 0) send_html(client_socket,2);
            else send(client_socket, not_allow, strlen(not_allow), 0);
        }
        else if((strcmp(path,"/upload/file") == 0 || strcmp(path,"/upload/file/") == 0)){ //upload file page and auth
            if(strcmp(method,"GET") == 0){
                char *start = strstr(buffer,"Authorization: Basic ");
                if(start != NULL){
                    pass = password_authentiaction(start);
                    if(pass) send_html(client_socket,4);
                    else send(client_socket, unauth_response, strlen(unauth_response), 0);
                }
                else send(client_socket, unauth_response, strlen(unauth_response), 0); //pop up password window
            }
            else send(client_socket, not_allow, strlen(not_allow), 0);
        }
        else if(strstr(path,"/api/file") != NULL){
            if(strcmp(path,"/api/file") == 0 || strcmp(path,"/api/file/") == 0){ //api upload file
                if(strcmp(method,"POST") == 0){
                    printf("%s~~~~~~\n",buffer);
                    
                    char *start = Memmem(buffer, bufSize, "Authorization: Basic ", strlen("Authorization: Basic "));
                    if(start != NULL){
                        printf("has basic!!!!\n");
                        pass = password_authentiaction(start);
                        if(pass) printf("yep yep\n");
                        if(pass) get_body_and_upload(client_socket, 0);
                        else{
                            Clean(client_socket);
                            send(client_socket, unauth_response, strlen(unauth_response), 0);
                        }
                    }
                    else{
                        Clean(client_socket);
                        send(client_socket, unauth_response, strlen(unauth_response), 0);
                    }
                }
                else send(client_socket, not_allow, strlen(not_allow), 0);
            }
            else{ //show file
                if(strcmp(method,"GET") == 0){
                    //printf("Substring 7 found\n");
                    char *file = strtok(path,"/");
                    file = strtok(NULL, "/");
                    file = strtok(NULL, "/");
                    //printf("%s\n",file);
                    //printf("%s",buffer);
                    struct stat fileStat;
                    char tmp_path[32];
                    sprintf(tmp_path,"./web/files/%s", file);
                    if(stat(tmp_path, &fileStat) != 0) send(client_socket, not_found, strlen(not_found), 0);
                    else show_files(client_socket,file, 0);
                }
                else send(client_socket, not_allow, strlen(not_allow), 0);
            }
        }
        else send(client_socket, not_found, strlen(not_found), 0);
    }
    else if(strstr(path,"video") != NULL){ //video op
        if((strcmp(path,"/video") == 0 || strcmp(path,"/video/") == 0)){ //video list
            if(strcmp(method,"GET") == 0) send_html(client_socket,3);
            else send(client_socket, not_allow, strlen(not_allow), 0);
        }
        else if(strcmp(path,"/upload/video") == 0 || strcmp(path,"/upload/video/") == 0){ //upload video page and auth
            if(strcmp(method,"GET") == 0){
                char *start = strstr(buffer,"Authorization: Basic ");
                if(start != NULL){
                    pass = password_authentiaction(start);
                    if(pass) send_html(client_socket,5);
                    else send(client_socket, unauth_response, strlen(unauth_response), 0);
                }
                else send(client_socket, unauth_response, strlen(unauth_response), 0); //pop up password window
            }
            else send(client_socket, not_allow, strlen(not_allow), 0);
        }
        else if(strstr(path,"/api/video") != NULL){
            if(strcmp(path,"/api/video") == 0 || strcmp(path,"/api/video/") == 0){ //api upload video
                if(strcmp(method,"POST") == 0){
                    char *start = strstr(buffer,"Authorization: Basic ");
                    if(start != NULL){
                        pass = password_authentiaction(start);
                        if(pass) get_body_and_upload(client_socket, 1);
                        else{
                            Clean(client_socket);
                            send(client_socket, unauth_response, strlen(unauth_response), 0);
                        }
                    }
                    else{
                        Clean(client_socket);
                        send(client_socket, unauth_response, strlen(unauth_response), 0);
                    }
                }
                else send(client_socket, not_allow, strlen(not_allow), 0);
            }
            else{ // show video: api/video/video name
                //printf("%s",buffer);
                char file_path[128];
                char *file = strstr(path,"/");
                file = strstr(file+1, "/");
                file = strstr(file+1, "/");
                //printf("%s\n",file);
                sprintf(file_path,"./web/videos%s",file);
                //printf("%s\n",file_path);
                struct stat fileStat;
                if(stat(file_path, &fileStat) != 0) send(client_socket, not_found, strlen(not_found), 0); //doesnt exist
                else send_mpd(client_socket, file_path);
            }
        }
        else if(strcmp(path,"/video/") > 0){ //click video:conver dash
            if(strcmp(method,"GET") == 0){
                //printf("Substring 8 found\n");
                char *file = strtok(path,"/");
                file = strtok(NULL, "/");
                memset(video,0,128);
                strcpy(video,file);
                //printf("%s\n",video);
                //printf("%s\n",file);
                //printf("%s",buffer);
                //show_files(client_socket,file);
                struct stat fileStat;
                char tmp_path[32];
                sprintf(tmp_path,"./web/videos/%s", video);
                if(stat(tmp_path, &fileStat) != 0) send(client_socket, not_found, strlen(not_found), 0);
                else show_video(client_socket);
            }
            else send(client_socket, not_allow, strlen(not_allow), 0);
        }
        else send(client_socket, not_found, strlen(not_found), 0);
    }
    else send(client_socket, not_found, strlen(not_found), 0);
    

    if(strcmp(connection,"Close") == 0) close(client_socket);

    //printf("command completed\n");
}

int main(int argc, char *argv[]){
    if(argc > 2){
        ERR_EXIT("Usage: ./server [port]");
    }
    int PORT = atoi(argv[1]);
    int server_socket, client_socket; //fd of server and client
    struct sockaddr_in server_addr;

    mkdir("./web/files", 0700);
    mkdir("./web/videos", 0700);
    mkdir("./web/tmp", 0700);
    //int client_addr_len = sizeof(client_addr);
    
    // Get socket file descriptor
    if((server_socket = socket(AF_INET , SOCK_STREAM , 0)) < 0){
        ERR_EXIT("socket()");
    }

    // Set server address information
    bzero(&server_addr, sizeof(server_addr)); // erase the data
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    // Bind the server file descriptor to the server address
    if(bind(server_socket, (struct sockaddr *)&server_addr , sizeof(server_addr)) < 0){
        ERR_EXIT("bind()");
    }

    // Listen on the server file descriptor
    if(listen(server_socket , MAX_CLIENTS) < 0){
        ERR_EXIT("listen()");
    }

    int server_addrlen = sizeof(server_addr);

    /*for (int i = 0; i < MAX_CLIENTS; i++) {
        clientSockets[i] = 0;
    }
    max_fd = server_socket;*/

    struct pollfd pollfds[MAX_CLIENTS + 1];
    pollfds[0].fd = server_socket;
    pollfds[0].events = POLLIN;
    int useClient = 1;

    for (int i = 1; i < MAX_CLIENTS; i++) {
         pollfds[i].fd = -1;
         pollfds[i].events = POLLIN;
    }

    while(1)
    {
        // printf("useClient => %d\n", useClient);
        int pollResult = poll(pollfds, useClient + 1, 0);
        if (pollResult > 0)
        {
            if (pollfds[0].revents & POLLIN)
            {
                struct sockaddr_in cliaddr;
                int addrlen = sizeof(cliaddr);
                int client_socket = accept(server_socket, (struct sockaddr *)&cliaddr, &addrlen);
                //Add new client
                for (int i = 1; i < MAX_CLIENTS; i++)
                {
                    if (pollfds[i].fd == -1)
                    {
                        pollfds[i].fd = client_socket;
                        pollfds[i].events = POLLIN;
                        useClient++;
                        //printf("accept success %s, from fd: %d\n", inet_ntoa(cliaddr.sin_addr), pollfds[i].fd);
                        //printf("%d %d\n",i,pollfds[i].fd);
                        break;
                    }
                }
            }
            for (int i = 1; i < MAX_CLIENTS; i++)
            {
                if (pollfds[i].fd != -1 && (pollfds[i].revents & POLLIN))
                {   
                    //printf("%d %d\n",i,pollfds[i].fd);
                    memset(buffer,0,sizeof(buffer));
                    bufSize = read(pollfds[i].fd, buffer, sizeof(buffer)-1);

                    
                    //printf("\nFrom fd:%d\n",pollfds[i].fd);
                    //printf("Read Size: %d\n", bufSize);
                    //buffer[bufSize] = '\0';
                    //printf("Buf:\n%s", buffer);
                    if (bufSize == -1)
                    {   
                        printf("-1 close fd:%d\n",pollfds[i].fd);
                        close(pollfds[i].fd);
                        pollfds[i].fd = -1;
                        useClient--;
                    }
                    else if (bufSize == 0)
                    {
                        printf("CONNECTION close fd:%d\n",pollfds[i].fd);
                        close(pollfds[i].fd);
                        pollfds[i].fd = -1;
                        useClient--;
                    }
                    else
                    {
                        //printf("From client: %s\n", buffer);
                        //printf("200 use fd: %d\n",pollfds[i].fd);
                        parse_request(pollfds[i].fd);
                        pollfds[i].revents = 0;
                        //printf("Done Parsing: %d\n",pollfds[i].fd);
                    }
                    
                }
            }
            /*printf("Actioning Client: ");
            for(int i=1; i<MAX_CLIENTS; i++){
                if(pollfds[i].fd != -1) printf("%d ", pollfds[i].fd);
            }
            printf("\n");*/
        }
    }
    printf("bye\n");
    close(server_socket);
    return 0;
}