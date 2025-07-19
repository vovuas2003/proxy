# DISCLAMER: USE FOR EDUCATIONAL PURPOSES ONLY!!!

This is proxy server with TLS handshake fragmentation. Inspired by https://github.com/GVCoder09/NoDPI (fork of https://github.com/theo0x0/nodpi).

python_asyncio.py - almost https://github.com/GVCoder09/NoDPI/blob/main/src/sample_version.py

c_linux_pthread.c, c_windows_pthread.c and c_linux_fork.c - results of porting the python program to c using LLM and some brains

## How to run (check releases for binaries)

Note: python version supports selective fragmentation only for urls line-by-line in blacklist.txt (txt file must be in the same directory as the program).

### Python Windows (from cmd)
python python_asyncio.py ip port

Examples (local, "server", universal variants):

python python_asyncio.py 127.0.0.1 8080

python python_asyncio.py 192.168.0.1 55555

python python_asyncio.py 0.0.0.0 55555

### Python Windows (find exe from pyinstaller in releases):
python_asyncio.exe ip port

### Python Linux:
python3 python_asyncio.py ip port

### C versions

Windows:
compiled_binary.exe ip port

Linux (as well as routers):
./compiled_binary ip port

## How to use running proxy (examples)

1. path_to_google_chrome_startup --proxy-server="http://127.0.0.1:8080"

Where path_to_google_chrome_startup:

"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe" - example for Windows

google-chrome-stable - for Linux

2. specify proxy in Firefox settings (just search proxy in settings)

3. specify proxy in your OS on PC

4. specify proxy in WiFi Android settings

## How to use on router (even default vendor firmware)

0. Get access to router admin console (I used telnet, enabled it in router web settings, possible to use ssh or maybe uart).
1. Cross-compile c version for your router architecture. I used crosstool-ng on Debian to build mipsel uclibc binary (edit mips-unknown-linux-uclibc sample to little endian) from source code c_linux_fork.c (there are very strange small libpthread.so on my ASUS rt-n12 vP). To understand router architecture, endianness and libc use: uname -a, ls -la /lib /usr/lib, cat /proc/cpuinfo. Or copy any system binary/lib to Linux PC and use utility file (file router_binary). To transfer files from router to PC use sftp or base64 (in my case there was base64 in openssl on router).
2. Upload compiled proxy server to router. To choose path so save use df -h (I used tmpfs mounted on /tmp because no space available in root filesystem; tmpfs is RAM so file will be dissappear after router reboot). To transfer files from PC to router use: 0) sftp, 1) wget, 2) base64 or 3) printf. 1) Run on PC in directory with compiled proxy: python3 -m http.server 8080, then on router wget -O /tmp/my_proxy http://192.168.0.2:8080/name_of_compiled_binary, of course use your PC ip 2) on PC openssl base64 -e -in file_to_transfer, copy string and be ready to paste into concole (e.g. telnet in Putty), on router echo "copied base64 string" | openssl base64 -d -out name_to_save, BUT the size of copied string to console may be limited 3) write simple python script that opens binary file and prints string like \x01\xab (result looks like xxd), then printf "string" > name_to_save (same issues with the size of string to copy into console).
3. Be sure that there are no corruptions (check size using ls -la name_of_saved_file; compare: md5sum file_on_pc, openssl md5 file_on_router).
4. Run proxy (I did it in /tmp directory): chmod +x name_of_saved_file, ./name_of_saved_file ip port (I like to use 0.0.0.0 and 55555).
5. I used fork daemon version on router (binary name is my_proxy). It will survive after exit from telnet session. To monitore: ps | grep my_proxy (depends on your router, maybe ps -el), netstat -an | grep 55555, cat /proc/meminfo, free, top. To stop daemon: kill -2 -pid (IMPORTANT minus before pid; check pid right after deamon start using ps, because after client connection where will be a lot of processes; of course, you can use the smallest pid or check ppid (parent pid) or pgid depending on your router ps/top utilities capabilities).