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

### rfd900-datasheet

- **Title**: RFD900 Radio Modem Data Sheet
- **Authors / org**: RFDesign Pty Ltd
- **Year**: 2013 (dated 31/05/2013 in page footers; no revision printed)
- **URL / DOI**: not printed — needs URL (firmware: github.com/RFDesign/SiK)
- **Licence**: Copyrighted: paraphrase only
- **File**: `raw/RFD900 DataSheet.pdf` (on the source drive)
- **Ingested**: 2026-10-08, docling

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
