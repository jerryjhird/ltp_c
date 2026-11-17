#define LIBLTP_IMPLEMENTATION
#include "libltp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080

int main(void) {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(1);
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // localhost
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(1);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        exit(1);
    }

    printf("LTP server listening on localhost:%d...\n", PORT);

    while (1) {
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
            perror("accept failed");
            continue;
        }

        size_t rx_cap = 1024;
        uint8_t *rx_buf = malloc(rx_cap);
        if (!rx_buf) {
            close(client_fd);
            continue;
        }

        ssize_t bytes_read = read(client_fd, rx_buf, rx_cap);
        if (bytes_read > 0) {
            ltp_request req = {0};
            ltp_response resp = {0};
            size_t tx_len = 0;

            if (ltp_decode_request(rx_buf, (size_t)bytes_read, &req) == LIBLTP_OK) {
                printf("[Server] recv %.*s request for path: %.*s\n", (int)req.method_len, req.method, (int)req.path_len, req.path);

                // standard ping (always returns 200 "PONG")
                if (ltp_method_is(&req, "PING")) {
                    ltp_set_response(&resp, 200, "PONG\n");
                }

                // standard EXT (returns extensions running server side that extend features of the protocol)
                else if (ltp_method_is(&req, "EXT")) {
                    const char *extensions = "HTCPCP/1.0\n";

                    if (extensions == NULL || extensions[0] == '\0') {
                        ltp_set_response(&resp, 200, "None\n");
                    } else {
                        ltp_set_response(&resp, 200, extensions);
                    }
                }

                else if (ltp_path_is(&req, "/")) {
                    // GET test
                    if (ltp_method_is(&req, "GET")) {
                        ltp_set_response(&resp, 200, "OK\n");
                    }
                    
                    // POST test
                    else if (ltp_method_is(&req, "POST")) {
                        printf("[Server] POST \"/\" (len %zu): %.*s\n", req.body_len, (int)req.body_len, req.body);
                        ltp_set_response(&resp, 200, "OK\n");
                    } 

                    else {
                        ltp_set_response(&resp, 405, "Method Not Allowed\n");
                    }
                }

                // custom methods work too
                else if (ltp_method_is(&req, "BREW") && ltp_path_is(&req, "/pot-0")) {
                    ltp_set_response(&resp, 418, "I'm a teapot\n");
                }

                // GET 404
                else {
                    ltp_set_response(&resp, 404, "Not Found\n");
                }
            } else {
                // BAD PACKET
                ltp_set_response(&resp, 500, "Bad Request\n");
            }

            size_t tx_cap = resp.body_len + 128;
            uint8_t *tx_buf = malloc(tx_cap);
            if (tx_buf) {
                if (ltp_encode_response(&resp, tx_buf, tx_cap, &tx_len) == LIBLTP_OK) {
                    write(client_fd, tx_buf, tx_len);
                }
                free(tx_buf);
            }
        }

        free(rx_buf);
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
