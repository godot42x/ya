
t:=ya
b_args:=
r_args:=

.PHONY: 

b:
	xmake b $(t) $(b_args)


r: 
	xmake b $(t) $(b_args) 
	xmake r $(t) $(r_args)

cfg: 
	xmake f -m debug -y
	xmake project -k compile_commands

