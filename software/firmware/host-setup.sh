#!/bin/sh
# chmod +x host-setup.sh
# sudo ./host-setup.sh

# 1. Create a directory to host the tools
echo "1. Create a tools directory"
mkdir -p tools
echo "2. Change to tools directory"
cd tools

# 2. Install the cross-compiler toolchain by downloading and untar gnupru release
echo "2.1 Downloading the cross-compiler toolchain..."
wget -r -tries=2 https://github.com/dinuxbg/gnupru/releases/download/2022.05/pru-elf-2022.05.amd64.tar.xz --output-document=gcc-port -o log 
echo "2.2 Untar'ing the cross-compiler toolchain..."
tar -xf gcc-port
echo "2.3 Deleting toolchain archive..."
rm -rf gcc-port

# 3. Install the PRU software support packages from the pru-software-support-package (branch name: linux-4.19-rproc)
echo "3. Cloning PRU software support packages (linux-4.19-rproc branch)"
git clone -b linux-4.19-rproc https://github.com/dinuxbg/pru-software-support-package.git

# 4. Add PRU gcc and binutils to your PATH
echo "4. Adding PRU GCC supports to path..."
export PRU_GCC=$PWD/pru-elf-*/bin
export PRU_SUPPORT=$PWD/pru-software-support-package
export PATH=$PATH:$PRU_GCC

# 5. Patch PRU software support packages
echo "5. Patching PRU software support packages..."

# 6. Compile
echo "6. Compiling..."
cd ../
make
