function main(argv)
    os.exec("xmake f -c")
    os.exec("xmake c")
    os.exec("xmake f -m debug --toolchain=clang-cl") -- Clang for crossplatform 
    os.exec("xmake project -k compile_commands")
end
