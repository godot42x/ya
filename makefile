
.PHONY: compile_commands

r: 
	xmake b && xmake r

cfg:
	xmake f -m debug -y
	xmake project -k compile_commands

