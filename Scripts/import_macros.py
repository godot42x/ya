import os
from re import sub
import subprocess

macro_dirs = "./Scripts/macros/"


def getMacoFilse(dir):

    ret = list()

    for root, dir, files in os.walk(dir):
        # print(root, dir, files, "\n")
        for file in files:
            if file.endswith(".lua"):
                name = file.removesuffix(".lua")
                path = root + file
                ret.append((name, path))

    return ret


if __name__ == '__main__':
    files = getMacoFilse(macro_dirs)
    # print(files)

    ts = list()

    for name, path in files:
        print(name, path)

        precmd = ["xmake", "m"]
        macro_path_arg = "--import="+path
        precmd.append(macro_path_arg)
        precmd.append(name)

        # cmd = "xmake m --import="+path+" " + name

        subprocess.run(precmd)
