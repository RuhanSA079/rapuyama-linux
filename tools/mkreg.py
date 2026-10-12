# python2, runs inside the ensymble container. makes a symbian app registration
# resource (_reg.rsc) and a localisable caption resource (_loc.rNN) for a NATIVE
# exe, same layout as ensymble's py2sis resources
# argv: <uid3hex> <exename> <caption> <outdir>
# makes <outdir>/<exename>_reg.rsc and <outdir>/<exename>_loc.r01
import sys, os
sys.path.insert(0, "/ensymble/ensymble")
from utils import rscfile

uid3 = int(sys.argv[1], 16)
exename = sys.argv[2] # e.g. "ptdump_0xe1000004" (basename of the exe)
caption = sys.argv[3].decode("utf-8")
outdir = sys.argv[4]

locbase = u"\\resource\\apps\\%s_loc" % exename
rw = rscfile.RSCWriter(uid2=0x101f8021, uid3=uid3)
res = rscfile.Resource(["LONG", "LLINK", "LTEXT", "LONG", "LTEXT", "LONG", "BYTE", "BYTE", "BYTE", "BYTE", "LTEXT", "BYTE", "WORD", "WORD", "WORD", "LLINK"], 0, 0, exename, 0, locbase, 1, 0, 0, 0, 0, "", 0, 0, 0, 0, 0)
rw.addresource(res)
open(os.path.join(outdir, "%s_reg.rsc" % exename), "wb").write(rw.tostring())
rw = rscfile.RSCWriter(uid2=0, offset="    ")
res = rscfile.Resource(["LONG", "LLINK", "LTEXT", "LONG", "LLINK", "LTEXT", "WORD", "LTEXT", "WORD", "LTEXT"], 0, 0, caption, 0, 0, caption, 0, "", 0, "")
rw.addresource(res)
open(os.path.join(outdir, "%s_loc.r01" % exename), "wb").write(rw.tostring())

print "wrote %s_reg.rsc and %s_loc.r01" % (exename, exename)