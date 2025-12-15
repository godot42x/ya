
t:=ya
cfg:=
b_args:=
r_args:=
f:=false

.PHONY: 

b: clean_if_needed cfg_if_needed
	xmake b $(t) $(b_args)


r:  clean_if_needed cfg_if_needed
	xmake b $(t) $(b_args) 
	xmake r $(t) $(r_args)


cfg: 
	xmake f -c 
	xmake f -m debug -y
	xmake project -k compile_commands


clean_if_needed:
	$(if $(filter true,$(f)), xmake c )

cfg_if_needed:
	$(if $(cfg), xmake f  $(cfg))