#!/bin/sh
# Do not compile Go;
NO_GO=
NO_RE=
NO_TEST=

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
		   time\
		   flag\
		   bufio\
		   container/ring\
		   container/vector\
		   template\
		   rand"

while
	test $1
do
	case $1 in
	--help)
		echo "parameters:"
		echo "	-ng: Do not compile the entire Go"
		echo "	-nre: Do not recompile packages of Go"
		echo "  -ntest: Do not compile test cases"
		exit
		;;
	-ng)
		NO_GO=1
		;;
	-nre)
		NO_RE=1
		;;
	-ntest)
		NO_TEST=1
		;;
	*)
		echo "'$1' not recognized."
		exit
		;;
	esac
	shift
done

if ! test $NO_GO
then
	# Compile Go;
	cd "$CURRENT/src/go/src/"
	./all.bash 
fi

# Set necessary environment;
export GOOS=ucore
export GOROOT=$CURRENT/src/go
export PATH=$PATH:$GOROOT/bin

if ! test $NO_RE
then
	# Recompile packages partly for each one;
	# Patch Go with our work;
	cp "$CURRENT/patch/src/" "$CURRENT/src/go/" -R

	for i in $BUILD_PKG
	do
		echo "[Building "$i"]"
		cd "$GOROOT/src/pkg/$i"
		make clean
		make install
	done

	# Recompile linker for a ucore target;
	cd "$CURRENT/src/go/src/cmd/8l/"
	make clean
	make install
fi

if ! test $NO_TEST
then
	# Compile test cases;

	# Copy to disk0;
	rm -f -r "$CURRENT/src/ucore/disk0/testsuit/"
	cp "$CURRENT/testsuit/" "$CURRENT/src/ucore/disk0/" -R

	# Get all directories;
	cd "$CURRENT/src/ucore/disk0/testsuit/"
	rm -f -r *.out *.8 *.s testall.sh
	TEST_DIRS=`find -type d`

	# Get all test cases;
	for i in $TEST_DIRS
	do
		# Enter one directory;
		CURRENT_TEST_DIR=`readlink -f "$CURRENT/src/ucore/disk0/testsuit/$i"`
		cd "$CURRENT_TEST_DIR"

		# Get cases in current directory;
		TEST_SUIT=`ls *.go | sed s'/.go//g'`
		for j in $TEST_SUIT
		do
			echo "[Testsuit] $j.go"
			echo "./$j.out" >> testall.sh
			8g "$j.go"
			8l -o "$j.out" "$j.8"
			rm "$j.8" "$j.go"
		done

		rm -f "$CURRENT/src/ucore/bin/fs.img"
	done
fi

# Now start ucore;
cd "$CURRENT/src/ucore/"
make qemu
