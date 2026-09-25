# Writes a file's bytes as a C array (cmake -P): -DINPUT=<file> -DOUTPUT=<file.c> -DNAME=<symbol>.
# The array is `const unsigned char NAME[]`, its size `const unsigned long NAME_size`. Used to put data the app needs
# into the binary (the math font of the Markdown formulas), so it works the same on every platform, Android included.

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" length)
math(EXPR size "${length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE "${OUTPUT}.tmp"
    "/* Generated from ${INPUT} by XqtEmbedFile.cmake: do not edit. */\n"
    "const unsigned char ${NAME}[] = {\n${bytes}\n};\n"
    "const unsigned long ${NAME}_size = ${size}UL;\n")
file(RENAME "${OUTPUT}.tmp" "${OUTPUT}")
