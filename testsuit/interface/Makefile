COMP := 8g
LINK := 8l
RM := rm
ECHO := echo
V := @
SHELL := sh

OTHER := "bigdata\
		  fail\
		  convert1\
		  fake\
		  convert2\
		  receiver1\
		  convert\
		  receiver\
		  recursive\
		  returntype\
		  embed\
		  struct"


all: embed1 others

embed1:
	$(V)$(ECHO) [Testsuit] embed1.go
	$(V)$(COMP) embed0.go
	$(V)$(COMP) embed1.go
	$(V)$(LINK) -o embed1.out embed1.8
	$(V)$(ECHO) "./embed1.out" >> testall.sh
	
others:
	$(V)$(SHELL) others.sh
	
clean:
	$(V)$(RM) -f *.8 *.go
