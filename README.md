# MAGI System: OSI Layer Network Simulator

<p align="center">
  <img src="https://raw.githubusercontent.com/TomaszRewak/MAGI/master/examples/example_1.gif" width=600/>
</p>

<div align="center">

[![Release v0.3.1](https://img.shields.io/badge/release-v0.3.1-blue)](https://github.com/labsister23/tugas-besar-jarkom-abg_hunter/releases/tag/v0.3.1)
[![Tests](https://img.shields.io/badge/tests-300%2F300-green)]()
[![Language](https://img.shields.io/badge/language-C-orange)]()

</div>

---

**STATUS: DARURAT LEVEL 1 (SITUASI MERAH)**

**LOKASI: Markas Besar NERV, Tokyo-3 (Geofront)**

Malaikat ke-11, Ireul, telah menginfeksi dan menghancurkan seluruh protokol komunikasi standar NERV. Komandan Ikari telah memberikan mandat mutlak: kru teknis elit Divisi Jaringan harus membangun kembali seluruh sistem komunikasi MAGI dari titik nol. Kehancuran Tokyo-3 menanti jika ada satu *bit* saja yang salah dalam implementasi ini.

## Prerequisites & Setup

* **Bahasa Pemrograman:** C (Language Multiplier: +25)
* **Compiler:** GCC (dengan flags `-Wall -Wextra -std=c11`)
* **Dependencies:**
  * **Wajib:** Tidak ada untuk fitur inti. Semua protokol jaringan diimplementasikan dari nol di *user-space*.
  * **Opsional:** SDL2 — diperlukan untuk GUI dan perintah `visualize` (ekspor topologi ke PNG).
* **Platform:** Linux, macOS, WSL
* **DILARANG** menggunakan API jaringan bawaan sistem operasi atau *library* simulasi jaringan pihak ketiga.

### Instalasi SDL2 (Opsional — untuk GUI & `visualize`)

**macOS (Homebrew):**
```bash
brew install sdl2
```

**Ubuntu / Debian / WSL:**
```bash
sudo apt-get update && sudo apt-get install -y libsdl2-dev
```

**Windows (MSYS2/MinGW):**
```bash
pacman -S mingw-w64-x86_64-SDL2
```

> Jika SDL2 tidak terinstal, program tetap bisa di-build dan digunakan dalam mode CLI. Fitur GUI (`--gui`) dan perintah `visualize` tidak akan tersedia.

## Cara Menjalankan Program

```bash
# Build simulator
make build

# Atau build + run langsung
make run

# Atau gunakan run.sh (auto-build + run)
./run.sh

# Jalankan dengan GUI (SDL2)
./run.sh --gui

# Atau manual
./magi_system.out
./magi_system.out --gui
```

Setelah dijalankan, simulator akan masuk ke mode *interactive prompt*:

```
Magi> _
```

Perintah yang tersedia:
- `load <file>` — Muat topologi dari file JSON
- `save <file>` — Simpan topologi ke file JSON
- `topology` — Tampilkan peta topologi
- `create <type> <name>` — Buat node baru (host/switch/router)
- `link <node1> <port1> <node2> <port2> [delay]` — Hubungkan dua node
- `unlink <node1> <node2>` — Putuskan hubungan
- `ping <target_ip>` — Tes koneksi ICMP Echo
- `traceroute <target_ip>` — Lacak rute ke tujuan
- `route` — Lihat tabel routing router
- `tcp_connect <ip> <port>` — Inisiasi koneksi TCP
- `tcp_listen <port>` — Mulai mendengarkan port
- `tcp_send <data>` — Kirim data melalui koneksi TCP
- `tcp_recv` — Terima data dari koneksi TCP
- `tcp_close` — Tutup koneksi TCP
- `udp_send <ip> <port> <data>` — Kirim datagram UDP
- `arp` — Lihat tabel ARP
- `mac` — Lihat tabel MAC switch
- `debug <milestone/all>` — Jalankan test suite
- `visualize` — Ekspor topologi ke file PNG di folder `TopologyOutput/`
- `quit` — Keluar

## Daftar Periksa Pencapaian (Milestones)

* [x] **Milestone 0: Fondasi Simulasi** `[v0.0.1]` — Pembuatan kelas fisik (*Interface*, *Link*), struktur dasar *Packet* yang mendukung konversi ke *byte* mentah, dan memuat topologi JSON. *(Tests: 50/50)*
* [x] **Milestone 1: Data Link Layer (L2)** `[v0.1.1]` — Implementasi *Ethernet Frame*, logika *Switching* (*VLAN-aware*), dan antrean IP Packet menggunakan *ARP Cache*. *(Tests: 18/18)*
* [x] **Milestone 2: Network Layer (L3)** `[v0.2.1]` — Implementasi resolusi *Longest Prefix Match Routing*, modifikasi parameter TTL, kalkulasi *Checksum* IPv4, dan pengiriman *ICMP Error Messages*. *(Tests: 172/172)*
* [x] **Milestone 3: Transport Layer (L4)** `[v0.3.1]` — Penyusunan *State Machine* TCP (*3-Way Handshake*, *Receive Buffers*, *4-Way Teardown*), protokol UDP, dan kalkulasi *Pseudo-Header*. *(Tests: 60/60)*
* [ ] **Milestone 4: Application Layer (L7)** — Pembuatan *Wrapper API* `MagiSocket` untuk mengabstraksi komunikasi OS, serta perakitan layanan mandiri DHCP, DNS, dan server HTTP.
* [ ] **Milestone 5: Fitur Bonus** — *Topology Visualizer*, *ACL Firewall*, *NAT/PAT*, *RIPv2*, *GUI Dashboard*.

## Pembagian Tugas

> Berdasarkan analisis riwayat kontribusi kode (`git log`).

| No | NPM | Nama | Kontribusi |
|----|-----|------|------------|
| 1 | 13524115 | **Ega Luthfi Rais** | M3 Transport Layer (UDP, TCP, TCP Socket State Machine), M2 Integration (Host, Router, IPv4, ICMP), Debugger Suite, CLI Commands, Middleboxes (NAT, ACL), GUI, Topology Visualizer, Layer 7 stubs (DHCP, DNS, HTTP, MagiSocket, RIP), Build System, Utils (JSON, Loader, Visualizer) |
| 2 | 13524141 | Ahmad Fauzan Putra | bug fixes M1, M2, M4. GUI, Topology visualizer |
| 3 | 13524146 | **Leonardus Brain Fatolosja** | M0 Fondasi Simulasi (Interface, Link, MAC, Packet, Data Structures), M1 Data Link Layer (Ethernet, ARP, Switch, Host), M2 Network Layer (IPv4, ICMP, Router), CLI & Visualizer awal, Makefile & Project Structure |
| 4 | 13524134 | Salman Faiz Assidqi | — |
| 5 | 13524124 | Zahran Alvan Putra Winarko | — |

> **Catatan:** Anggota dengan kontribusi "—" belum terdeteksi aktivitas di git history. Silakan diperbarui sesuai peran masing-masing.

## Struktur Proyek

```
magi_system/
├── core/              # Interface, Link, MAC, Packet
├── dataStructure/     # Map (hash table)
├── layer2/            # Ethernet, ARP, Host, Switch
├── layer3/            # IPv4, ICMP, Router
├── layer4/            # UDP, TCP, TCPSocket
├── layer7/            # HTTP, DNS, DHCP, MagiSocket, RIP
├── middleboxes/       # NAT, ACL
├── utils/             # CLI, Debugger, Loader, Visualizer, JSON
├── gui/               # GUI app (SDL)
├── simulator.c/h      # Main simulator
├── main.c             # Entry point
└── topology.json      # Default topology
```

## Status Test Suite

```
╔══════════════════════════════════════════════╗
║           TEST SUMMARY                       ║
╠══════════════════════════════════════════════╣
║  Total tests:                          300  ║
║  Passed:                               300  ║
║  Failed:                                 0  ║
╚══════════════════════════════════════════════╝

M0 (Fondasi Simulasi):     50/50  ✅
M1 (Data Link Layer):      18/18  ✅
M2 (Network Layer):       172/172 ✅
M3 (Transport Layer):      60/60  ✅
M4 (Application Layer):     0/0   ⏳
```

## GitHub Releases

| Milestone | Tag | Rilis |
|-----------|-----|-------|
| M0: Fondasi Simulasi | `v0.0.1` | [Lihat Release](https://github.com/labsister23/tugas-besar-jarkom-abg_hunter/releases/tag/v0.0.1) |
| M1: Data Link Layer | `v0.1.1` | [Lihat Release](https://github.com/labsister23/tugas-besar-jarkom-abg_hunter/releases/tag/v0.1.1) |
| M2: Network Layer | `v0.2.1` | [Lihat Release](https://github.com/labsister23/tugas-besar-jarkom-abg_hunter/releases/tag/v0.2.1) |
| M3: Transport Layer | `v0.3.1` | [Lihat Release](https://github.com/labsister23/tugas-besar-jarkom-abg_hunter/releases/tag/v0.3.1) |
