# 🚀 Multithreaded Download Manager in C

A **Multithreaded Download Manager** written in **C** that downloads files from a given URL using multiple threads. It splits the file into chunks and downloads them concurrently for faster performance.

---

## ✨ Features

- 🔗 Downloads files using multiple threads  
- 🌐 Uses `libcurl` for HTTP/HTTPS requests  
- 🔄 Supports resume and chunked downloads  
- 🧩 Merges downloaded parts into a single output file  
- ❗ Implements error handling for invalid URLs and connection issues  
- 🖥️ GUI Supported

---

## 📁 Project Structure


---

## 🛠️ Prerequisites

Make sure you have the following installed:

- GCC Compiler (`gcc`)
- libcurl (`libcurl-dev` or `libcurl-devel`)
- POSIX Threads (`pthreads`) *(Included by default on Linux/macOS)*

---

## 📦 Installing libcurl

### 🔹 On Ubuntu / Debian
```bash
sudo apt update
sudo apt install libcurl4-openssl-dev


🔹 On macOS (via Homebrew)

brew install curl

🔧 Compilation & Execution
1️⃣ Compile the Program

🔹 On Linux/macOS:
gcc -o downloader downloader.c -lpthread -lcurl

2️⃣ Run the Program
./downloader <URL> <NUMBER_OF_THREADS> <OUTPUT_FILENAME>


🔸 Example:
./downloader https://example.com/file.zip 4 output.zip
This will download file.zip using 4 parallel threads and save it as output.zip.

🧑‍💻 Author
Zohaib Raza
📅 Last Updated: 7 May 2025

📜 License
This project is open-source and freely usable/modifiable. Contributions are welcome!
---

Would you like a badge layout (like GitHub stars/forks/downloads) or GUI screenshots added too?
