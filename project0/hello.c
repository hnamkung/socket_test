#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXLINE 8192
#define BUFSIZE 300

#define BPattern "%d%d%d%d%d%d%d%d"
#define BOrder(byte)  \
      (byte & 0x80 ? 1 : 0), \
  (byte & 0x40 ? 1 : 0), \
  (byte & 0x20 ? 1 : 0), \
  (byte & 0x10 ? 1 : 0), \
  (byte & 0x08 ? 1 : 0), \
  (byte & 0x04 ? 1 : 0), \
  (byte & 0x02 ? 1 : 0), \
  (byte & 0x01 ? 1 : 0) 

// struct for protocol
struct message {
    char op;
    char proto;
    unsigned short checksum;
    unsigned int trans_id;
};

// struct for io
typedef struct sockaddr SA;
typedef struct {
    int fd;
    int cnt;
    char *bufptr;
    char buf[BUFSIZE];
}rio_t;

// phase 1
void phase1(int fd_from_client);

// phase 2
void phase2(int fd_from_client);
void add_duplicate_slash(char *buf);
void remove_duplicate_slash(char *bubuff); 

// read write wrapper
void read_message(int fd, struct message *m);
void write_message(int fd, struct message *m);

// read write without buffer, handle shortcount
ssize_t rio_read_nobuf(int fd, void *usrbuf, size_t n);
ssize_t rio_write_nobuf(int fd, void *usrbuf, size_t n);

// read write with buffer
void rio_init(rio_t *rp, int fd);
ssize_t read_string(rio_t *rp, char *usrbuf, size_t maxlen);
ssize_t rio_read_buf(rio_t *rp, char *usrbuf, size_t n);

// open socket
int open_clientfd(char* ip, int port);

// calculate checksum
void set_checksum(struct message *m);

// for debug
void printMessage(struct message *m);

void main(int argc, char **argv)
{
    char *ip, buf[MAXLINE];
    int port;
    int fd_from_client;
    int i;

    for(i=1; i<argc; i++) {
        if(!strcmp(argv[i], "-h")) {
            ip = argv[++i];
        }
	if(!strcmp(argv[i], "-p")) {
            port = atoi(argv[++i]);
        }
        if(!strcmp(argv[i], "-m")) {
        }
    }
//    printf("port : %d\n", port);
//    printf("ip : %s\n", ip);

    fd_from_client = open_clientfd(ip, port);

    // phase 1 - hand shake first
    phase1(fd_from_client);

    // phase 2
    phase2(fd_from_client);
    close(fd_from_client);
}

// phase 1
void phase1(int fd)
{
    struct message read_m, write_m;
    read_message(fd, &read_m);

    write_m.op = 1;
    write_m.proto = 1;
    write_m.trans_id = read_m.trans_id;

    write_message(fd, &write_m);
}

// phase 2
void phase2(int fd)
{
    char buf[MAXLINE];
    int error;
    rio_t rio;
    rio_init(&rio, fd);
    while(fgets(buf, MAXLINE, stdin) != NULL) {
        add_duplicate_slash(buf);
        int length = strlen(buf);
        buf[length] = '\\';
        buf[length+1] = '0';
        buf[length+2] = 0;
        //printf("client : %s\n\n", buf);
        //printf("client : %s    len(%d)\n", buf, (int)strlen(buf));
        rio_write_nobuf(fd, buf, strlen(buf));

       // int len = rio_read_nobuf(fd, buf, 4);
       // buf[4] = 0;
       // printf("server : %s    len(%d)\n", buf, len);
        //printf("len : %d\n", len);
       if((error = read_string(&rio, buf, MAXLINE)) < 0) {
            printf("Error! read_string returned %d\n", error);
        }
       remove_duplicate_slash(buf);
       printf("%s", buf);
    }
}
void add_duplicate_slash(char *buf)
{
    int i=0;
    int j=0;
    while(buf[i] != 0) {
        if(buf[i] == '\\') {
            for(j=strlen(buf); j>i; j--) {
                buf[j+1] = buf[j];
            }
            buf[i+1] = '\\';
            i++;
        }
        i++;
    }
}

void remove_duplicate_slash(char *buf)
{
    int i=0;
    int j=0;
    while(buf[i] != 0) {
        if(buf[i] == '\\') {
            for(j=i+1; j<strlen(buf); j++) {
                buf[j] = buf[j+1];
            }
        }
        i++;
    }

}

// read write wrapper
void read_message(int fd, struct message *m)
{
    char buf[10];
    rio_read_nobuf(fd, buf, 8);

    m->op = buf[0];
    m->proto = buf[1];
    m->checksum = ( *(short *)(buf+2) );
    m->trans_id = ( *(int *)(buf+4) );
   // printf("read_message_network \n");
   // printMessage(m);

    m->checksum = ntohs( *(short *)(buf+2) );
    m->trans_id = ntohl( *(int *)(buf+4) );

    //printf("read_message_ubuntu_big_endian \n");
   // printMessage(m);

}

void write_message(int fd, struct message *m)
{
    struct message m_copy = *m;

    m_copy.trans_id = htonl(m_copy.trans_id);
    set_checksum(&m_copy);
    
    //printf("write_message_network_little_endian\n");
   // printMessage(&m_copy);
    rio_write_nobuf(fd, &m_copy, sizeof m_copy);
}

// read write without buffer, handle shortcount
ssize_t rio_read_nobuf(int fd, void *usrbuf, size_t n)
{
    size_t nleft = n;
    ssize_t nread;
    char *bufp = usrbuf;
    while(nleft > 0) {
        if( (nread = read(fd, bufp, nleft)) < 0) {
            return -1;
        }
        else if(nread == 0)
            break;
    //    printf("read_nobuf : %d\n", (int)nread);
        nleft -= nread;
        bufp += nread;
    }
    return (n - nleft);
}


ssize_t rio_write_nobuf(int fd, void *usrbuf, size_t n)
{
    size_t nleft = n;
    ssize_t nwritten;
    char *bufp = usrbuf;

    while (nleft > 0) {
        if( (nwritten = write(fd, bufp, nleft)) <= 0) {
            return -1;
        }
        nleft -= nwritten;
        bufp += nwritten;
    }
    return n;
}

// read write with buffer
void rio_init(rio_t *rp, int fd)
{
    rp->fd = fd;
    rp->cnt = 0;
    rp->bufptr = rp->buf;
}

ssize_t read_string(rio_t *rp, char *usrbuf, size_t maxlen)
{
    int n, rc;
    char c, *bufp = usrbuf;

   // printf("---read string start\n");

    for(n=1; n<maxlen; n++) {
        if((rc = rio_read_buf(rp, &c, 1)) == 1) {
            //printf("%c(%d)", c, (int)c);
            *bufp = c;
            if(*(bufp-1) == '\\' && *bufp == '0')
                break;
            bufp++;
        }
        else if(rc == 0) {
           return -2;
        } else
            return -4;
    }
    *(bufp-1) = 0;
  //  printf("\n---read done\n");
    return n-2;
}

ssize_t rio_read_buf(rio_t *rp, char *usrbuf, size_t n)
{
    int cnt;
   // printf("------rio_buffer start\n");
    while (rp->cnt <= 0) {
        rp->cnt = read(rp->fd, rp->buf, sizeof(rp->buf));
    //    printf("------cnt : %d\n", rp->cnt);
        if(rp->cnt < 0) {
            return -1;
        }
        else if(rp->cnt == 0)
            return 0;
        else
            rp->bufptr = rp->buf;
    }

    cnt = n;
    if(rp->cnt < n)
        cnt = rp->cnt;
    memcpy(usrbuf, rp->bufptr, cnt);
    rp->bufptr += cnt;
    rp->cnt -= cnt;
   // printf("------rio_buffer done\n");
    return cnt;
}

// open socket
int open_clientfd(char* ip, int port)
{
    struct in_addr ipv4addr;
    struct hostent *he;
    struct sockaddr_in serveraddr;
    int clientfd;

    // get in_addr from ip
    inet_pton(AF_INET, ip, &ipv4addr);

    // create socket
    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1; 

    // get host entry from in_addr
    if((he = gethostbyaddr(&ipv4addr, sizeof ipv4addr, AF_INET)) == NULL)
        return -2; 

    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)he->h_addr_list[0], 
        (char *)&serveraddr.sin_addr.s_addr, he->h_length);

    serveraddr.sin_port = htons(port);

    /* Establish a connection with the server */
    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
        return -1;
    return clientfd;
}

// calculate checksum
void set_checksum(struct message *m)
{
    //printf("set checksum\n");
    unsigned short checksum1, checksum2, checksum3;
    unsigned int checksum, carry;

    checksum1 = *(char *)m << 8;
    checksum1 += *((char *)m+1) & 0xff;
  //  printf("%x\n", checksum1);

    checksum2 = *((char *)m+4) << 8;
    checksum2 += *((char *)m+5) & 0xff;
 //   printf("%x\n", checksum2);

    checksum3 = *((char *)m+6) << 8;
    checksum3 += *((char *)m+7) & 0xff;
//    printf("%x\n", checksum3);

    checksum = checksum1 + checksum2 + checksum3;
    
    //printf("%x\n", checksum);

    while(checksum >> 16 != 0) {
       carry = checksum >> 16;
       checksum = checksum & 0xffff;
       checksum += carry;
    }
    m->checksum = ntohs(~checksum);
}

// for debug 
void printMessage(struct message *m)
{
    int j;

    char temp[3];
//    printf("test--\n");
//    printf("%p %p %p\n", temp, temp+1, temp+2);
//    printf("test--\n");

    for(j=0; j<8; j++) {
        char c = *((char *)m+j);

        if(j == 0)
            printf("op         (%p) : 0x%10x (", &m->op, (int)m->op);
        if(j == 1)
            printf(")\nproto      (%p) : 0x%10x (", &m->proto, (int)m->proto);
        if(j == 2)
            printf(")\nchecksum   (%p) : 0x%10x (", &m->checksum, m->checksum);
        if(j == 4)
            printf(")\ntrnas_id   (%p) : 0x%10x (", &m->trans_id, m->trans_id);
        //printf(".%p.", ((char *)m+j));
        printf(BPattern, BOrder(c));
    }

    printf(")\n\n");
}

