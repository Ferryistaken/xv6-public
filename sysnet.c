#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "syscall.h"

// Rust FFI functions, implemented in lib.rs
extern int  rust_net_listen(int port);
extern int  rust_net_accept(void);
extern int  rust_net_recv(char *buf, int len);
extern int  rust_net_send(char *buf, int len);
extern void rust_net_close(void);

// int net_listen(int port);
int
sys_net_listen(void)
{
  int port;
  if(argint(0, &port) < 0)
    return -1;
  if(port < 0 || port > 65535)
    return -1;
  return rust_net_listen(port);
}

// int net_accept(void);
// returns 1 if a connection is established, 0 if not yet, -1 on error
int
sys_net_accept(void)
{
  return rust_net_accept();
}

// int net_recv(void *buf, int len);
int
sys_net_recv(void)
{
  int len;
  char *buf;

  if(argptr(0, &buf, 0) < 0) // size checked below
    return -1;
  if(argint(1, &len) < 0)
    return -1;
  if(len < 0)
    return -1;

  return rust_net_recv(buf, len);
}

// int net_send(void *buf, int len);
int
sys_net_send(void)
{
  int len;
  char *buf;

  if(argptr(0, &buf, 0) < 0)
    return -1;
  if(argint(1, &len) < 0)
    return -1;
  if(len < 0)
    return -1;

  return rust_net_send(buf, len);
}

// int net_close(void);
int
sys_net_close(void)
{
  rust_net_close();
  return 0;
}

