# Internet relay

A local UDP relay lets four games meet when plain UDP cannot. There are no accounts. This is not for public deployment.

## Packets

Each datagram starts with one type byte. The token is 16 raw bytes. The client does not choose a session id. Multi-byte integers are not in the header. The token bytes are in the order the server minted them.

| Byte | Name | Body |
| --- | --- | --- |
| 1 | Open | empty |
| 2 | Join | 16 byte token |
| 3 | Data | 16 byte token, then the lockstep payload |
| 4 | Accept | 16 byte token |
| 5 | Reject | empty |
| 6 | Joined | one seat byte, 2, 3, or 4 |
| 7 | Roster | count, then that many seat bytes |
| 8 | Start | host sends the 16 byte token. The notice is one mask byte |

Open is the host, seat 1. The server binds that UDP endpoint. Under the cap it replies Accept and 16 random bytes, then a roster. Otherwise it replies Reject.

Join binds the next free seat when that token exists, is not expired, has not started, and has room. It replies Joined and the seat, or Reject. A fifth player in the same session is refused. The server then sends a roster to every bound seat.

Data is forwarded only when the source endpoint is a bound seat. Every other bound seat receives the lockstep payload alone.

The host sends Start. The server rejects Start from any other endpoint, and a second Start. It remembers the seats bound at that moment and sends a start notice to each of them, including the host. The mask byte uses bit 0 for seat 1 through bit 3 for seat 4. After that, Join is Reject. Data still goes only to seats in that mask. A second endpoint for a seat is still dropped.

## Limits

One session, four seats. The idle timeout is 15 minutes from the last accepted packet. The absolute lifetime is 3 hours from Open. Each packet sweeps expired sessions.

```
relay_server <port> [max_sessions] [idle_sec] [abs_sec]
```

The extra arguments override the cap, the idle seconds, and the lifetime seconds. Tests use them so a session can expire without waiting 15 minutes.

## Link

`ff1:<relay-host>:<port>:<32 hex chars>`

The hex is the 16 byte token (128 bits). The F1 settings panel can Host or Join and copy the link. The host gets Start after Open. The server assigns seats 2, 3, and 4. Launch the games with `--lockstep` and without `--lockstep-peer`. The game keeps running until the host presses Start. Whoever has joined plays, and empty seats stay disconnected so the title fills them with AI. A guest who has not joined does not enter this match. A direct `--lockstep-peer` still uses two-seat UDP.

Traffic is plaintext UDP. The relay does not cross a firewall that blocks outbound UDP.
