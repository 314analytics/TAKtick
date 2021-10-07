/*
    TAKtick: quick and dirty multi-platform CoT/TAK TCP server
             which echoes back entire messages to all participants

    Copyright (C) 2021 Peter Lawrence

    Permission is hereby granted, free of charge, to any person obtaining a 
    copy of this software and associated documentation files (the "Software"), 
    to deal in the Software without restriction, including without limitation 
    the rights to use, copy, modify, merge, publish, distribute, sublicense, 
    and/or sell copies of the Software, and to permit persons to whom the 
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in 
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
    DEALINGS IN THE SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <assert.h>

#if defined(_MSC_VER) || defined(__MINGW32__)
	#include <windows.h>
	#include <conio.h>
	enum homegrown_bool
	{
		false = 0,
		true = 1,
	};
	typedef enum homegrown_bool bool;
	#ifndef MSG_NOSIGNAL
		#define MSG_NOSIGNAL 0
	#endif
#else
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <unistd.h>
	#include <errno.h>
	#include <fcntl.h>
	typedef struct sockaddr * LPSOCKADDR;
	typedef int SOCKET;
	typedef struct sockaddr_in SOCKADDR_IN;
	#include <stdbool.h>
	#include <termios.h>
	#include <unistd.h>
	#include <signal.h>
#endif

static const char *terminator_string = "</event>";
static const int terminator_length = 8;
static const int buffer_chunk_size = 65536;

struct server_context_type
{
	struct participant_list_struct *participant_list_base;
	int participant_count;
};

struct participant_list_struct
{
	SOCKET socket;
	bool closed;
	char *buffer;
	int length, max_length;
	struct participant_list_struct *next;
};

/* local function prototypes */
static void add_participant(SOCKET listen_socket, struct server_context_type *ctx);
static void service_participants(fd_set *state, struct server_context_type *ctx);
static void terminate_participants(struct server_context_type *ctx, bool forceall);
static void parse_data(struct participant_list_struct *participant, struct server_context_type *ctx);
static SOCKET set_reads(fd_set *state, SOCKET highest_socket, struct server_context_type *ctx);
static void set_nonblocking(SOCKET sock);
static void share_data(const char *buffer, int length, struct server_context_type *ctx);
static void *memmem(const void *haystack, size_t haystacklen, const void * const needle, const size_t needlelen);
static void changemode(int dir);
#if !defined(_MSC_VER) && !defined(__MINGW32__)
static int _kbhit(void);
static void intHandler(int);
#endif

int main (int argc, char *argv[])
{
	int rc;
#if defined(_MSC_VER) || defined(__MINGW32__)
	WSADATA wsaData;
#endif
	SOCKADDR_IN local;
	SOCKET local_socket, highest_socket;
	fd_set reads, writes;
	struct server_context_type ctx;
	char ch;
	struct timeval tv;

	if (argc < 2)
	{
		fprintf(stderr, "%s <portno_listen>\n", argv[0]);
		return -1;
	}

	ctx.participant_list_base = NULL;
	ctx.participant_count = 0;

#if defined(_MSC_VER) || defined(__MINGW32__)
	/* Initialize WinSock and check the version */
	rc = WSAStartup(MAKEWORD(2,0), &wsaData);

	/* check the version */
	if (MAKEWORD(2,0) != wsaData.wVersion)
	{
		fprintf(stderr, "ERROR: WSAStartup() failed\n");
		goto finished_nochangemode;
	}
#endif

	/* establish a socket to listen for incoming connections */
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = htons((unsigned short)atoi(argv[1]));

	local_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

#if defined(_MSC_VER) || defined(__MINGW32__)
	if (INVALID_SOCKET == local_socket)
#else
	if (local_socket <= 0)
#endif
		goto finished_nochangemode;

	rc = bind(local_socket, (LPSOCKADDR)&local, sizeof(local));

	if (rc)
	{
		fprintf(stderr, "ERROR: unable to bind(); the socket may already be in use or is in timeout\n");
		goto finished_nochangemode;
	}

	rc = listen(local_socket, SOMAXCONN);

	if (rc)	goto finished_nochangemode;

	printf("Press 'Q' to exit program\n");
	changemode(1); /* disable keyboard echo */
#if !defined(_MSC_VER) && !defined(__MINGW32__)
	signal(SIGINT, intHandler);
#endif

	for (;;)
	{
		/* FD_SET "reads" with all the sockets we are listening on */
		FD_ZERO(&reads);
		FD_SET(local_socket, &reads);
		highest_socket = set_reads(&reads, local_socket, &ctx);
		FD_ZERO(&writes);

		/* block until something happens or timeout occurs */
		tv.tv_sec = 0;
		tv.tv_usec = 100000;
		rc = select(highest_socket + 1, &reads, &writes, NULL, &tv);

		if (_kbhit())
		{
#if defined(_MSC_VER) || defined(__MINGW32__)
			ch = getch();
#else
			ch = getchar();
#endif
			if ( ('q' == ch) || ('Q' == ch) ) break;

			printf("%d participants currently; press 'Q' to exit program\n", ctx.participant_count);
		}
		
		if (rc < 0) goto finished;

		if (rc > 0) /* rc is positive, indicating the number of sockets worthy of attention */
		{
			/* first, we check "local_socket", which will have activity if a new connection is attempted */
			if (FD_ISSET(local_socket, &reads))
				add_participant(local_socket, &ctx);

			/* cycle through all the participants, processing all incoming data and closing terminated sockets */
			service_participants(&reads, &ctx);
		}

	}

	/* mop up any remaining sockets */
	terminate_participants(&ctx, true);

finished:
	changemode(0); /* re-enable keyboard echo */

finished_nochangemode:
	return 0;
}

/* accept() new socket and add new incoming participant to list */

static void add_participant(SOCKET listen_socket, struct server_context_type *ctx)
{
	SOCKET participant_socket;
	struct participant_list_struct *pnt, *prev_pnt, *new_entry;

	participant_socket = accept(listen_socket, NULL, NULL);

#if defined(_MSC_VER) || defined(__MINGW32__)
	if (INVALID_SOCKET == participant_socket) return;
#else
	if (participant_socket <= 0) return;
#endif

	/* set for non-blocking, as we will use select() to achieve blocking */
	set_nonblocking(participant_socket);

	/* search through participant list to for an existing entry */

	pnt = ctx->participant_list_base;
	prev_pnt = NULL;

	while (pnt)
	{
		if (pnt->socket == participant_socket)
			break;
		prev_pnt = pnt;
		pnt = pnt->next;
	}

	if (pnt)
	{
		return;
	}

	/* create a new entry for this new participant */

	new_entry = (struct participant_list_struct *)malloc(sizeof(struct participant_list_struct));
	assert(new_entry);
	memset(new_entry, 0, sizeof(struct participant_list_struct));
	new_entry->socket = participant_socket;
	new_entry->closed = false;
	new_entry->max_length = new_entry->length = 0;
	new_entry->buffer = NULL;

	if (NULL == prev_pnt)
		ctx->participant_list_base = new_entry;
	else
		prev_pnt->next = new_entry;

	ctx->participant_count++;
}

static void terminate_participants(struct server_context_type *ctx, bool force_all)
{
	struct participant_list_struct *pnt, *prev_pnt, *next_pnt;

	pnt = ctx->participant_list_base;
	prev_pnt = NULL;

	while (pnt)
	{
		next_pnt = pnt->next;

		if (pnt->closed || force_all)
		{
#if defined(_MSC_VER) || defined(__MINGW32__)
			closesocket(pnt->socket);
#else
			close(pnt->socket);
#endif

			ctx->participant_count--;

			if (prev_pnt)
				prev_pnt->next = pnt->next;
			else
				ctx->participant_list_base = pnt->next;

			if (pnt->buffer) free(pnt->buffer);
			free(pnt);
		}
		else
		{
			prev_pnt = pnt;
		}

		pnt = next_pnt;
	}
}

static void service_participants(fd_set *state, struct server_context_type *ctx)
{
	struct participant_list_struct *pnt;

	/*
	sequence through each entry in the linked list
	if state indicates that this socket should be polled, we call parse_data() for it
	*/

	pnt = ctx->participant_list_base;

	while (pnt)
	{
		if (FD_ISSET(pnt->socket, state))
			parse_data(pnt, ctx);

		pnt = pnt->next;
	}

	/*
	sequence again through each entry in the linked list
	if 'closed' indicates that this socket should be closed, we do so
	*/

	terminate_participants(ctx, false);
}

static void parse_data(struct participant_list_struct *participant, struct server_context_type *ctx)
{
	int numRead, onset;
	char *pnt;

	do
	{
		if ( (participant->max_length <= 0) || ((participant->length + buffer_chunk_size) > participant->max_length) )
		{
			/* we'll likely run out of buffer space, so re-allocate more (2x as much) */
			participant->max_length = (participant->max_length <= 0) ? buffer_chunk_size : (participant->max_length << 1);
			participant->buffer = realloc(participant->buffer, participant->max_length);
			assert(participant->buffer);
		}

		numRead = recv(participant->socket, participant->buffer + participant->length, participant->max_length - participant->length, 0);

		switch (numRead)
		{
		case -1:
#if defined(_MSC_VER) || defined(__MINGW32__)
			if (WSAEWOULDBLOCK == WSAGetLastError())
#else
			if (EAGAIN == errno)
#endif
				break;
		case 0:
			participant->closed = true;
			break;
		default:
			onset = (participant->length > terminator_length) ? (participant->length - terminator_length) : 0;
			participant->length += numRead;
			if (pnt = memmem(participant->buffer + onset, participant->length - onset, terminator_string, terminator_length))
			{
				int size = pnt + terminator_length - participant->buffer;
				share_data(participant->buffer, size, ctx);
				participant->length -= size;
				memmove(participant->buffer, pnt + terminator_length, participant->length);
			}
			break;
		}

	} while (numRead > 0);
}

/* utility function to FD_SET all sockets in the participant list, and track the highest socket (for select()) */

static SOCKET set_reads(fd_set *state, SOCKET highest_socket, struct server_context_type *ctx)
{
	struct participant_list_struct *pnt;

	pnt = ctx->participant_list_base;

	while (pnt)
	{
		FD_SET(pnt->socket, state);

		if (pnt->socket > highest_socket)
			highest_socket = pnt->socket;

		pnt = pnt->next;
	}

	return highest_socket;
}

/* utility function to configure a socket as non-blocking */

static void set_nonblocking(SOCKET sock)
{
#if defined(_MSC_VER) || defined(__MINGW32__)
	u_long nonblocking = 1;
	ioctlsocket(sock, FIONBIO, &nonblocking);
#else
	int opts;

	opts = fcntl(sock, F_GETFL);

	if (opts > 0)
	{
		opts = (opts | O_NONBLOCK);
		fcntl(sock, F_SETFL, opts);
	}
#endif
}

/* send provided message to all participants */

static void share_data(const char *buffer, int length, struct server_context_type *ctx)
{
	struct participant_list_struct *pnt;
	int outcome;

	pnt = ctx->participant_list_base;

	while (pnt)
	{
		outcome = send(pnt->socket, buffer, length, MSG_NOSIGNAL);

		if (outcome <= 0) pnt->closed = true;

		pnt = pnt->next;
	}
}

/* bounded equivalent to strstr(); only Linux gcc has an implementation, so we provide one */

static void *memmem(const void *haystack, size_t haystacklen, const void * const needle, const size_t needlelen)
{
	const char *h;

	if ( (haystack == NULL) || (needle == NULL) ) return NULL;
	if ( (haystacklen == 0) || (needlelen == 0) ) return NULL;

	for (h = haystack; haystacklen >= needlelen; ++h, --haystacklen)
	{
		if (!memcmp(h, needle, needlelen)) return (void *)h;
	}
	return NULL;
}

static void changemode(int dir)
{
#if defined(_MSC_VER) || defined(__MINGW32__)
	(void)dir;
#else
	static struct termios oldt, newt;

	if ( dir == 1 )
	{
		tcgetattr( STDIN_FILENO, &oldt);
		newt = oldt;
		newt.c_lflag &= ~( ICANON | ECHO );
		tcsetattr( STDIN_FILENO, TCSANOW, &newt);
	}
	else
		tcsetattr( STDIN_FILENO, TCSANOW, &oldt);
#endif
}

#if !defined(_MSC_VER) && !defined(__MINGW32__)
static int _kbhit(void)
{
	struct timeval tv;
	fd_set rdfs;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&rdfs);
	FD_SET (STDIN_FILENO, &rdfs);

	select(STDIN_FILENO+1, &rdfs, NULL, NULL, &tv);
	return FD_ISSET(STDIN_FILENO, &rdfs);
}
#endif

static void intHandler(int unused)
{
	(void)unused;
	changemode(0);
}

