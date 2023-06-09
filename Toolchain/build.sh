#!/bin/bash

if [[ "$OSTYPE" == "darwin"* ]]; then
  realpath() {
      [[ $1 = /* ]] && echo "$1" || echo "$PWD/${1#./}"
  }

  cores=$(sysctl -n hw.physicalcpu)

  copyfiles() {
    find $1 -type f | grep -i $2$ | xargs -i ditto {} -t $3/. || on_error
  }
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
  cores=$(nproc)

  copyfiles() {
    find $1/ -type f | grep -i $2$ | xargs -i cp {} --parents -t $3/. || on_error
  }
fi

true_path="$(dirname "$(realpath "$0")")"
root_path=$true_path/..

on_error()
{
    echo "Cross-compiler build failed!"
    exit 1
}

arch="64"

if [ "$1" ]
  then
    if [ $1 != "64" ] && [ $1 != "32" ]
    then
      echo "Unknown architecture $1"
      on_error
    else
      arch="$1"
    fi
fi

compiler_prefix="i686"

if [ $arch = "64" ]
then
  compiler_prefix="x86_64"
else
  compiler_prefix="i686"
fi

source $root_path/Scripts/utils.sh

pushd $true_path

if [ -e "CrossCompiler/Tools$arch/bin/$compiler_prefix-pc-ultra-g++" ]
then
  exit 0
else
  echo "Building the cross-compiler for $compiler_prefix..."
fi

echo "Generating system root..."

pushd $root_path

sysroot_path="$root_path/Root$arch"
include_path="$sysroot_path/System/Includes"
library_path="$sysroot_path/System/Libraries"

mkdir -p $include_path || on_error

pushd "Userland/LibC"
copyfiles . h $include_path
popd

popd # root_path

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
  sudo apt update

  declare -a dependencies=(
              "bison"
              "flex"
              "libgmp-dev"
              "libmpc-dev"
              "libmpfr-dev"
              "texinfo"
              "libisl-dev"
              "build-essential"
          )
elif [[ "$OSTYPE" == "darwin"* ]]; then
  declare -a dependencies=(
              "bison"
              "flex"
              "gmp"
              "libmpc"
              "mpfr"
              "texinfo"
              "isl"
              "libmpc"
              "wget"
          )
fi

for dependency in "${dependencies[@]}"
do
   echo -n $dependency
   if [[ "$OSTYPE" == "linux-gnu"* ]]; then
      is_dependency_installed=$(dpkg-query -l | grep $dependency)
   elif [[ "$OSTYPE" == "darwin"* ]]; then
      is_dependency_installed=$(brew list $dependency)
   fi

   if [ -z "$is_dependency_installed" ]
    then
      echo " - not installed"
      if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        sudo apt install -y $dependency || on_error
      elif [[ "$OSTYPE" == "darwin"* ]]; then
        brew install $dependency || on_error
      fi
    else
      echo " - installed"
    fi
done

gcc_version="gcc-10.1.0"
binutils_version="binutils-2.34"
gcc_url="ftp://ftp.gnu.org/gnu/gcc/$gcc_version/$gcc_version.tar.gz"
binutils_url="https://ftp.gnu.org/gnu/binutils/$binutils_version.tar.gz"

if [ ! -e "CrossCompiler/gcc/configure" ]
then
  echo "Downloading GCC source files..."
  mkdir -p "CrossCompiler"     || on_error
  mkdir -p "CrossCompiler/gcc" || on_error
  wget -O "CrossCompiler/gcc.tar.gz" $gcc_url || on_error
  echo "Unpacking GCC source files..."
  tar -xf "CrossCompiler/gcc.tar.gz" \
      -C  "CrossCompiler/gcc" --strip-components 1  || on_error
  rm "CrossCompiler/gcc.tar.gz"

  pushd CrossCompiler
  patch -s -p0 < $true_path/Patches/gcc.patch || on_error
  popd
else
  echo "GCC is already downloaded!"
fi

if [ ! -e "CrossCompiler/binutils/configure" ]
then
  echo "Downloading binutils source files..."
  mkdir -p  "CrossCompiler"          || on_error
  mkdir -p  "CrossCompiler/binutils" || on_error
  wget -O "CrossCompiler/binutils.tar.gz" $binutils_url || on_error
  echo "Unpacking binutils source files..."
  tar -xf "CrossCompiler/binutils.tar.gz" \
      -C "CrossCompiler/binutils" --strip-components 1 || on_error
  rm "CrossCompiler/binutils.tar.gz"

  pushd CrossCompiler
  patch -s -p0 < $true_path/Patches/binutils.patch || on_error
  popd
else
  echo "binutils is already downloaded!"
fi

export PREFIX="$true_path/CrossCompiler/Tools$arch"
export TARGET="$compiler_prefix-pc-ultra"
export PATH="$PREFIX/bin:$PATH"

# Build with optimizations and no debug information
export CFLAGS="-g0 -O2"
export CXXFLAGS="-g0 -O2"

echo "Crosscompiler target $TARGET"

echo "Building binutils..."
mkdir -p "CrossCompiler/binutils_build$arch" || on_error

pushd "CrossCompiler/binutils_build$arch"
../binutils/configure --target=$TARGET \
                      --prefix="$PREFIX" \
                      --with-sysroot=$sysroot_path \
                      --disable-nls \
                      --disable-werror || on_error
make         -j$cores || on_error
make install          || on_error
popd

echo "Building GCC..."
mkdir -p "CrossCompiler/gcc_build$arch" || on_error
pushd "CrossCompiler/gcc_build$arch"

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
  ../gcc/configure --target=$TARGET \
                   --prefix="$PREFIX" \
                   --disable-nls \
                   --with-sysroot=$sysroot_path \
                   --enable-languages=c,c++ || on_error
elif [[ "$OSTYPE" == "darwin"* ]]; then
  ../gcc/configure --target=$TARGET \
                   --prefix="$PREFIX" \
                   --disable-nls \
                   --with-sysroot=$sysroot_path \
                   --enable-languages=c,c++ \
                   --with-gmp=/usr/local/opt/gmp \
                   --with-mpc=/usr/local/opt/libmpc \
                   --with-mpfr=/usr/local/opt/mpfr || on_error
fi

make all-gcc              -j$cores || on_error
make install-gcc                   || on_error
make all-target-libgcc    -j$cores || on_error
make install-target-libgcc         || on_error

popd

popd # true_path

# consider deleting the intermediate folders here
echo "Cross-compiler built successfully!"