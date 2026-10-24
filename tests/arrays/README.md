# Multidimensional array fixture

`ArrayDims.java` is compiled into `arrays_fixture.h` and run by
`tests/test_arrays.c` through real bytecode, the same way
`tests/ioapi/` embeds its classes.

Regenerate after editing the source:

    javac --release 8 -d /tmp/arrfix tests/arrays/ArrayDims.java
    python3 - <<'PY'
    b = open('/tmp/arrfix/ArrayDims.class','rb').read()
    with open('tests/arrays/arrays_fixture.h','w') as f:
        f.write('static const unsigned char arraydims_ArrayDims_class[] = {\n')
        for i in range(0, len(b), 12):
            f.write('    ' + ', '.join('0x%02x' % c for c in b[i:i+12]) + ',\n')
        f.write('};\n')
    PY

The header is checked in so the CMake build needs no Java toolchain.
