# Testing GCC Port

1. Clone this directory tree

```shell
git clone -b gcc-cmp https://github.com/fedy0/shepherd.git
```

2. Install the cross toolchain from [gnupru](https://github.com/dinuxbg/gnupru.git)

3. Install the PRU software support packages from [pssp](https://github.com/dinuxbg/pru-software-support-package.git)

4. Add PRU gcc and binutils to your PATH

```shell
export PRU_GCC=/installation_path/pru-elf-2021.12
export PRU_SUPPORT=/installation_path/pru-software-support-package
export PATH=$PATH:$PRU_GCC
```

5. Compile
```shell
cd ~/shepherd/software/firmware
make
```
