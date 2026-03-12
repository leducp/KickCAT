# embed_binary.cmake - Convert a binary file into a C header with a byte array.
# Invoked at build time via add_custom_command.
#
# Required variables (pass via -D):
#   INPUT_FILE  - path to the binary file to embed
#   OUTPUT_FILE - path to the generated C header
#   VAR_NAME    - C variable name for the byte array

file(READ "${INPUT_FILE}" hex_content HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," c_array "${hex_content}")
file(SIZE "${INPUT_FILE}" file_size)
file(WRITE "${OUTPUT_FILE}"
    "#ifndef ${VAR_NAME}_H\n"
    "#define ${VAR_NAME}_H\n"
    "\n"
    "constexpr unsigned char ${VAR_NAME}[] = {\n    ${c_array}\n};\n"
    "constexpr unsigned int ${VAR_NAME}_len = ${file_size};\n"
    "\n"
    "#endif\n"
)
