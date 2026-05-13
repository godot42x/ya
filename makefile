t:=ya
cfg:=
b_args:=
r_args:=
force:=false
m:=debug

.PHONY: test vulkan-sdk-macos basic

UNAME_S := $(shell uname -s)

b: clean_if_needed cfg_if_needed
	xmake b $(t) $(b_args)

r: clean_if_needed cfg_if_needed
	xmake b $(t) $(b_args)
	xmake r $(t) $(r_args)

cfg: basic
	xmake f -c -y
	xmake f -m $(m) -y
	xmake project -k compile_commands

profile:
	npm install -g speedscope
	speedscope "./Engine/Saved/Profiling/App.speedscope.json"

clean_if_needed:
	$(if $(filter true,$(force)), xmake c )

cfg_if_needed:
	$(if $(cfg), xmake f $(cfg))

basic:
	# make imgui and imguizmo readonly
	python3 "./Script/setup_3rd_party.py"
	# MacOs vulkan-sdk check and init
ifeq ($(UNAME_S),Darwin)
	# Idempotent: returns quickly if the SDK is already present.
	python3 "./Script/setup_vulkan_sdk_macos.py"
endif

# macOS: manually (re-)install Vulkan SDK into Engine/ThirdParty/VulkanSDK/
vulkan-sdk-macos:
	python3 "./Script/setup_vulkan_sdk_macos.py"

test:
	xmake b $(t)-testing
	xmake r $(t)-testing --gtest_filter=$(r_args)
