# BCDB core

The BCDB core is responsible for taking LLVM modules and splitting out each
function definition into its own module. It then stores each of these modules
in MemoDB, along with other information needed to recreate the original module.
MemoDB will automatically deduplicate the function definitions whenever two
of them are identical, no matter whether they came from the same LLVM module or
from different modules. The BCDB core can also reverse the process, retrieving
the parts of a module from MemoDB and reconstituting the original module.

The BCDB core makes a few changes to the LLVM IR in order to improve
deduplication:

- Names are assigned to all anonymous global variables and functions.
- Global string constants are renamed based on a hash of their contents.
- Pointers to structures are replaced by opaque pointers (i.e., void pointers)
  when the details of the structure don't matter.

Aside from these changes, the reconstituted LLVM module is expected to be
semantically identical to the original module.

## Usage

See [BCDB CLI].

[BCDB CLI]: ./cli.md
