add_library(Atrac9 STATIC 
    src/band_extension.c
    src/bit_allocation.c
    src/bit_reader.c
    src/decinit.c
    src/decoder.c
    src/huffCodes.c
    src/imdct.c
    src/libatrac9.c
    src/quantization.c
    src/scale_factors.c
    src/tables.c
    src/unpack.c
    src/utility.c
)

target_include_directories(Atrac9
PUBLIC
    include
PRIVATE
    include/libatrac9
)
