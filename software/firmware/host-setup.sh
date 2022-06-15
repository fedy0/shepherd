#!/bin/sh
# chmod +x host-setup.sh
# sudo ./host-setup.sh

# 1. Create a directory to host the tools
echo "Create a tools directory"
mkdir -p tools
echo "Change to tools directory"
cd tools

# 2. Install the cross-compiler toolchain by downloading and untar gnupru release
echo "Downloading the cross-compiler toolchain..."
wget -r -tries=2 https://github.com/dinuxbg/gnupru/releases/download/2022.05/pru-elf-2022.05.amd64.tar.xz --output-document=gcc-port -o wgetlog 
echo "Untar'ing the cross-compiler toolchain..."
tar -xf gcc-port
echo "Deleting toolchain archive..."
rm -rf gcc-port

# 3. Install the PRU software support packages from the pru-software-support-package (branch name: linux-4.19-rproc)
git clone -b linux-4.19-rproc https://github.com/dinuxbg/pru-software-support-package.git

# 4. Add PRU gcc and binutils to ylsour PATH
echo "Add PRU GCC supports to path"
export PRU_GCC=$1/tools/pru-elf-*/bin
export PRU_SUPPORT=$1/tools/pru-software-support-package
export PATH=$PATH:$PRU_GCC

# 5. Compile
echo "Compiling..."
cd ../
make
