#![no_std]
#![allow(static_mut_refs)]

use core::ffi::c_int;
use core::panic::PanicInfo;

use smoltcp::iface::{Config, Interface, SocketHandle, SocketSet, SocketStorage};
use smoltcp::phy::{Device, DeviceCapabilities, Medium, RxToken, TxToken};
use smoltcp::time::Instant;
use smoltcp::wire::{EthernetAddress, HardwareAddress, IpAddress, IpCidr, Ipv4Address};
use smoltcp::socket::tcp;

extern "C" {
    fn c_kputs(msg: *const u8);
    fn e1000_rx_poll(buf: *mut u8, maxlen: c_int) -> c_int;
    fn e1000_tx(buf: *const u8, len: c_int) -> c_int;
    fn net_time_msec() -> u64;
}

/// Simple helper: print a null-terminated static string.
unsafe fn puts_const(msg: &'static [u8]) {
    c_kputs(msg.as_ptr());
}

// ---- Configuration constants ----

const MAX_FRAME_SIZE: usize = 2048;
const NUM_SOCKETS: usize = 4;

const MY_MAC_BYTES: [u8; 6] = [0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee];
const MY_IP_V4: Ipv4Address = Ipv4Address::new(10, 0, 3, 2);

// TCP buffers (single server socket)
const TCP_RX_BUF_SIZE: usize = 2048;
const TCP_TX_BUF_SIZE: usize = 2048;

static mut TCP_RX_DATA: [u8; TCP_RX_BUF_SIZE] = [0; TCP_RX_BUF_SIZE];
static mut TCP_TX_DATA: [u8; TCP_TX_BUF_SIZE] = [0; TCP_TX_BUF_SIZE];

// Handle for the single TCP server socket
static mut TCP_HANDLE: Option<SocketHandle> = None;

// ---- Time helper ----

fn now() -> Instant {
    let ms = unsafe { net_time_msec() };
    Instant::from_millis(ms as i64)
}

// ---- Device + tokens ----

struct Xv6Device {
    rx_buf: [u8; MAX_FRAME_SIZE],
    rx_len: usize,
    tx_buf: [u8; MAX_FRAME_SIZE],
}

impl Xv6Device {
    const fn new() -> Self {
        Self {
            rx_buf: [0; MAX_FRAME_SIZE],
            rx_len: 0,
            tx_buf: [0; MAX_FRAME_SIZE],
        }
    }
}

struct Xv6RxToken<'a> {
    buffer: &'a [u8],
}

struct Xv6TxToken<'a> {
    buffer: &'a mut [u8],
}

impl<'a> RxToken for Xv6RxToken<'a> {
    fn consume<R, F>(self, f: F) -> R
    where
        F: FnOnce(&[u8]) -> R,
    {
        f(self.buffer)
    }
}

impl<'a> TxToken for Xv6TxToken<'a> {
    fn consume<R, F>(self, len: usize, f: F) -> R
    where
        F: FnOnce(&mut [u8]) -> R,
    {
        let slice = &mut self.buffer[..len];
        let result = f(slice);

        unsafe {
            puts_const(b"rust: smoltcp TxToken::consume, sending frame\n\0");
            e1000_tx(slice.as_ptr(), len as c_int);
        }

        result
    }
}

impl Device for Xv6Device {
    type RxToken<'a> = Xv6RxToken<'a> where Self: 'a;
    type TxToken<'a> = Xv6TxToken<'a> where Self: 'a;

    fn capabilities(&self) -> DeviceCapabilities {
        let mut caps = DeviceCapabilities::default();
        caps.medium = Medium::Ethernet;
        caps.max_transmission_unit = MAX_FRAME_SIZE as usize;
        caps
    }

    fn receive(
        &mut self,
        _timestamp: Instant,
    ) -> Option<(Self::RxToken<'_>, Self::TxToken<'_>)> {
        unsafe { puts_const(b"rust: smoltcp receive() got frame\n\0"); }

        self.rx_len = 0;
        let n = unsafe { e1000_rx_poll(self.rx_buf.as_mut_ptr(), MAX_FRAME_SIZE as c_int) };
        if n <= 0 {
            return None;
        }

        self.rx_len = n as usize;
        let rx = Xv6RxToken {
            buffer: &self.rx_buf[..self.rx_len],
        };
        let tx = Xv6TxToken {
            buffer: &mut self.tx_buf[..],
        };
        Some((rx, tx))
    }

    fn transmit(&mut self, _timestamp: Instant) -> Option<Self::TxToken<'_>> {
        unsafe { puts_const(b"rust: smoltcp transmit() called\n\0"); }
        Some(Xv6TxToken {
            buffer: &mut self.tx_buf[..],
        })
    }
}

// ---- Global interface + sockets + device ----

static mut DEVICE: Xv6Device = Xv6Device::new();
static mut IFACE: Option<Interface> = None;
static mut SOCKET_SET: Option<SocketSet<'static>> = None;
static mut SOCKET_STORAGE: [SocketStorage<'static>; NUM_SOCKETS] =
    [SocketStorage::EMPTY; NUM_SOCKETS];

#[no_mangle]
pub extern "C" fn rust_test() {
    static MSG: &[u8] = b"Rust smoltcp module loaded\n\0";
    unsafe { puts_const(MSG) }
}

#[no_mangle]
pub extern "C" fn rust_net_init() {
    unsafe {
        puts_const(b"rust: smoltcp rust_net_init\n\0");
    }

    unsafe {
        let dev: &mut Xv6Device = &mut DEVICE;

        let hw = HardwareAddress::Ethernet(EthernetAddress(MY_MAC_BYTES));
        let mut cfg = Config::new(hw);
        cfg.random_seed = 0x1234_5678;

        let mut iface = Interface::new(cfg, dev, now());

        // IP 10.0.3.2/24
        iface.update_ip_addrs(|ip_addrs| {
            ip_addrs
                .push(IpCidr::new(IpAddress::Ipv4(MY_IP_V4), 24))
                .unwrap();
        });

        // SocketSet + single TCP socket (initially not listening)
        let mut sockets = SocketSet::new(&mut SOCKET_STORAGE[..]);

        let rx_buf = tcp::SocketBuffer::new(&mut TCP_RX_DATA[..]);
        let tx_buf = tcp::SocketBuffer::new(&mut TCP_TX_DATA[..]);
        let sock = tcp::Socket::new(rx_buf, tx_buf);

        let handle = sockets.add(sock);
        TCP_HANDLE = Some(handle);

        IFACE = Some(iface);
        SOCKET_SET = Some(sockets);
    }
}

/// Called from e1000_intr whenever there is RX/TX work to do.
#[no_mangle]
pub extern "C" fn rust_net_poll() {
    unsafe {
        let iface = match IFACE.as_mut() {
            Some(i) => i,
            None => return,
        };
        let sockets = match SOCKET_SET.as_mut() {
            Some(s) => s,
            None => return,
        };
        let dev: &mut Xv6Device = &mut DEVICE;

        let ts = now();
        let _ = iface.poll(ts, dev, sockets);
    }
}

// ---- Networking FFI for sysnet.c ----
//
// All of these are non-blocking:
// - net_listen(port): 0 on success, -1 on error
// - net_accept(): 1 if Established, 0 if not yet, -1 on error
// - net_recv(buf, len): n>0 bytes read, 0 if no data, -1 on error
// - net_send(buf, len): n>0 bytes queued, 0 if would-block, -1 on error
// - net_close(): close socket (ignore errors)

#[no_mangle]
pub extern "C" fn rust_net_listen(port: i32) -> i32 {
    unsafe {
        let sockets = match SOCKET_SET.as_mut() {
            Some(s) => s,
            None => return -1,
        };
        let handle = match TCP_HANDLE {
            Some(h) => h,
            None => return -1,
        };
        let socket = sockets.get_mut::<tcp::Socket>(handle);
        match socket.listen(port as u16) {
            Ok(()) => 0,
            Err(_) => -1,
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_net_accept() -> i32 {
    unsafe {
        let sockets = match SOCKET_SET.as_mut() {
            Some(s) => s,
            None => return -1,
        };
        let handle = match TCP_HANDLE {
            Some(h) => h,
            None => return -1,
        };
        let socket = sockets.get_mut::<tcp::Socket>(handle);
        use smoltcp::socket::tcp::State;
        match socket.state() {
            State::Established => 1,
            State::Listen | State::SynReceived | State::SynSent => 0,
            _ => 0,
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_net_recv(buf: *mut u8, len: i32) -> i32 {
    if buf.is_null() || len <= 0 {
        return -1;
    }
    unsafe {
        let sockets = match SOCKET_SET.as_mut() {
            Some(s) => s,
            None => return -1,
        };
        let handle = match TCP_HANDLE {
            Some(h) => h,
            None => return -1,
        };
        let socket = sockets.get_mut::<tcp::Socket>(handle);

        if !socket.may_recv() {
            return 0;
        }

        let max_len = len as usize;
        let mut out_n: i32 = 0;

        let res = socket.recv(|data| {
            let n = core::cmp::min(data.len(), max_len);
            if n > 0 {
                core::ptr::copy_nonoverlapping(data.as_ptr(), buf, n);
            }
            out_n = n as i32;
            // consume n bytes, return n as the closure result
            (n, ())
        });

        if res.is_err() {
            -1
        } else {
            out_n
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_net_send(buf: *const u8, len: i32) -> i32 {
    if buf.is_null() || len <= 0 {
        return -1;
    }
    unsafe {
        let sockets = match SOCKET_SET.as_mut() {
            Some(s) => s,
            None => return -1,
        };
        let handle = match TCP_HANDLE {
            Some(h) => h,
            None => return -1,
        };
        let socket = sockets.get_mut::<tcp::Socket>(handle);

        // Only send if TCP state/window allows
        if !socket.may_send() {
            return 0; // would block / not ready
        }

        let slice = core::slice::from_raw_parts(buf, len as usize);

        match socket.send_slice(slice) {
            Ok(n) => n as i32, // actual bytes queued
            Err(_) => -1,
        }
    }
}


#[no_mangle]
pub extern "C" fn rust_net_close() {
    unsafe {
        let sockets = match SOCKET_SET.as_mut() {
            Some(s) => s,
            None => return,
        };
        let handle = match TCP_HANDLE {
            Some(h) => h,
            None => return,
        };
        let socket = sockets.get_mut::<tcp::Socket>(handle);
        socket.close();
    }
}

// ---- Panic handler ----

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    unsafe {
        puts_const(b"rust: panic in smoltcp module\n\0");
    }
    loop {}
}


