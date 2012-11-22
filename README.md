Tools
=====

OpenFOAM utilities and tools.

foamDict
--------
Utility to query and manipulate OpenFOAM dictionary files. Refer to
[`foamDict/foamDict.C`](tools/blob/master/foamDict/foamDict.C) for the details.

writeCellDist
-------------
Utility to create a field `cellDist` in the processor directories at
`<startTime>`. The `decomposePar` utility also writes a `cellDist` field if the
`-cellDist` option is used, but puts the output into the reconstructed case.
This wastes a lot of space, because the field is no longer uniform, and this
cannot be done *after* the decomposition.

Note that this utility has to be run in parallel in order to be useful.
