use std::net::UdpSocket;

//return Result, allows us to use ? error propagation in fn 
pub fn udp_server_init() -> std::io::Result<()> {
    {
        let socket = UdpSocket::bind("192.168.4.2:34254")?;

        // Receives a single datagram message on the socket. If `buf` is too small to hold
        // the message, it will be cut off.
        let mut buf = [0; 256];
        let (amt, src) = socket.recv_from(&mut buf)?;

        let buf = &buf[..amt]; //reference only to received data

        println!("buf raw received : {:?}, size: {}, from: {:?}", buf, amt, src);
        println!("{}", String::from_utf8_lossy(buf));
    } // the socket is closed here
    Ok(())
}