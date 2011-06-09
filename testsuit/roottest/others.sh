OTHER=" 235\
		64bit\
		append\
		args\
		bigalg\
		bigmap\
		blank\
		chancap\
		char_lit\
		closedchan\
		closure\
		cmp1\
		complit\
		compos\
		const3\
		const\
		convert\
		copy\
		ddd\
		decl\
		defer\
		deferprint\
		env\
		escape\
		floatcmp\
		float_lit\
		for\
		func5\
		func\
		gc1\
		gc\
		hashmap\
		helloworld\
		if\
		index\
		indirect\
		initcomma\
		initialize\
		initsyscall\
		intcvt\
		int_lit\
		iota\
		literal\
		malloc1\
		mallocfin\
		mallocrand\
		mallocrep1\
		mallocrep\
		map\
		method3\
		method\
		named\
		nil\
		nul1\
		peano\
		printbig\
		range\
		recover1\
		recover2\
		recover3\
		recover\
		rename\
		sigchld\
		simassign\
		solitaire\
		stack\
		string_lit\
		stringrange\
		switch1\
		switch\
		test0\
		turing\
		typeswitch1\
		typeswitch\
		utf\
		varinit\
		vectors\
		zerodivide"

for i in $OTHER
do
	echo "[Testsuit] $i.go"
	echo "./$i.out" >> testall.sh
	8g "$i.go"
	8l -o "$i.out" "$i.8"
	rm "$i.8" "$i.go"
done
