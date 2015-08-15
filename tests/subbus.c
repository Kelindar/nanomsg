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


#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../src/nn.h"
#include "../src/subbus.h"
#include "testutil.h"
#include <windows.h>


int node(const int argc, const char **argv)
{
	int sock = nn_socket(AF_SP, NN_SUBBUS);
	assert(sock >= 0);
	assert(nn_bind(sock, argv[2]) >= 0);
	Sleep(1); // wait for connections
	if (argc >= 3)
	{
		int x = 3;
		for (x; x < argc; x++) {
			assert(nn_connect(sock, argv[x]) >= 0);
			printf("Connecting to %s\n", argv[x]);
		}
	}
	Sleep(2000); // wait for connections
	int to = 100;

	assert(nn_setsockopt(sock, NN_SOL_SOCKET, NN_RCVTIMEO, &to, sizeof(to)) >= 0);
	//assert(nn_setsockopt(sock, NN_SUBBUS, NN_SUBBUS_SUBSCRIBE, &channel, sizeof(channel)) >= 0);

	nn_send(sock, "Stweet.", 12, 0);

	// SEND
	int sz_n = strlen(argv[1]) + 1; // '\0' too
	printf("%s: SENDING '%s' ONTO BUS\n", argv[1], argv[1]);
	int send = nn_send(sock, argv[1], sz_n, 0);
	assert(send == sz_n);
	while (1)
	{
		// RECV
		char *buf = NULL;
		int recv = nn_recv(sock, &buf, NN_MSG, 0);
		if (recv >= 0)
		{
			printf("%s: RECEIVED '%s' FROM BUS\n", argv[1], buf);
			nn_freemsg(buf);
		}

		Sleep(100);

		int send = nn_send(sock, argv[1], sz_n, 0);
	}
	return nn_shutdown(sock, 0);
}

int main(const int argc, const char **argv)
{
	if (argc >= 3) node(argc, argv);
	else
	{
		fprintf(stderr, "Usage: bus <NODE_NAME> <URL> <URL> ...\n");
		return 1;
	}
}