Import("env")

build_flags = env.ParseFlags(env['BUILD_FLAGS'])
#print(build_flags.get("CPPDEFINES"))
flags = {}
for define in build_flags.get("CPPDEFINES", []) or []:
    if isinstance(define, (list, tuple)):
        if len(define) == 1:
            flags[define[0]] = None
        else:
            flags[define[0]] = define[1]
    else:
        flags[define] = None
#print(flags)
if flags.get("HARDWARE") == "MKS_28_V1_0":
    filename = "MKSTFT28"
else:
    filename = flags.get("HARDWARE") + "." + flags.get("SOFTWARE_VERSION")
#print(filename)
env.Replace(PROGNAME=filename)
