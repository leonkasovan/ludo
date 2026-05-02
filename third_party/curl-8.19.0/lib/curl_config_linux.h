/* curl_config.h for Linux (aarch64 / x86_64) - GnuTLS + brotli + zstd */

#define HAVE_CONFIG_H 1
#define _GNU_SOURCE 1

/* Platform */
#define CURL_OS "linux"

/* Headers */
#define HAVE_ARPA_INET_H 1
#define HAVE_FCNTL_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LINUX_TCP_H 1
#define HAVE_NETDB_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_NETINET_TCP_H 1
#define HAVE_NETINET_UDP_H 1
#define HAVE_POLL_H 1
#define HAVE_PTHREAD_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_RESOURCE_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_UIO_H 1
#define HAVE_SYS_UN_H 1
#define HAVE_PWD_H 1
#define HAVE_TIME_H 1
#define HAVE_UNISTD_H 1
#define HAVE_UTIME_H 1

/* Type sizes */
#define SIZEOF_INT 4
#define SIZEOF_LONG 8
#define SIZEOF_OFF_T 8
#define SIZEOF_CURL_OFF_T 8
#define SIZEOF_CURL_SOCKET_T 4
#define SIZEOF_SIZE_T 8
#define SIZEOF_TIME_T 8
#define HAVE_STRUCT_TIMEVAL 1

/* Functions */
#define HAVE_ALARM 1
#define HAVE_BASENAME 1
#define HAVE_CLOCK_GETTIME_MONOTONIC 1
#define HAVE_CONNECT 1
#define HAVE_FCNTL_O_NONBLOCK 1
#define HAVE_FREEADDRINFO 1
#define HAVE_FSETXATTR 1
#define HAVE_GETADDRINFO 1
#define HAVE_GETEUID 1
#define HAVE_GETHOSTNAME 1
#define HAVE_GETPPID 1
#define HAVE_GETPWUID 1
#define HAVE_GETRLIMIT 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_GMTIME_R 1
#define HAVE_INET_NTOP 1
#define HAVE_INET_PTON 1
#define HAVE_IOCTL 1
#define HAVE_IOCTLSOCKET 1
#define HAVE_KEVENT 1
#define HAVE_KQUEUE 1
#define HAVE_LIBZ 1
#define HAVE_LOCALTIME_R 1
#define HAVE_MEMRCHR 1
#define HAVE_NAPMS 1
#define HAVE_PIPE 1
#define HAVE_POLL 1
#define HAVE_POSIX_STRERROR_R 1
#define HAVE_PTHREAD_H 1
#define HAVE_PWRITE 1
#define HAVE_RECV 1
#define RECV_TYPE_ARG1 int
#define RECV_TYPE_ARG2 void *
#define RECV_TYPE_ARG3 size_t
#define RECV_TYPE_ARG4 int
#define HAVE_SELECT 1
#define HAVE_SEND 1
#define SEND_TYPE_ARG1 int
#define SEND_TYPE_ARG2 const void *
#define SEND_TYPE_ARG3 size_t
#define SEND_TYPE_ARG4 int
/* SEND_4TH_ARG defined by curl_setup.h */
#define HAVE_SETLOCALE 1
#define HAVE_SIGACTION 1
#define HAVE_SIGINTERRUPT 1
#define HAVE_SIGNAL 1
#define HAVE_SIGSETJMP 1
#define HAVE_SOCKADDR_IN6_SIN6_SCOPE_ID 1
#define HAVE_SOCKET 1
#define HAVE_STRCASECMP 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR_R 1
#define HAVE_STRNCASECMP 1
#define HAVE_STRTOK_R 1
#define HAVE_STRTOLL 1
#define HAVE_WRITV 1
#define RETSIGTYPE void

/* Compression */
#define HAVE_BROTLI 1
#define HAVE_BROTLI_BROTLI_DECODE_H 1
#define HAVE_ZSTD 1
#define HAVE_ZSTD_ZSTD_H 1

/* SSL/TLS - OpenSSL */
#define USE_OPENSSL 1
#define HAVE_OPENSSL_CRYPTO_H 1
#define HAVE_OPENSSL_ERR_H 1
#define HAVE_OPENSSL_PEM_H 1
#define HAVE_OPENSSL_PKCS12_H 1
#define HAVE_OPENSSL_RSA_H 1
#define HAVE_OPENSSL_SSL_H 1
#define HAVE_OPENSSL_X509_H 1

/* DNS */
#define USE_THREADS_POSIX 1

/* Disabled protocols */
#define CURL_DISABLE_ALTSVC 1
#define CURL_DISABLE_DICT 1
#define CURL_DISABLE_FILE 1
#define CURL_DISABLE_FTP 1
#define CURL_DISABLE_GOPHER 1
#define CURL_DISABLE_IMAP 1
#define CURL_DISABLE_IPFS 1
#define CURL_DISABLE_LDAP 1
#define CURL_DISABLE_LDAPS 1
#define CURL_DISABLE_MQTT 1
#define CURL_DISABLE_NEGOTIATE_AUTH 1
#define CURL_DISABLE_POP3 1
#define CURL_DISABLE_RTSP 1
#define CURL_DISABLE_SMB 1
#define CURL_DISABLE_SMTP 1
#define CURL_DISABLE_TELNET 1
#define CURL_DISABLE_TFTP 1

/* CA */
#define CURL_CA_FALLBACK 1
