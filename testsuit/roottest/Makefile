COMP := 8g
LINK := 8l
RM := rm
ECHO := echo
V := @
SHELL := sh

all: cmplxdivide ddd3 others

cmplxdivide:
	$(V)$(ECHO) [Testsuit] cmplxdivide.go
	$(V)$(COMP) cmplxdivide.go cmplxdivide1.go
	$(V)$(LINK) -o cmplxdivide.out cmplxdivide.8
	$(V)$(ECHO) "./cmplxdivide.out" >> testall.sh
	
ddd3:
	$(V)$(ECHO) [Testsuit] ddd3.go
	$(V)$(COMP) ddd2.go
	$(V)$(COMP) ddd3.go
	$(V)$(LINK) -o ddd3.out ddd3.8
	$(V)$(ECHO) "./ddd3.out" >> testall.sh

others:
	$(V)$(SHELL) others.sh
	
clean:
	$(V)$(RM) -f *.8 *.go
