# pqc_drone_link
PQC-1: Quantum-Secure Drone Link


## Submodules

```sh
git submodule add https://github.com/itzmeanjan/ml-kem.git third_party/ml-kem
git submodule add https://github.com/ascon/ascon-c.git     third_party/ascon-c
git commit -m "Add ml-kem and ascon-c as submodules"
```

## Building

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release    # only needed after editing CMakeLists.txt
cmake --build build
cmake --build build --target docs
```