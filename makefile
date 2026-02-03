
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


cfg: basic
	xmake f -c -y
	xmake f -m debug -y
	xmake project -k compile_commands

profile:
	npm install -g speedscope
	speedscope "./Engine/Saved/Profiling/App.speedscope.json"

clean_if_needed:
	$(if $(filter true,$(f)), xmake c )

cfg_if_needed:
	$(if $(cfg), xmake f  $(cfg))

basic:
	# make imgui and imguizmo readonly
	python3  "./Script/setup_3rd_party.py"