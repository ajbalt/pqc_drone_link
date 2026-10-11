# Sources

Registry of every source the wiki cites. Pages cite a source by linking to its ID here:
`[fips-203](sources.md#fips-203)`. The PDFs live on the maintainer's OneDrive
(`PQC_SOURCES\raw\`) and are not in git; use the URL to get your own copy.

IDs are lowercase with hyphens, and they never change once used, because pages link to
them. Add new sources in alphabetical order by ID.

Entry format:

```markdown
### <id>

- **Title**: Full title
- **Authors / org**: …
- **Year**: …
- **URL / DOI**: …
- **Licence**: Public domain (US gov) | Copyrighted: paraphrase only | CC-BY … | …
- **File**: `raw/<filename>` (on the source drive)
- **Ingested**: YYYY-MM-DD, extraction backend
```

---

### arch-arm-pi4-clock-forum

- **Title**: Forum threads "system time resets to incorrect time after reboot" and "systemd-timesyncd is unreliable on Raspberry Pi 4 (aarch64)"
- **Authors / org**: Arch Linux ARM forum users. **User reports, not authoritative; low trust; Arch Linux ARM, not Raspberry Pi OS**
- **Year**: 2021 and later
- **URL / DOI**: https://archlinuxarm.org/forum/viewtopic.php?p=68356 and https://archlinuxarm.org/forum/viewtopic.php?p=67275
- **Licence**: Copyrighted: paraphrase only
- **File**: none (web)
- **Ingested**: 2026-10-11, read from search summaries only; the threads themselves were not opened

### debian-openssl-tracker

- **Title**: Debian Security Tracker, source package `openssl` (available versions per release)
- **Authors / org**: Debian Security Team
- **Year**: 2026 (read 2026-10-11; changes with every update)
- **URL / DOI**: https://security-tracker.debian.org/tracker/source-package/openssl
- **Licence**: Public web page; cite and paraphrase
- **File**: none (web)
- **Ingested**: 2026-10-11, fetched

### expressvpn-pq-wireguard-guide

- **Title**: Post-Quantum WireGuard: A Practical Implementation Guide
- **Authors / org**: Membrey P, Beyel T (writing as ExpressVPN). **Vendor white paper: not peer-reviewed, no citations; low trust**, see [summary](src-expressvpn-pq-wireguard-guide.md)
- **Year**: 2025 (August 5)
- **URL / DOI**: not printed — needs URL
- **Licence**: Copyrighted: paraphrase only
- **File**: `raw/Post-Quantum+WireGuard_+A+Practical+Implementation+Guide.pdf` (on the source drive)
- **Ingested**: 2026-10-08, docling

### fips-203

- **Title**: Module-Lattice-Based Key-Encapsulation Mechanism Standard (FIPS 203)
- **Authors / org**: National Institute of Standards and Technology (NIST)
- **Year**: 2024 (published and effective August 13, 2024)
- **URL / DOI**: https://doi.org/10.6028/NIST.FIPS.203
- **Licence**: Public domain (US gov)
- **File**: `raw/NIST.FIPS.203.pdf` (on the source drive)
- **Ingested**: 2026-10-08, docling

### fips-204

- **Title**: Module-Lattice-Based Digital Signature Standard (FIPS 204)
- **Authors / org**: National Institute of Standards and Technology (NIST)
- **Year**: 2024 (published and effective August 13, 2024; final)
- **URL / DOI**: https://doi.org/10.6028/NIST.FIPS.204
- **Licence**: Public domain (US gov)
- **File**: `raw/NIST.FIPS.204.pdf` (on the source drive)
- **Ingested**: 2026-10-08, docling

### linux-ip-sysctl

- **Title**: IP Sysctl (Linux kernel documentation, `networking/ip-sysctl`)
- **Authors / org**: The Linux kernel community
- **Year**: 2026 (latest kernel docs)
- **URL / DOI**: https://www.kernel.org/doc/html/latest/networking/ip-sysctl.html
- **Licence**: GPL-2.0 documentation; paraphrase and short quotes
- **File**: none (web)
- **Ingested**: 2026-10-11, fetched; only the TCP timer entries were read (first 100,000 of about 130,000 characters)

### lorawan-rp-1-0-3

- **Title**: LoRaWAN 1.0.3 Regional Parameters
- **Authors / org**: LoRa Alliance Technical Committee, Regional Parameters Workgroup
- **Year**: 2018 (Revision A, July 2018, released)
- **URL / DOI**: not printed — needs URL
- **Licence**: Copyrighted: paraphrase only
- **File**: `raw/lorawan_regional_parameters_v1.0.3reva_0.pdf` (on the source drive)
- **Ingested**: 2026-10-08, docling

### mavlink2-guide

- **Title**: Guide to MAVLink2
- **Authors / org**: MAVLink project (author not printed)
- **Year**: Not printed
- **URL / DOI**: not printed — needs URL (believed to be the `MAVLink2.md` guide from the MAVLink/pymavlink repositories; confirm)
- **Licence**: Copyrighted: paraphrase only
- **File**: `raw/MAVLink2.md` (on the source drive)
- **Ingested**: 2026-10-08, none (markdown, read directly)

### openssl-3-5-9

- **Title**: OpenSSL 3.5.9 source tree: manual pages (`doc/`), `CHANGES.md`, `NEWS.md`, `INSTALL.md`, demos and code, read at tag `openssl-3.5.9` (commit `45e844fa2a14ec92d146bd8f5778ac130b6625fb`, 29 Sep 2026)
- **Authors / org**: The OpenSSL Project
- **Year**: 2026
- **URL / DOI**: https://github.com/openssl/openssl/tree/openssl-3.5.9 (rendered manual pages: https://docs.openssl.org/3.5/)
- **Licence**: Apache-2.0 (`LICENSE.txt`); paraphrase and cite file paths
- **File**: none; shallow clone of the tag, read in a scratch folder. The tag signature was **not** verified
- **Ingested**: 2026-10-10, read directly (no extraction needed)

### rfc-6298

- **Title**: RFC 6298, Computing TCP's Retransmission Timer
- **Authors / org**: Paxson V, Allman M, Chu J, Sargent M (IETF)
- **Year**: 2011
- **URL / DOI**: https://www.rfc-editor.org/rfc/rfc6298
- **Licence**: IETF Trust; short quotes
- **File**: none (web)
- **Ingested**: 2026-10-11, fetched; sections 2.1, 2.5, 5.5 and 5.7 read

### rfd900-datasheet

- **Title**: RFD900 Radio Modem Data Sheet
- **Authors / org**: RFDesign Pty Ltd
- **Year**: 2013 (dated 31/05/2013 in page footers; no revision printed)
- **URL / DOI**: not printed — needs URL (firmware: github.com/RFDesign/SiK)
- **Licence**: Copyrighted: paraphrase only
- **File**: `raw/RFD900 DataSheet.pdf` (on the source drive)
- **Ingested**: 2026-10-08, docling

### rpi-docs-rtc

- **Title**: Raspberry Pi documentation, "Raspberry Pi hardware", Real Time Clock (RTC) section
- **Authors / org**: Raspberry Pi Ltd
- **Year**: 2026 (read 2026-10-11)
- **URL / DOI**: https://www.raspberrypi.com/documentation/computers/raspberry-pi.html
- **Licence**: Public web page; paraphrase
- **File**: none (web)
- **Ingested**: 2026-10-11, fetched; the section names only the Raspberry Pi 5 as having an RTC

### rpi-os-trixie-news

- **Title**: Raspberry Pi OS based on Debian 13 "Trixie" (news article)
- **Authors / org**: CNX Software (secondary report)
- **Year**: 2025 (6 Oct)
- **URL / DOI**: https://www.cnx-software.com/2025/10/06/raspberry-pi-os-debian-13-trixie/
- **Licence**: Copyrighted: paraphrase only
- **File**: none (web)
- **Ingested**: 2026-10-11, read from search summaries; the Raspberry Pi downloads page lists current trixie images

### sp-800-133r3-ipd

- **Title**: Recommendation for Cryptographic Key Generation (NIST SP 800-133 Rev. 3, initial public draft)
- **Authors / org**: Dang Q, Moody D, Regenscheid A, Silberg H (NIST)
- **Year**: 2026 (April; **initial public DRAFT**, comments April 17 – June 16, 2026)
- **URL / DOI**: https://doi.org/10.6028/NIST.SP.800-133r3.ipd
- **Licence**: Public domain (US gov)
- **File**: `raw/NIST.SP.800-133r3.ipd.pdf` (on the source drive)
- **Ingested**: 2026-10-08, pdfplumber (docling failed)

### sp-800-227

- **Title**: Recommendations for Key-Encapsulation Mechanisms (NIST SP 800-227)
- **Authors / org**: Alagic G, Barker E, Chen L, Moody D, Robinson A, Silberg H, Waller N (NIST)
- **Year**: 2025 (September, final)
- **URL / DOI**: https://doi.org/10.6028/NIST.SP.800-227
- **Licence**: Public domain (US gov)
- **File**: `raw/NIST.SP.800-227.pdf` (on the source drive)
- **Ingested**: 2026-10-08, docling

### sp-800-232

- **Title**: Ascon-Based Lightweight Cryptography Standards for Constrained Devices: Authenticated Encryption, Hash, and Extendable Output Functions (NIST SP 800-232)
- **Authors / org**: Sönmez Turan M, McKay KA, Chang D, Kang J, Kelsey J (NIST)
- **Year**: 2025 (August, final)
- **URL / DOI**: https://doi.org/10.6028/NIST.SP.800-232
- **Licence**: Public domain (US gov)
- **File**: `raw/NIST.SP.800-232.pdf` (on the source drive)
- **Ingested**: 2026-10-08, docling

### sp-800-56c

- **Title**: Recommendation for Key-Derivation Methods in Key-Establishment Schemes (NIST SP 800-56C Rev. 2)
- **Authors / org**: Barker E, Chen L (NIST); Davis R (NSA)
- **Year**: 2020 (August, final, Revision 2)
- **URL / DOI**: https://doi.org/10.6028/NIST.SP.800-56Cr2
- **Licence**: Public domain (US gov)
- **File**: `raw/NIST.SP.800-56Cr2.pdf` (on the source drive)
- **Ingested**: 2026-10-08, docling

### sp-800-57pt1

- **Title**: Recommendation for Key Management: Part 1 – General (NIST SP 800-57 Part 1 Rev. 5)
- **Authors / org**: Barker E (NIST)
- **Year**: 2020 (May, final, Revision 5)
- **URL / DOI**: https://doi.org/10.6028/NIST.SP.800-57pt1r5
- **Licence**: Public domain (US gov)
- **File**: `raw/NIST.SP.800-57pt1r5.pdf` (on the source drive)
- **Ingested**: 2026-10-08, docling

### sp-800-90a

- **Title**: Recommendation for Random Number Generation Using Deterministic Random Bit Generators (NIST SP 800-90A Rev. 1)
- **Authors / org**: Barker E, Kelsey J (NIST)
- **Year**: 2015 (June, final, Revision 1)
- **URL / DOI**: https://doi.org/10.6028/NIST.SP.800-90Ar1
- **Licence**: Public domain (US gov)
- **File**: `raw/NIST.SP.800-90Ar1.pdf` (on the source drive)
- **Ingested**: 2026-10-08, docling

### sp-800-90b

- **Title**: Recommendation for the Entropy Sources Used for Random Bit Generation (NIST SP 800-90B)
- **Authors / org**: Sönmez Turan M, Barker E, Kelsey J, McKay KA (NIST); Baish ML, Boyle M (NSA)
- **Year**: 2018 (January, final)
- **URL / DOI**: https://doi.org/10.6028/NIST.SP.800-90B
- **Licence**: Public domain (US gov)
- **File**: `raw/NIST.SP.800-90B.pdf` (on the source drive)
- **Ingested**: 2026-10-08, docling

### sp-800-90c

- **Title**: Recommendation for Random Bit Generator (RBG) Constructions (NIST SP 800-90C)
- **Authors / org**: Barker E, Kelsey J, McKay K, Roginsky A, Sönmez Turan M (NIST)
- **Year**: 2025 (September, final)
- **URL / DOI**: https://doi.org/10.6028/NIST.SP.800-90C
- **Licence**: Public domain (US gov)
- **File**: `raw/NIST.SP.800-90C.pdf` (on the source drive)
- **Ingested**: 2026-10-08, docling

### sx1276-datasheet

- **Title**: SX1276/77/78/79 – 137 MHz to 1020 MHz Low Power Long Range Transceiver (datasheet)
- **Authors / org**: Semtech Corporation
- **Year**: 2020 (Rev. 7, May 2020)
- **URL / DOI**: not printed — needs URL
- **Licence**: Copyrighted: paraphrase only
- **File**: `raw/DS_SX1276-7-8-9_W_APP_V7.pdf` (on the source drive)
- **Ingested**: 2026-10-08, docling (time-on-air formulas recovered from the PDF text layer)

### tc-netem-man

- **Title**: tc-netem(8), Network Emulator manual page
- **Authors / org**: Linux iproute2 project
- **Year**: n/a
- **URL / DOI**: https://www.mankier.com/8/tc-netem (also https://man7.org/linux/man-pages/man8/tc-netem.8.html)
- **Licence**: Free documentation; paraphrase
- **File**: none (web)
- **Ingested**: 2026-10-11, fetched from the mankier mirror
