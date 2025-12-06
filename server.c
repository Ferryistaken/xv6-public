#include "types.h"
#include "stat.h"
#include "user.h"

// Returns 1 if request body == "close"
static int
body_is_close(char *buf, int n)
{
  if(n <= 0) return 0;

  // find "\r\n\r\n" manually
  int i;
  for(i = 0; i+3 < n; i++){
    if(buf[i]   == '\r' &&
       buf[i+1] == '\n' &&
       buf[i+2] == '\r' &&
       buf[i+3] == '\n')
    {
      // body begins after the blank line
      int body_start = i + 4;

      // skip spaces/newlines
      while(body_start < n &&
           (buf[body_start] == ' ' ||
            buf[body_start] == '\n' ||
            buf[body_start] == '\r'))
        body_start++;

      // check for "close"
      if(body_start + 5 <= n &&
         buf[body_start]   == 'c' &&
         buf[body_start+1] == 'l' &&
         buf[body_start+2] == 'o' &&
         buf[body_start+3] == 's' &&
         buf[body_start+4] == 'e')
        return 1;

      return 0; // found body, but not "close"
    }
  }
  return 0; // didn't find end of headers
}

int
main(void)
{
  for(;;){
    // Put the TCP socket back into LISTEN state each iteration.
    // On first iteration the socket is Closed; later it is Closed again
    // after net_close().
    if(net_listen(80) < 0){
      printf(2, "httpd: net_listen(80) failed\n");
      exit();
    }

    printf(1, "httpd: listening on port 80\n");

    // Wait until the TCP connection reaches Established
    while(net_accept() == 0)
      sleep(10);

    printf(1, "httpd: connection established\n");

    char buf[512];
    int n = net_recv(buf, sizeof(buf));

    if(n > 0)
      printf(1, "httpd: received %d bytes\n", n);

    if(body_is_close(buf, n)){
      // Shutdown reply
      static const char hdr[] =
        "HTTP/1.0 200 OK\r\n"
        "Content-Length: 13\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n";
      static const char body[] = "shutting down\n"; // 13 bytes

      net_send((void*)hdr,  sizeof(hdr)  - 1);
      net_send((void*)body, sizeof(body) - 1);
      net_close();

      printf(1, "httpd: close command received, exiting.\n");
      exit();
    } else {
      // Normal reply
      static const char hdr[] =
        "HTTP/1.0 200 OK\r\n"
        "Content-Length: 13\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n";
      static const char body[] = "hi from user\n"; // 13 bytes

      net_send((void*)hdr,  sizeof(hdr)  - 1);
      net_send((void*)body, sizeof(body) - 1);
      net_close();

    }
  }
}

