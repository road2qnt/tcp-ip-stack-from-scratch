# MAGI System: OSI Layer Network Simulator

***(Silahkan merombak file README.md ini sepuasnya kalian)***

<p align="center">
  <img src="https://raw.githubusercontent.com/TomaszRewak/MAGI/master/examples/example_1.gif" width=800/>
</p>

**STATUS: DARURAT LEVEL 1 (SITUASI MERAH)**

**LOKASI: Markas Besar NERV, Tokyo-3 (Geofront)**

Malaikat ke-11, Ireul, telah menginfeksi dan menghancurkan seluruh protokol komunikasi standar NERV. Komandan Ikari telah memberikan mandat mutlak: kru teknis elit Divisi Jaringan harus membangun kembali seluruh sistem komunikasi MAGI dari titik nol. Kehancuran Tokyo-3 menanti jika ada satu *bit* saja yang salah dalam implementasi ini.

## Prerequisites & Setup

* **Bahasa Pemrograman:** C
* Pilihan bahasa pemrograman memengaruhi *Language Multiplier* pada penilaian akhir.
* Penggunaan *Large Language Models* (LLM) diizinkan sebagai asisten belajar, namun setiap baris kode wajib dipahami seutuhnya.
* Gagal menjelaskan alur eksekusi saat demonstrasi akan dianggap sebagai plagiarisme.
* **DILARANG** menggunakan API jaringan bawaan sistem operasi atau *library* simulasi jaringan pihak ketiga.

## Cara Menjalankan Program

* [Jelaskan langkah-langkah instalasi *dependencies* jika ada kelompok Anda menggunakan *library* eksternal khusus untuk antarmuka/visualisasi]
* Simulator diwajibkan menyediakan sebuah `Makefile` atau skrip `run.sh` di *root repository*.
* Asisten hanya akan mengeksekusi perintah `make run` atau `./run.sh` untuk memulai CLI Simulator.
* Program tidak boleh berhenti secara otomatis, melainkan harus masuk ke mode *interactive prompt* (contoh: `Magi>`) untuk memuat konfigurasi JSON dan menerima instruksi jaringan.

## Daftar Periksa Pencapaian (Milestones)

Centang (*checklist*) kotak di bawah ini sesuai dengan *layer* yang telah kelompok kalian selesaikan:

* [ ] **Milestone 0: Fondasi Simulasi** - Pembuatan kelas fisik (*Interface*, *Link*), struktur dasar *Packet* yang mendukung konversi ke *byte* mentah, dan memuat topologi JSON.
* [ ] **Milestone 1: Data Link Layer (L2)** - Implementasi *Ethernet Frame*, logika *Switching* (*VLAN-aware*), dan antrean IP Packet menggunakan *ARP Cache*.
* [ ] **Milestone 2: Network Layer (L3)** - Implementasi resolusi *Longest Prefix Match Routing*, *Inter-VLAN Routing*, modifikasi parameter TTL, kalkulasi *Checksum* IPv4, dan pengiriman *ICMP Error Messages*.
* [ ] **Milestone 3: Transport Layer (L4)** - Penyusunan *State Machine* TCP (*3-Way Handshake*, *Receive Buffers*, *4-Way Teardown*), protokol UDP, dan kalkulasi *Pseudo-Header*.
* [ ] **Milestone 4: Application Layer (L7)** - Pembuatan *Wrapper API* `MagiSocket` untuk mengabstraksi komunikasi OS, serta perakitan layanan mandiri DHCP, DNS, dan server HTTP.
* [ ] **Milestone 5: Fitur Bonus** - [Sebutkan fitur lanjutan yang kelompok Anda targetkan, misal: *Topology Visualizer*, *IP Fragmentation*, *ACL*, *NAT/PAT*, *RIPv2*, atau *Asynchronous Engine*].

## Pembagian Tugas

[Deskripsikan dengan jelas anggota kelompok dan *milestone* yang mereka kerjakan, ini wajib diisi sesuai instruksi pengumpulan repositori.]

* **Anggota 1 13524115 (Ega Luthfi Rais):** [Bagian yang dikerjakan]
* **Anggota 2 13524141 (Ahmad Fauzan Putra):** [Bagian yang dikerjakan]
* **Anggota 3 13524146 (Leonardus Brain Fatolosja):** [Bagian yang dikerjakan]
* **Anggota 4 13524134 (Salman Faiz Assidqi):** [Bagian yang dikerjakan]
* **Anggota 5 13524124 (Zahran Alvan Putra Winarko):** [Bagian yang dikerjakan]

