# Building OpenSSL 3.5.9 (all team machines)

Hybrid `X25519MLKEM768` needs OpenSSL 3.5 or newer. Ubuntu 24.04 ships 3.0.13, which cannot
negotiate it (CONTRIBUTING: Dependencies). We build a pinned copy into a private prefix and
leave the system OpenSSL alone.

Tested on: Ubuntu under WSL2, x86_64. On other systems, record what you used.

## 1. Prerequisites

```sh
sudo apt update
sudo apt install -y build-essential perl git
```

## 2. Fetch the pinned tag and verify it

```sh
mkdir -p ~/src ~/opt && cd ~/src
git clone --branch openssl-3.5.9 --depth 1 https://github.com/openssl/openssl.git openssl-3.5.9
cd openssl-3.5.9
git rev-parse HEAD
```

The commit must be `45e844fa2a14ec92d146bd8f5778ac130b6625fb` (the one in
[results.md](results.md)). If it differs, stop and tell the team.

## 3. Configure, build, install

```sh
./Configure --prefix=$HOME/opt/openssl-3.5.9 --libdir=lib no-docs no-tests
make -j"$(nproc)"
make install_sw install_ssldirs
```

`install_sw` installs binaries, libraries and headers only. `install_ssldirs` adds
`ssl/openssl.cnf`; without it, `openssl req` (used by `scripts/make_demo_certs.sh`) fails with
`Can't open ".../ssl/openssl.cnf"`. `--libdir=lib` avoids a `lib64` directory, so every
machine has the same layout.

## 4. Verify

```sh
P=$HOME/opt/openssl-3.5.9
LD_LIBRARY_PATH=$P/lib $P/bin/openssl version -a
LD_LIBRARY_PATH=$P/lib $P/bin/openssl list -tls1_3 -tls-groups | grep -i X25519MLKEM768
```

Expected: version `3.5.9` and a line containing `X25519MLKEM768`. If either is missing,
you are running the system OpenSSL, not this one.

## 5. Use it

- Always call it by full path (`$P/bin/openssl`), or set `LD_LIBRARY_PATH=$P/lib` first.
  Do **not** put it first on `PATH` system-wide or replace `/usr/bin/openssl`.
- The scripts take the prefix as their first argument, for example
  `scripts/tls_group_matrix.sh $P <cert-dir>` (see `scripts/make_demo_certs.sh`).
- When we build C code against it, use `-I$P/include -L$P/lib` and an rpath to `$P/lib`.
- Paste the output of `openssl version -a` into every test log (CONTRIBUTING).

## Troubleshooting

- `Can't locate ... in @INC` during Configure: install `perl` (full, not `perl-base`).
- `Can't open ".../ssl/openssl.cnf"`: run `make install_ssldirs` in the source directory.
- `error while loading shared libraries: libssl.so.3`: set `LD_LIBRARY_PATH=$P/lib`.
- Build is slow: use `make -j"$(nproc)"`; the first build takes a few minutes.
