//! Haiku's Mio poll+pipe backend must not lose executor notifications.
use mio::{Events, Interest, Poll, Token, Waker};
use std::time::Duration;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
fn wake_regression() {
    let mut poll = Poll::new().unwrap();
    let waker = Waker::new(poll.registry(), Token(99)).unwrap();
    let mut socket = mio::net::UdpSocket::bind("127.0.0.1:0".parse().unwrap()).unwrap();
    let mut events = Events::with_capacity(8);
    for action in 0..3 {
        waker.wake().unwrap();
        match action {
            0 => poll.registry().register(&mut socket, Token(1), Interest::READABLE).unwrap(),
            1 => poll.registry().reregister(&mut socket, Token(1), Interest::READABLE).unwrap(),
            _ => poll.registry().deregister(&mut socket).unwrap(),
        }
        poll.poll(&mut events, Some(Duration::from_millis(100))).unwrap();
        assert!(events.iter().any(|e| e.token() == Token(99) && e.is_readable()),
            "LOST WAKE: socket operation {action} consumed executor notification");
    }
    println!("poll: wake survives register, reregister and deregister");
}
async fn sockets() {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let address = listener.local_addr().unwrap();
    let server = tokio::spawn(async move {
        let mut jobs = Vec::new();
        for _ in 0..128 {
            let (mut socket, _) = listener.accept().await.unwrap();
            jobs.push(tokio::spawn(async move {
                let mut value = [0; 8];
                socket.read_exact(&mut value).await.unwrap();
                socket.write_all(&value).await.unwrap();
            }));
        }
        for job in jobs { job.await.unwrap(); }
    });
    let mut clients = Vec::new();
    for id in 0u64..128 {
        clients.push(tokio::spawn(async move {
            // Blocking DB-style work must yield and then wake the executor.
            let payload = tokio::task::spawn_blocking(move || id.to_be_bytes()).await.unwrap();
            let mut socket = tokio::net::TcpStream::connect(address).await.unwrap();
            for byte in payload {
                socket.write_all(&[byte]).await.unwrap();
                tokio::time::sleep(Duration::from_millis(1)).await;
            }
            let mut reply = [0; 8];
            socket.read_exact(&mut reply).await.unwrap();
            assert_eq!(reply, payload);
        }));
    }
    for client in clients { client.await.unwrap(); }
    server.await.unwrap();
    // Exhaust sockets, leave the runtime idle, then wake it from a blocking task.
    tokio::task::spawn_blocking(|| std::thread::sleep(Duration::from_millis(50))).await.unwrap();
    tokio::time::sleep(Duration::from_millis(20)).await;
}
fn main() {
    // Independent watchdog still exits if the executor AND its timers stop.
    std::thread::spawn(|| { std::thread::sleep(Duration::from_secs(45)); std::process::exit(2); });
    wake_regression();
    for workers in [2, 4] {
        let runtime = tokio::runtime::Builder::new_multi_thread().worker_threads(workers).enable_all().build().unwrap();
        runtime.block_on(sockets());
        runtime.shutdown_timeout(Duration::from_secs(2));
        println!("runtime: {workers} workers, 128 TCP exchanges, blocking work, timers and shutdown OK");
    }
}
