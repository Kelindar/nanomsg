/*
    Copyright (c) 2012 Martin Sustrik  All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include "../src/nn.h"
#include "../src/pubsub.h"

#define SERVER "server"
#define CLIENT "client"

char *date()
{
	time_t raw = time(&raw);
	struct tm *info = localtime(&raw);
	char *text = asctime(info);
	text[strlen(text) - 1] = '\0'; // remove '\n'
	return text;
}

int server(const char *url)
{
	int sock = nn_socket(AF_SP, NN_PUB);
	assert(sock >= 0);
	assert(nn_bind(sock, url) >= 0);

	char d[] = "Mhello.world";
	int sz_d = strlen(&d) + 1; // '\0' too

	while (1)
	{
		//char *d = date();
		//int sz_d = strlen(d) + 1; // '\0' too
		//printf("SERVER: PUBLISHING DATE %s\n", d);
		printf("SERVER: PUBLISHING '%s'\n", d);
		int bytes = nn_send(sock, &d, sz_d, 0);
		assert(bytes == sz_d);

		// Receive and wait
		char *buf = NULL;
		nn_recv(sock, &buf, NN_MSG, NN_DONTWAIT);
		Sleep(1000);
	}
	return nn_shutdown(sock, 0);
}

int client(const char *url, const char *name)
{
	int sock = nn_socket(AF_SP, NN_SUB);
	assert(sock >= 0);
	// TODO learn more about publishing/subscribe keys
	assert(nn_setsockopt(sock, NN_SUB, NN_SUB_SUBSCRIBE, "", 0) >= 0);
	assert(nn_connect(sock, url) >= 0);

	// Subscribe on start
	char d[] = "Shello.";
	int sz_d = strlen(&d) + 1; // '\0' too
	nn_send(sock, &d, sz_d, 0);

	while (1)
	{
		char *buf = NULL;
		int bytes = nn_recv(sock, &buf, NN_MSG, 0);
		assert(bytes >= 0);
		printf("CLIENT (%s): RECEIVED %s\n", name, buf);
		nn_freemsg(buf);
	}
	return nn_shutdown(sock, 0);
}

int main(const int argc, const char **argv)
{
	if (strncmp(SERVER, argv[1], strlen(SERVER)) == 0 && argc >= 2)
		return server(argv[2]);
	else if (strncmp(CLIENT, argv[1], strlen(CLIENT)) == 0 && argc >= 3)
		return client(argv[2], argv[3]);
	else
	{
		fprintf(stderr, "Usage: pubsub %s|%s <URL> <ARG> ...\n",
			SERVER, CLIENT);
		return 1;
	}
}