#!/bin/sh
# Do not compile Go;
NO_GO=
NO_RE=

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
		   template"

while
	test $1
do
	case $1 in
	--help)
		echo "parameters:"
		echo "	-ng: Do not compile the entire Go"
		echo "	-nre: Do not recompile packages of Go"
		exit
		;;
	-ng)
		NO_GO=1
		;;
	-nre)
		NO_RE=1
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

# Compile test cases;
cd "$CURRENT/testsuit/"
# Get all test cases;
TEST_SUIT=`ls *.go | sed s'/.go//g'`
if test -e "*.out"
then
	rm *.out
fi
if test -e "*.s"
then
	rm *.s
fi
if test -e "*.8"
then
	rm *.s
fi
if test -e "testall.sh"
then
	rm testall.sh
fi
for i in $TEST_SUIT
do
	echo "[Testsuit] $i.go"
	echo "./$i.out" >> testall.sh
	8g "$i.go"
	8l -a -o "$i.out" "$i.8" > "$i.s"
	cp "$i.out" "$CURRENT/src/ucore/disk0/"
done
cp "testall.sh" "$CURRENT/src/ucore/disk0/"

if test -e "$CURRENT/src/ucore/bin/fs.img"
then
	rm "$CURRENT/src/ucore/bin/fs.img"
fi

# Now start ucore;
cd "$CURRENT/src/ucore/"
make qemu
