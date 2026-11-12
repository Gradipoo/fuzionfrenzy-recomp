import os
import socket
import subprocess
import sys
import time

TYPE_OPEN = 1
TYPE_JOIN = 2
TYPE_DATA = 3
TYPE_ACCEPT = 4
TYPE_REJECT = 5
TYPE_JOINED = 6
TYPE_ROSTER = 7
TYPE_START = 8
TOKEN_LEN = 16

SERVER = os.path.abspath("test_temp/relay_server")


def recv(sock, timeout):
    sock.settimeout(timeout)
    try:
        data, addr = sock.recvfrom(4096)
        return data, addr
    except socket.timeout:
        return None, None


def start_server(port, max_sessions, idle_sec, abs_sec):
    proc = subprocess.Popen(
        [SERVER, str(port), str(max_sessions), str(idle_sec), str(abs_sec)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.3)
    if proc.poll() is not None:
        print("FAIL: relay server exited at start")
        sys.exit(1)
    return proc


def stop_server(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=2)


def udp():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    return sock


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def open_session(sock, port):
    sock.sendto(bytes([TYPE_OPEN]), ("127.0.0.1", port))
    data, _ = recv(sock, 2.0)
    if data is None or len(data) != 1 + TOKEN_LEN or data[0] != TYPE_ACCEPT:
        return None
    return data[1:]


def join_session(sock, port, token):
    sock.sendto(bytes([TYPE_JOIN]) + token, ("127.0.0.1", port))
    data, _ = recv(sock, 2.0)
    return data


def send_data(sock, port, token, payload):
    sock.sendto(bytes([TYPE_DATA]) + token + payload, ("127.0.0.1", port))


def send_start(sock, port, token):
    sock.sendto(bytes([TYPE_START]) + token, ("127.0.0.1", port))


def next_packet(sock, timeout):
    end = time.time() + timeout
    while time.time() < end:
        left = end - time.time()
        if left <= 0:
            break
        data, _ = recv(sock, left)
        if data is None:
            return None
        if len(data) >= 1 and data[0] == TYPE_ROSTER:
            continue
        return data
    return None


def expect_payload(sock, payload, timeout=1.0):
    end = time.time() + timeout
    while time.time() < end:
        left = end - time.time()
        if left <= 0:
            break
        data, _ = recv(sock, left)
        if data is None:
            return False
        if len(data) >= 1 and data[0] == TYPE_ROSTER:
            continue
        return data == payload
    return False


def test_forward_and_reject():
    port = free_port()
    proc = start_server(port, 32, 900, 10800)
    host = guest = attacker = None
    try:
        host = udp()
        guest = udp()
        attacker = udp()
        token = open_session(host, port)
        if token is None:
            print("FAIL: host open")
            sys.exit(1)
        joined = join_session(guest, port, token)
        if joined is None or len(joined) != 2 or joined[0] != TYPE_JOINED or joined[1] != 2:
            print("FAIL: guest join")
            sys.exit(1)

        send_data(host, port, token, b"HELLO123")
        if not expect_payload(guest, b"HELLO123"):
            print("FAIL: host payload")
            sys.exit(1)
        print("PASS: host payload")

        send_data(guest, port, token, b"BACK")
        if not expect_payload(host, b"BACK"):
            print("FAIL: guest payload")
            sys.exit(1)
        print("PASS: guest payload")

        send_data(attacker, port, token, b"MALICIOUS")
        data, _ = recv(guest, 0.4)
        if data is not None:
            print("FAIL: replacement payload delivered")
            sys.exit(1)
        data, _ = recv(host, 0.2)
        if data is not None:
            print("FAIL: replacement payload delivered to host")
            sys.exit(1)
        print("PASS: endpoint replacement dropped")

        send_data(host, port, token, b"STILL_WORKING")
        if not expect_payload(guest, b"STILL_WORKING"):
            print("FAIL: host lost after replacement attempt")
            sys.exit(1)
        print("PASS: original host still delivers")

        bad = join_session(attacker, port, b"\x11" * TOKEN_LEN)
        if bad != bytes([TYPE_REJECT]):
            print("FAIL: bad token was not rejected")
            sys.exit(1)
        print("PASS: bad token rejected")
    finally:
        stop_server(proc)
        for sock in (host, guest, attacker):
            if sock is not None:
                sock.close()


def test_host_start():
    port = free_port()
    proc = start_server(port, 32, 900, 10800)
    host = guest = late = None
    try:
        host = udp()
        guest = udp()
        late = udp()
        token = open_session(host, port)
        if token is None:
            print("FAIL: start-test open")
            sys.exit(1)
        joined = join_session(guest, port, token)
        if joined is None or len(joined) != 2 or joined[0] != TYPE_JOINED or joined[1] != 2:
            print("FAIL: start-test join")
            sys.exit(1)
        send_start(guest, port, token)
        if next_packet(guest, 1.0) != bytes([TYPE_REJECT]):
            print("FAIL: guest start was not rejected")
            sys.exit(1)
        print("PASS: guest start rejected")
        send_start(host, port, token)
        notice = bytes([TYPE_START, 0x03])
        if next_packet(host, 1.0) != notice or next_packet(guest, 1.0) != notice:
            print("FAIL: start notice")
            sys.exit(1)
        print("PASS: start notice")
        if join_session(late, port, token) != bytes([TYPE_REJECT]):
            print("FAIL: join after start was not rejected")
            sys.exit(1)
        print("PASS: join after start rejected")
        send_data(host, port, token, b"AFTER")
        if not expect_payload(guest, b"AFTER"):
            print("FAIL: data after start")
            sys.exit(1)
        if recv(late, 0.4)[0] is not None:
            print("FAIL: late seat got data")
            sys.exit(1)
        send_data(guest, port, token, b"BACK")
        if not expect_payload(host, b"BACK"):
            print("FAIL: guest data after start")
            sys.exit(1)
        print("PASS: data stays with the started seats")
    finally:
        stop_server(proc)
        for sock in (host, guest, late):
            if sock is not None:
                sock.close()


def test_four_and_fifth():
    port = free_port()
    proc = start_server(port, 1, 900, 10800)
    socks = []
    try:
        host = udp()
        socks.append(host)
        token = open_session(host, port)
        if token is None:
            print("FAIL: four-player open")
            sys.exit(1)
        guests = []
        for seat in (2, 3, 4):
            guest = udp()
            socks.append(guest)
            guests.append(guest)
            joined = join_session(guest, port, token)
            if joined is None or len(joined) != 2 or joined[0] != TYPE_JOINED or joined[1] != seat:
                print("FAIL: seat", seat, "join")
                sys.exit(1)
        send_data(host, port, token, b"FAN")
        for guest in guests:
            if not expect_payload(guest, b"FAN"):
                print("FAIL: host fan-out")
                sys.exit(1)
        print("PASS: host fan-out")
        send_data(guests[1], port, token, b"FROM3")
        if not expect_payload(host, b"FROM3"):
            print("FAIL: seat 3 did not reach the host")
            sys.exit(1)
        if not expect_payload(guests[0], b"FROM3") or not expect_payload(guests[2], b"FROM3"):
            print("FAIL: seat 3 did not reach the other guests")
            sys.exit(1)
        print("PASS: seat fan-out")
        extra = udp()
        socks.append(extra)
        rejected = join_session(extra, port, token)
        if rejected != bytes([TYPE_REJECT]):
            print("FAIL: fifth player was not rejected")
            sys.exit(1)
        print("PASS: fifth player rejected")
    finally:
        stop_server(proc)
        for sock in socks:
            sock.close()


def test_full():
    port = free_port()
    proc = start_server(port, 1, 900, 10800)
    first = second = None
    try:
        first = udp()
        second = udp()
        if open_session(first, port) is None:
            print("FAIL: first open on a full-cap server")
            sys.exit(1)
        second.sendto(bytes([TYPE_OPEN]), ("127.0.0.1", port))
        data, _ = recv(second, 2.0)
        if data != bytes([TYPE_REJECT]):
            print("FAIL: full server did not reject")
            sys.exit(1)
        print("PASS: full server rejected")
    finally:
        stop_server(proc)
        for sock in (first, second):
            if sock is not None:
                sock.close()


def test_expired():
    port = free_port()
    proc = start_server(port, 32, 1, 30)
    host = guest = None
    try:
        host = udp()
        guest = udp()
        token = open_session(host, port)
        if token is None:
            print("FAIL: open before expiry")
            sys.exit(1)
        time.sleep(1.5)
        data = join_session(guest, port, token)
        if data != bytes([TYPE_REJECT]):
            print("FAIL: expired session was joined")
            sys.exit(1)
        print("PASS: expired session rejected")
    finally:
        stop_server(proc)
        for sock in (host, guest):
            if sock is not None:
                sock.close()


ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
TYPE_RESOLVE = 9
TYPE_LIST = 10
TYPE_RESTART = 12


def code_of(token):
    v = (token[0] << 24) | (token[1] << 16) | (token[2] << 8) | token[3]
    v >>= 2
    chars = []
    for _ in range(6):
        chars.append(ALPHABET[v & 31])
        v >>= 5
    return "".join(chars).encode()


def start_default(port):
    proc = subprocess.Popen(
        [SERVER, str(port)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.3)
    if proc.poll() is not None:
        print("FAIL: default relay server exited at start")
        sys.exit(1)
    return proc


def test_default_cap():
    port = free_port()
    proc = start_default(port)
    first = second = None
    try:
        first = udp()
        second = udp()
        if open_session(first, port) is None:
            print("FAIL: default cap open")
            sys.exit(1)
        second.sendto(bytes([TYPE_OPEN]), ("127.0.0.1", port))
        data, _ = recv(second, 2.0)
        if data != bytes([TYPE_REJECT]):
            print("FAIL: default cap did not reject a second open")
            sys.exit(1)
        print("PASS: default cap rejected")
    finally:
        stop_server(proc)
        for sock in (first, second):
            if sock is not None:
                sock.close()


def test_addr_limit():
    port = free_port()
    proc = start_server(port, 32, 900, 10800)
    socks = []
    try:
        for i in range(4):
            sock = udp()
            socks.append(sock)
            if open_session(sock, port) is None:
                print("FAIL: address open", i)
                sys.exit(1)
        extra = udp()
        socks.append(extra)
        extra.sendto(bytes([TYPE_OPEN]), ("127.0.0.1", port))
        data, _ = recv(extra, 2.0)
        if data != bytes([TYPE_REJECT]):
            print("FAIL: fifth open from one address")
            sys.exit(1)
        print("PASS: address open limit")
    finally:
        stop_server(proc)
        for sock in socks:
            sock.close()


def listed_codes(data):
    if data is None or len(data) < 2 or data[0] != TYPE_LIST:
        return None
    count = data[1]
    off = 2
    codes = []
    for _ in range(count):
        if off + 7 > len(data):
            return None
        codes.append(data[off : off + 6])
        name_n = data[off + 6]
        off += 7 + name_n + 1
        if off > len(data):
            return None
    return codes


def test_list_and_resolve():
    port = free_port()
    proc = start_server(port, 8, 900, 10800)
    private = public = hidden = lister = joiner = None
    try:
        private = udp()
        if open_session(private, port) is None:
            print("FAIL: private open")
            sys.exit(1)
        lister = udp()
        lister.sendto(bytes([TYPE_LIST]), ("127.0.0.1", port))
        data, _ = recv(lister, 2.0)
        codes = listed_codes(data)
        if codes != []:
            print("FAIL: private game was listed")
            sys.exit(1)
        print("PASS: private game hidden")

        public = udp()
        name = b"Sean"
        public.sendto(bytes([TYPE_OPEN, 1, len(name)]) + name, ("127.0.0.1", port))
        opened, _ = recv(public, 2.0)
        if opened is None or len(opened) != 1 + TOKEN_LEN or opened[0] != TYPE_ACCEPT:
            print("FAIL: public open")
            sys.exit(1)
        token = opened[1:]
        # The open also gets a roster. Drain it before the next read on this socket.
        recv(public, 0.2)
        code = code_of(token)
        lister.sendto(bytes([TYPE_LIST]), ("127.0.0.1", port))
        data, _ = recv(lister, 2.0)
        if data is None or data[0] != TYPE_LIST or data[1] != 1 or data[2:8] != code:
            print("FAIL: public game missing from the list")
            sys.exit(1)
        if data[8] != len(name) or data[9 : 9 + len(name)] != name or data[9 + len(name)] != 1:
            print("FAIL: list row")
            sys.exit(1)
        print("PASS: public game listed")

        joiner = udp()
        joiner.sendto(bytes([TYPE_RESOLVE]) + code, ("127.0.0.1", port))
        resolved, _ = recv(joiner, 2.0)
        if resolved != bytes([TYPE_ACCEPT]) + token:
            print("FAIL: resolve")
            sys.exit(1)
        print("PASS: resolve")

        hidden = udp()
        secret = b"Secret"
        hidden.sendto(bytes([TYPE_OPEN, 0, len(secret)]) + secret, ("127.0.0.1", port))
        hid, _ = recv(hidden, 2.0)
        if hid is None or hid[0] != TYPE_ACCEPT:
            print("FAIL: link-only open")
            sys.exit(1)
        secret_code = code_of(hid[1 : 1 + TOKEN_LEN])
        lister.sendto(bytes([TYPE_LIST]), ("127.0.0.1", port))
        data, _ = recv(lister, 2.0)
        codes = listed_codes(data)
        if codes is None or secret_code in codes or code not in codes:
            print("FAIL: link-only game was listed")
            sys.exit(1)
        print("PASS: link-only game hidden")
        joiner.sendto(bytes([TYPE_RESOLVE]) + secret_code, ("127.0.0.1", port))
        resolved, _ = recv(joiner, 2.0)
        if resolved != hid[: 1 + TOKEN_LEN]:
            print("FAIL: link-only resolve")
            sys.exit(1)
        print("PASS: link-only resolve")
    finally:
        stop_server(proc)
        for sock in (private, public, hidden, lister, joiner):
            if sock is not None:
                sock.close()


def join_at(sock, port, token, seat):
    sock.sendto(bytes([TYPE_JOIN]) + token + bytes([seat]), ("127.0.0.1", port))
    return next_packet(sock, 2.0)


def test_restart():
    port = free_port()
    proc = start_server(port, 8, 900, 10800)
    host = guest = back_h = back_g = None
    try:
        host = udp()
        guest = udp()
        token = open_session(host, port)
        if token is None or join_session(guest, port, token) is None:
            print("FAIL: restart setup")
            sys.exit(1)
        recv(host, 0.2)
        host.sendto(bytes([TYPE_RESTART]) + token, ("127.0.0.1", port))
        if next_packet(host, 2.0) != bytes([TYPE_RESTART]) or next_packet(guest, 2.0) != bytes([TYPE_RESTART]):
            print("FAIL: restart notice")
            sys.exit(1)
        print("PASS: restart notice")
        back_h = udp()
        back_g = udp()
        if join_at(back_h, port, token, 1) != bytes([TYPE_JOINED, 1]):
            print("FAIL: host did not get seat 1 back")
            sys.exit(1)
        joined = join_at(back_g, port, token, 2)
        if joined != bytes([TYPE_JOINED, 2]):
            print("FAIL: guest did not get seat 2 back")
            sys.exit(1)
        notice = bytes([TYPE_START, 0x03])
        if next_packet(back_h, 2.0) != notice or next_packet(back_g, 2.0) != notice:
            print("FAIL: restart did not start the match")
            sys.exit(1)
        print("PASS: restart starts when both rejoin")
    finally:
        stop_server(proc)
        for sock in (host, guest, back_h, back_g):
            if sock is not None:
                sock.close()


def main():
    if not os.path.isfile(SERVER):
        print("FAIL: missing test_temp/relay_server")
        sys.exit(1)
    test_forward_and_reject()
    test_host_start()
    test_four_and_fifth()
    test_full()
    test_expired()
    test_default_cap()
    test_addr_limit()
    test_list_and_resolve()
    test_restart()
    print("PASS: relay regression")


if __name__ == "__main__":
    main()
