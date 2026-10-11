#!/usr/bin/env bash
# Create a demo CA and a server certificate for hostname relay.test (L5/L6 bring-up).
# Usage: make_demo_certs.sh <openssl-binary> <output-dir> [valid-days]
# Keys are written to <output-dir> and must never be committed (CONTRIBUTING.md rule 17).
# The certificates are backdated by one day so a Pi whose clock starts slightly behind
# still accepts them; time checks stay ON (never disable them to hide a clock problem).
set -eu
O=${1:?openssl binary}; D=${2:?output dir}; DAYS=${3:-30}
mkdir -p "$D"; cd "$D"
nb=$(date -u -d '1 day ago' +%Y%m%d%H%M%SZ)
na=$(date -u -d "+$DAYS days" +%Y%m%d%H%M%SZ)
$O req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -noenc -keyout ca.key \
  -out ca.crt -subj '/CN=demo-ca' -addext basicConstraints=critical,CA:TRUE \
  -days "$((DAYS + 1))" 2>/dev/null
$O req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -noenc -keyout srv.key \
  -out srv.csr -subj '/CN=relay.test' 2>/dev/null
printf 'subjectAltName=DNS:relay.test\nbasicConstraints=CA:FALSE\n' > ext.cnf
$O x509 -req -in srv.csr -CA ca.crt -CAkey ca.key -CAcreateserial -not_before "$nb" \
  -not_after "$na" -out srv.crt -extfile ext.cnf 2>/dev/null
chmod 600 ca.key srv.key
$O verify -CAfile ca.crt -verify_hostname relay.test srv.crt
$O x509 -in srv.crt -noout -dates
