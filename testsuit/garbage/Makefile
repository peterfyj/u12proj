COMP := 8g
LINK := 8l
RM := rm
ECHO := echo
V := @

all: peano tree
	
peano:
	$(V)$(ECHO) [Testsuit] peano.go
	$(V)$(COMP) peano.go stats.go
	$(V)$(LINK) -o peano.out peano.8
	$(V)$(ECHO) "./peano.out" >> testall.sh
	
tree:
	$(V)$(ECHO) [Testsuit] tree.go
	$(V)$(COMP) tree.go stats.go
	$(V)$(LINK) -o tree.out peano.8
	$(V)$(ECHO) "./tree.out" >> testall.sh
	
clean:
	$(V)$(RM) -f *.8
