# Internet relay

A UDP relay lets four games meet when plain UDP cannot. Both games send outbound to the relay, so neither player forwards a port. The relay machine is the one that must accept the UDP port. There are no accounts. Traffic is plaintext.

## Packets

Each datagram starts with one type byte. The token is 16 raw bytes. The client does not choose a session id. Multi-byte integers are not in the header. The token bytes are in the order the server minted them.

| Byte | Name | Body |
| --- | --- | --- |
| 1 | Open | empty, or a flags byte, a name length, and a name of at most 16 bytes |
| 2 | Join | 16 byte token |
| 3 | Data | 16 byte token, then the lockstep payload |
| 4 | Accept | 16 byte token |
| 5 | Reject | empty |
| 6 | Joined | one seat byte, 2, 3, or 4 |
| 7 | Roster | count, then that many seat bytes |
| 8 | Start | host sends the 16 byte token. The notice is one mask byte |
| 9 | Resolve | 6 character code. The reply is Accept and the token, or Reject |
| 10 | List | empty. The reply is List, a count, then one row per game: 6 character code, name length, name, seat count |
| 11 | Heartbeat | 16 byte token, host only |
| 12 | Restart | host sends the 16 byte token. The notice is empty. Seats rejoin, and a Join may add one preferred seat byte |

Open is the host, seat 1. A one-byte Open is a private game with no name. A longer Open uses flag bit 0 to put the game on the list. The server binds that UDP endpoint. Under the cap, and under four live games from the same address, it replies Accept and 16 random bytes, then a roster. Otherwise it replies Reject. The 6 character code is derived from the token. The alphabet is `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`.

List returns public games that have not started and were heard from in the last 20 seconds. The host sends Heartbeat about every 5 seconds while it waits, which refreshes that time. The idle timeout below is unchanged.

Join binds the next free seat when that token exists, is not expired, has not started, and has room. It replies Joined and the seat, or Reject. A fifth player in the same session is refused. The server then sends a roster to every bound seat.

Data is forwarded only when the source endpoint is a bound seat. Every other bound seat receives the lockstep payload alone.

The host sends Start. The server rejects Start from any other endpoint, and a second Start. It remembers the seats bound at that moment and sends a start notice to each of them, including the host. The mask byte uses bit 0 for seat 1 through bit 3 for seat 4. After that, Join is Reject. Data still goes only to seats in that mask. A second endpoint for a seat is still dropped.

## Limits

One session, four seats. The idle timeout is 15 minutes from the last accepted packet. The absolute lifetime is 3 hours from Open. Each packet sweeps expired sessions.

```
relay_server <port> [max_sessions] [idle_sec] [abs_sec]
```

The extra arguments override the cap, the idle seconds, and the lifetime seconds. Tests use them so a session can expire without waiting 15 minutes.

Port 0 binds a free port. The listen line prints the port the socket got. Any other port argument is unchanged.

## Link

`ff1:<6 characters>`

That code is for the relay in `FUZION_RELAY=host:port`, or the built-in host when that constant is set. The F1 panel hides the address box in that case. Host shows the code large and copies `ff1:` plus the code. A private game stays off the list and still joins by the code. Refresh lists public games that have not started. Join takes the code, or the row.

The older link still joins:

`ff1:<relay-host>:<port>:<32 hex chars>`

The hex is the 16 byte token (128 bits). The host gets Start after Open. The server assigns seats 2, 3, and 4. With `--lockstep` and no `--lockstep-peer`, the game keeps running until the host presses Start. Whoever has joined plays, and empty seats stay disconnected so the title fills them with AI. A guest who has not joined does not enter this match. Play online restarts the same binary with `--lockstep`, and with `FUZION_RELAY_CODE` when a code was pasted. A direct `--lockstep-peer` still uses two-seat UDP.

A firewall that blocks outbound UDP still stops the match. Both players still need the same width, height, and ultrawide. When those differ, the panel says the video settings do not match.

Start does not begin the match in the games that have been sitting in the lobby. The host's Start sends Restart. Every seat relaunches with `--lockstep` and joins the same code again, asking for the seat it had. When that many seats are back, the relay sends Start. Each relaunched game holds its first present until that notice, so the wait itself does not move the simulation. The two save folders still have to match. `--instance 2` uses `hdd2`.

## Host online

On macOS and Linux, Host online sits beside Host. It starts `relay_server` from the same directory as the game, on port 0, with a cap of one session. It then asks the router to forward that UDP port. UPnP IGD is first: SSDP, then `GetExternalIPAddress` and `AddPortMapping`. NAT-PMP to the default gateway is second (version 0 on UDP port 5351). PCP (RFC 6887) is third, and only when that NAT-PMP request gets no reply. PCP is version 2 on the same port. There is no extra library.

The address the router reports has to be a public IPv4 address. These are refused: `10/8`, `172.16/12`, `192.168/16`, `100.64/10`, loopback, `0/8`, link-local `169.254/16`, and multicast. The panel says internet play is not available, and the relay process is stopped. If the router does not open the port, the panel says the router did not open the port, and no relay is left running.

The host opens the match on `127.0.0.1`. That is the address saved for the Start relaunch, so the host does not depend on hairpin NAT. The copied link is `ff1:<public-ip>:<mapped-port>:<32 hex>`. There is no shared list for this path. The other player pastes that whole link. A short code still only works for someone already pointed at the same relay.

Host does not start a relay. Same-machine play still uses a relay you started yourself, usually `127.0.0.1:9999`.

The relay process and the mapping outlive Start. `execv` keeps the child and the environment, and the game exits with `_Exit`, which does not run `atexit`. A small `relay_guard` beside the game watches the host pid and removes the mapping when that pid is gone. A PCP mapping is removed with a MAP request whose lifetime is 0. The relaunched game refreshes the lease (one hour, renewed while that process is up). That lease is also the fallback if the delete packet is lost. A hard crash can leave the mapping until the lease ends. Windows has no Host online button. The relay client there is still a stub.
