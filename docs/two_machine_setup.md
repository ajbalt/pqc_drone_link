# Running the link on two computers

How to run the Phase 1 link and the T10 handshake benchmark between **two machines on a LAN**:
one is the server (Pi B's role), one is the client (Pi A's role). It is the same procedure for
two Pis later; only the notes marked *(Pi)* change. First run and results: 2026-10-20, see
[results.md](results.md).

Every command below was run for the first measurement. Paths are the ones used then
(`~/pqc_certs`, `~/t10_results`); change them if you like, but keep them **outside the repo**.

## What you need

| Item | Server | Client |
|------|--------|--------|
| OS | Native Linux (used: Linux on an i7-6700HQ) | Linux, or WSL2 on Windows (used: Core Ultra 9 285H, WSL2) |
| Software | CMake 3.28+, C17 compiler, Python 3, OpenSSL 3.5.9 ([build_openssl.md](build_openssl.md)) | the same |
| Same commit | yes | yes (`git rev-parse --short HEAD` on both) |
| Network | a static or known LAN address, port 4433 and 4434 reachable | reaches the server's address |

Why server on native Linux and client in WSL2: WSL2 sits behind NAT, so it can *make* connections to
the LAN but other machines cannot easily reach *it*. Putting the client in WSL2 needs no inbound
setup. A WSL2 client is a virtual machine, so label its numbers as such. Native Windows is not
supported (the code uses POSIX sockets, `termios`, `fsync`, glibc `mallinfo2`). *(Pi)* both ends are
Linux; nothing in this paragraph applies.

## 1. Build and test on each machine

```sh
git clone <repo> ~/libraries/pqc_drone_link && cd ~/libraries/pqc_drone_link
cmake -B build -DCMAKE_BUILD_TYPE=Release -DOPENSSL_ROOT_DIR=$HOME/opt/openssl-3.5.9
cmake --build build
ctest --test-dir build --output-on-failure      # all tests pass; some may skip (exit 77)
```

The programs are `build/tls_server`, `build/tls_client` and `build/bench_handshake`
(not `build/pi/...`). Use a **Release** build; the benchmark scripts refuse anything else.
After `ctest`, check nothing was left running: `pgrep -a tls_server` must print nothing
(`pkill -f 'tls_server --config'` if it does).

## 2. Certificates (once, on the server)

```sh
P=$HOME/opt/openssl-3.5.9
LD_LIBRARY_PATH=$P/lib scripts/make_demo_certs.sh $P/bin/openssl ~/demo_certs 30
```

Only `ca.crt` goes to the client; the keys (`srv.key`, `ca.key`) never leave the server and never go
in git (CONTRIBUTING.md rule 17). On the client:

```sh
mkdir -p ~/pqc_certs && chmod 700 ~/pqc_certs
scp ari@192.168.1.222:demo_certs/ca.crt ~/pqc_certs/      # run on the client
```

Compare the fingerprint on both machines, they must match exactly:

```sh
openssl x509 -in ~/pqc_certs/ca.crt -noout -fingerprint -sha256
```

The client connects by **IPv4 address** (`--host` is an IPv4 literal, no name lookup) and checks the
certificate against `--hostname relay.test`. Certificate time checks stay on, so keep both clocks
right (`timedatectl`; in WSL2 correct the clock after sleep, e.g. `sudo hwclock -s`).

## 3. Network and firewall

```sh
ping -c 3 192.168.1.222              # from the client
sudo ufw allow 4433/tcp              # server, if ufw is active (config A server / smoke test)
sudo ufw allow 4434/tcp              # server, config B server of the benchmark
nc -zv -w 3 192.168.1.222 4433       # client, after starting a server
```

A client whose connect gets **no answer** reports `connect_failed rc=-71` (`PQC_ERR_TIMEOUT`).
That means the packets are dropped (firewall), not refused. Opening the port to the whole network
was accepted for this proof of concept; restrict it with
`sudo ufw allow from <client address> to any port 4433 proto tcp` when it matters. The address WSL2
presents to the LAN is the Windows host's, not WSL's `172.x` address.

## 4. Smoke test (link, both configurations)

Use a **new store and queue file for every run and config**: the server remembers every
node and sequence number it has seen.

```sh
# server
export LD_LIBRARY_PATH=$HOME/opt/openssl-3.5.9/lib
build/tls_server --config B --port 4433 --bind 192.168.1.222 \
  --cert ~/demo_certs/srv.crt --key ~/demo_certs/srv.key --store ~/pqc_smoke_store_b.dat

# client
export LD_LIBRARY_PATH=$HOME/opt/openssl-3.5.9/lib
build/tls_client --config B --host 192.168.1.222 --port 4433 \
  --ca ~/pqc_certs/ca.crt --hostname relay.test \
  --queue ~/pqc_smoke_queue_b.dat --count 20 --rate-hz 10
```

Pass: exit status 0 and `done generated=20 accepted=20 rejected=0 acked=20 pending=0`.
Repeat with `--config A`. Checks that must also hold: rerun with the **same store** and a new queue
file and every record is ACKed as a duplicate; and these fail with a nonzero exit and no
records accepted: wrong CA (a second `make_demo_certs.sh` into another directory), `--hostname other.test`,
and a client/server config mismatch. Add `--deadline-ms 15000` to the failing cases.

## 5. T10 benchmark between the machines

Config: `experiments/configs/t10_handshake_lan.conf` (A and B interleaved, 100 measured
handshakes per config after 5 warm-up). Use the **same run id on both machines**, a new one for
every session (results are never overwritten). Three sessions are needed (T10); use
`t10_lan_s1`, `t10_lan_s2`, `t10_lan_s3` and give each its own start. Keep both machines idle
during a session (no builds, no other VMs).

```sh
# server, start FIRST; wait for "servers ready"
pgrep -a tls_server                      # must print nothing
cd ~/libraries/pqc_drone_link; P=$HOME/opt/openssl-3.5.9
scripts/bench_server_remote.sh $P build experiments/configs/t10_handshake_lan.conf \
  ~/t10_results 192.168.1.222 t10_lan_s1 ~/demo_certs

# client
cd ~/libraries/pqc_drone_link; P=$HOME/opt/openssl-3.5.9
scripts/bench_client_remote.sh $P build experiments/configs/t10_handshake_lan.conf \
  ~/t10_results 192.168.1.222 ~/pqc_certs/ca.crt t10_lan_s1
```

When the client prints `client done`, press Ctrl-C on the server. Then, on the client:

```sh
scp 'ari@192.168.1.222:t10_results/t10_lan_s1.server_*' ~/t10_results/     # quote the *
python3 -I scripts/summarize_handshakes.py ~/t10_results/t10_lan_s1.ok.csv \
  ~/t10_results/t10_lan_s1.fail.csv --min 100 --csv ~/t10_results/t10_lan_s1.summary.csv \
  --server A=$HOME/t10_results/t10_lan_s1.server_A.csv \
  --server B=$HOME/t10_results/t10_lan_s1.server_B.csv --server-warmup 5 \
  | tee ~/t10_results/t10_lan_s1.summary.txt
```

A session is **valid** only if the summary shows `n=100`, `failures=0` for A and B on both sides and
**no `WARNING` line**. A warning means the server logged a different number of handshakes than the
client made, so something else connected to it. Do not edit the CSVs; move the session into
`~/t10_results/invalid/` with a note and run again under a new id (`..._s2b`).

The server writes ports **4433 (A)** and **4434 (B)**, stores in a temporary directory, and
`<id>.server_{A,B}.csv` plus `<id>.server_meta.txt` in the output directory. The client writes
`<id>.{ok,fail}.csv`, `.bench.log` and `.client_meta.txt`. Both meta files record the commit,
whether the tree was dirty, the CPU model and governor; the client also records the round-trip
time. Put those values next to the numbers in [results.md](results.md).

## Mistakes already made (so you don't)

| What happened | Cause | How to avoid |
|---------------|-------|--------------|
| `No such file: build/pi/tls_server` | Programs are in `build/` | `ls build/tls_server` |
| `usage: ...` from the server | `--store FILE` is required | copy the commands above |
| Client: `connect_failed rc=-71` for minutes | Server firewall silently dropped the port | `sudo ufw allow <port>/tcp`; `nc -zv` first |
| `zsh: no matches found: ...server_*` | zsh expanded `*` locally | quote the remote path |
| Session 2: server CSV had 44 extra rows per config; session 3: no handshakes recorded | Servers from an earlier session were still running and other client runs reached them (the files do not say which); a client started before its server found nothing to connect to | `pgrep -a tls_server` before every start; `bench_server_remote.sh` refuses to start if 4433 or 4434 is in use; the summarizer warns when the counts differ |
| Stray `tls_server --port 0` processes after a test run | An interrupted `ctest` (e2e_bench) left its servers | `pkill -f 'tls_server --config'`; check with `pgrep` |

## What changes with real equipment *(Pi)*

- Both ends are Raspberry Pis on the access point or the router; record board IDs and OS image.
- The Pis' clocks must be set (no battery clock); the demo certificate is backdated one day.
- T08 loss then needs `tc netem` on a real interface (root on the sending side, or on the access
  point); the current `run_loss_test.sh` only works in a private namespace on one machine.
- The numbers from the Pis, with board IDs and the instructor's initials, go in `evidence/`.
  Numbers from these development machines never do.
