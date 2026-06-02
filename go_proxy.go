package main

import (
	"encoding/hex"
	"fmt"
	"io"
	"math/rand"
	"net"
	"os"
	"strconv"
	"strings"
	"time"
)

func main() {
	if len(os.Args) != 3 {
		fmt.Printf("Usage: %s ip port\n", os.Args[0])
		return
	}
	host := os.Args[1]
	portStr := os.Args[2]
	port, err := strconv.Atoi(portStr)
	if err != nil {
		fmt.Printf("Incorrect port %s, maybe non-integer?\n", portStr)
		return
	}
	addr := net.JoinHostPort(host, strconv.Itoa(port))
	fmt.Printf("Starting proxy server on %s, ctrl+c to shutdown.\n", addr)
	listener, err := net.Listen("tcp", addr)
	if err != nil {
		fmt.Printf("Failed to listen on %s: %v\n", addr, err)
		return
	}
	defer listener.Close()
	rand.Seed(time.Now().UnixNano())
	for {
		localConn, err := listener.Accept()
		if err != nil {
			fmt.Printf("Accept error: %v\n", err)
			continue
		}
		go handleConnection(localConn)
	}
}

func handleConnection(localConn net.Conn) {
	defer localConn.Close()
	buf := make([]byte, 1500)
	n, err := localConn.Read(buf)
	if err != nil || n == 0 {
		return
	}
	data := buf[:n]
	lines := strings.SplitN(string(data), "\r\n", 2)
	if len(lines) < 1 {
		return
	}
	parts := strings.Split(lines[0], " ")
	if len(parts) < 2 {
		return
	}
	method := parts[0]
	target := parts[1]
	if method != "CONNECT" {
		return
	}
	hostPort := target
	host, portStr, err := net.SplitHostPort(hostPort)
	if err != nil {
		host = hostPort
		portStr = "443"
	}
	port, err := strconv.Atoi(portStr)
	if err != nil {
		return
	}
	_, err = localConn.Write([]byte("HTTP/1.1 200 OK\r\n\r\n"))
	if err != nil {
		return
	}
	remoteConn, err := net.Dial("tcp", net.JoinHostPort(host, portStr))
	if err != nil {
		return
	}
	defer remoteConn.Close()
	if port == 443 {
		err = fragmentData(localConn, remoteConn)
		if err != nil {
			return
		}
	}
	done := make(chan struct{}, 2)
	go pipe(localConn, remoteConn, done)
	go pipe(remoteConn, localConn, done)
	<-done
	<-done
}

func pipe(src net.Conn, dst net.Conn, done chan<- struct{}) {
	defer func() { done <- struct{}{} }()
	io.Copy(dst, src)
}

func extractSNIPosition(data []byte) (start, end int, found bool) {
	for i := 0; i < len(data)-8; i++ {
		if data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x00 && data[i + 4] == 0x00 && data[i + 6] == 0x00 && data[i + 7] == 0x00 {
			extLen := data[i + 3]
			listLen := data[i + 5]
			nameLen := data[i + 8]
			if int(extLen) - int(listLen) == 2 && int(listLen) - int(nameLen) == 3 {
				sniStart := i + 9
				sniEnd := sniStart + int(nameLen)
				return sniStart, sniEnd, true
			}
		}
	}
	return 0, 0, false
}

func fragmentData(localConn net.Conn, remoteConn net.Conn) error {
	head := make([]byte, 5)
	_, err := io.ReadFull(localConn, head)
	if err != nil {
		return err
	}
	data := make([]byte, 2048)
	n, err := localConn.Read(data)
	if err != nil && err != io.EOF {
		return err
	}
	data = data[:n]
	tlsHeaderPrefix := []byte{0x16, 0x03, 0x04}
	var parts [][]byte
	start, end, found := extractSNIPosition(data)
	if found {
		partStart := data[: start]
		sniData := data[start : end]
		partEnd := data[end :]
		makePart := func(payload []byte) []byte {
			lenBytes := []byte{byte(len(payload) >> 8), byte(len(payload) & 0xff)}
			part := append(tlsHeaderPrefix, lenBytes...)
			return append(part, payload...)
		}
		if len(partStart) > 0 {
			parts = append(parts, makePart(partStart))
		}
		for i := 0; i < len(sniData); i += 2 {
			chunkEnd := i + 2
			if chunkEnd > len(sniData) {
				chunkEnd = len(sniData)
			}
			chunk := sniData[i:chunkEnd]
			parts = append(parts, makePart(chunk))
		}
		if len(partEnd) > 0 {
			parts = append(parts, makePart(partEnd))
		}
	}
	for _, p := range parts {
		_, err := remoteConn.Write(p)
		if err != nil {
			return err
		}
	}
	return nil
}

