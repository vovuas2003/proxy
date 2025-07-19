import random
import asyncio
import sys

BLOCKED = []
TASKS = []
TASKS_CLEANUP_LOCK = asyncio.Lock()
TASKS_CLEANUP_TIMEOUT_SECONDS = 60 * 5 # cleanup period = 5 min

def main():
    try:
        unsafe_main()
    except KeyboardInterrupt:
        print()
        print("Goodbye!")
    except Exception:
        print()
        print("Unknown error, emergency exit!")
        print("Double check the format of ip and port in command line options if this message appears right after proxy startup!")

def unsafe_main():
    global BLOCKED
    args = sys.argv
    if len(args) != 3:
        print(f"Usage: {args[0]} ip port")
        return
    else:
        try:
            host = args[1]
            port = int(args[2])
        except:
            print(f"Incorrect ip {args[1]} or port {args[2]}, maybe non-integer port!")
            return
        try:
            with open("blacklist.txt", "r", encoding = "utf-8") as f:
                BLOCKED = [line.rstrip().encode() for line in f]
            print(f"Only blacklist.txt fragmentation ({len(BLOCKED)} URLs)!")
        except:
            BLOCKED = None
            print("Fragmentation for all HTTPS traffic (TCP port 443), can't open blacklist.txt!")
    print()
    print(f"Starting proxy server on {host}:{port}, ctrl+c to shutdown.")
    asyncio.run(almost_main(host, port))

async def almost_main(host, port):
    server = await asyncio.start_server(new_conn, host, port)
    asyncio.create_task(cleanup_tasks())
    await server.serve_forever()

async def cleanup_tasks():
    global TASKS
    while True:
        await asyncio.sleep(TASKS_CLEANUP_TIMEOUT_SECONDS)
        async with TASKS_CLEANUP_LOCK:
            #print("Before cleanup: tasks len = ", len(TASKS), " sizeof (bytes) = ", sys.getsizeof(TASKS)) # cleanup debug
            TASKS = [t for t in TASKS if not t.done()]
            #print("After cleanup: tasks len = ", len(TASKS), " sizeof (bytes) = ", sys.getsizeof(TASKS)) # cleanup debug

async def pipe(reader, writer):
    while not reader.at_eof() and not writer.is_closing():
        try:
            writer.write(await reader.read(1500))
            await writer.drain()
        except:
            break
    writer.close()

async def new_conn(local_reader, local_writer):
    http_data = await local_reader.read(1500)
    if not http_data:
        local_writer.close()
        return
    try:
        type, target = http_data.split(b"\r\n")[0].split(b" ")[0:2]
        host, port = target.split(b":")
    except:
        local_writer.close()
        return
    if type != b"CONNECT":
        local_writer.close()
        return
    local_writer.write(b"HTTP/1.1 200 OK\n\n")
    await local_writer.drain()
    try:
        remote_reader, remote_writer = await asyncio.open_connection(host, port)
    except:
        local_writer.close()
        return
    if port == b"443":
        await fragment_data(local_reader, remote_writer)
    TASKS.append(asyncio.create_task(pipe(local_reader, remote_writer)))
    TASKS.append(asyncio.create_task(pipe(remote_reader, local_writer)))
    #print("New conn, tasks len = ", len(TASKS), " sizeof (bytes) = ", sys.getsizeof(TASKS)) # cleanup debug

async def fragment_data(local_reader, remote_writer):
    try:
        head = await local_reader.read(5)
        data = await local_reader.read(2048)
    except:
        local_reader.close()
        return
    parts = []
    if BLOCKED != None:
        if all(data.find(site) == -1 for site in BLOCKED):
            remote_writer.write(head + data)
            await remote_writer.drain()
            return
    host_end_index = data.find(b"\x00")
    if host_end_index != -1:
        parts.append(bytes.fromhex("160304") + int(host_end_index + 1).to_bytes(2, byteorder="big") + data[: host_end_index + 1])
        data = data[host_end_index + 1:]
    while data:
        part_len = random.randint(1, len(data))
        parts.append(bytes.fromhex("160304") + int(part_len).to_bytes(2, byteorder="big") + data[0:part_len])
        data = data[part_len:]
    remote_writer.write(b"".join(parts))
    await remote_writer.drain()

if __name__ == "__main__":
    main()
