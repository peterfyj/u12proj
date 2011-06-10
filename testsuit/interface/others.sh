OTHER="bigdata\
	   fail\
	   convert1\
	   fake\
	   convert2\
	   convert\
	   receiver\
	   returntype\
	   embed\
	   struct"

for i in $OTHER
do
	echo "[Testsuit] $i.go"
	echo "./$i.out" >> testall.sh
	8g "$i.go"
	8l -o "$i.out" "$i.8"
	rm "$i.8" "$i.go"
done
