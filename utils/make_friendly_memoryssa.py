# FalseMemorySSA needs to access some protected members of classes defined in
# MemorySSA.h. We rewrite MemorySSA.h to add FalseMemorySSA as a friend.
import re
import sys

header = open(sys.argv[1]).read()
header = re.sub(
    r"class (MemoryUseOrDef|MemoryAccess|MemoryPhi)[^;]*?{",
    r"\g<0>friend class FalseMemorySSA;",
    header,
)
open(sys.argv[2], "w").write(header)
