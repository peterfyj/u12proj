#!/usr/bin/env bash
# Do not compile Go;
NO_GO=false

# Get current directory;
CURRENT=`pwd`

# Packages that needed to be partly recompiled;
BUILD_PKG="runtime\
		   sync/atomic\
		   sync\
		   syscall\
		   os\
		   io\
		   unicode\
		   utf8\
		   bytes\
		   math\
		   strings\
		   strconv\
		   reflect\
		   fmt\
		   sort\
		   container/heap\
		   path/filepath\
		   io/ioutil\
		   time"

# The test cases;
TEST_SUIT="hw1\
		   hw2\
		   sieve1\
		   goroutines\
		   peter"

while
	test $1
do
	case $1 in
	--help)
		echo "parameters:"
		echo "	-ng: Do not compile the entire Go"
		exit
		;;
	-ng)
		NO_GO=true
		;;
	*)
		echo "'$1' not recognized."
		exit
		;;
	esac
	shift
done

if !$NO_GO; then
	# Compile Go;
	cd $CURRENT/src/go/src/
	./all.bash 
fi

# Patch Go with our work;
cp $CURRENT/patch/src/ $CURRENT/src/go/ -R

# Set necessary environment;
export GOOS=ucore
export GOROOT=$CURRENT/src/go
export PATH=$PATH:$GOROOT/bin

# Recompile packages partly for each one;
for i in $BUILD_PKG
do
	echo "[Building "$i"]"
	cd $GOROOT/src/pkg/$i
	make clean
	make install
done

# Recompile linker for a ucore target;
cd $CURRENT/src/go/src/cmd/8l/
make clean
make install

# Compile test cases;
cd $CURRENT/testsuit/
rm *.out *.s *.8
for i in $TEST_SUIT
do
	8g $i.go
	8l -a -o $i.out $i.8 > $i.s
	cp $i.out $CURRENT/src/ucore/disk0/
done

# Now start ucore;
cd $CURRENT/src/ucore/
make qemu
