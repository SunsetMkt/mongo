#!/usr/bin/env python3
#
# Public Domain 2014-present MongoDB, Inc.
# Public Domain 2008-2014 WiredTiger, Inc.
#
# This is free and unencumbered software released into the public domain.
#
# Anyone is free to copy, modify, publish, use, compile, sell, or
# distribute this software, either in source code form or as a compiled
# binary, for any purpose, commercial or non-commercial, and by any
# means.
#
# In jurisdictions that recognize copyright laws, the author or authors
# of this software dedicate any and all copyright interest in the
# software to the public domain. We make this dedication for the benefit
# of the public at large and to the detriment of our heirs and
# successors. We intend this dedication to be an overt act of
# relinquishment in perpetuity of all present and future rights to this
# software under copyright law.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.

# Dump/decode a WiredTiger .wt file.

# This is not a complete dump program and should not be depended upon for correctness --
# see "wt dump" for that.  But this is standalone (doesn't require linkage with any WT
# libraries), and may be useful as 1) a learning tool 2) quick way to hack/extend dumping.

import binascii, codecs, io, os, string, sys, traceback
from dataclasses import dataclass
import typing

# Optional dependency: crc32c
have_crc32c = False
try:
    import crc32c
    have_crc32c = True
except:
    pass

# Optional dependency: python-snappy
have_snappy = False
try:
    import snappy
    have_snappy = True
except:
    pass

decode_version = "2023-03-03.0"

# A container for fields in the WT_PAGE_HEADER
class PageHeader:
    pass

# A container for fields in the WT_BLOCK_HEADER
@dataclass
class BlockHeader:
    disk_size: int
    checksum: int
    flags: int

# A container for page stats
@dataclass
class PageStats:
    num_keys: int = 0
    keys_sz: int = 0

    # prepared updates (not reported currently)
    num_d_start_ts: int = 0
    d_start_ts_sz: int = 0
    num_d_stop_ts: int = 0
    d_stop_ts_sz: int = 0

    # durable history
    num_start_ts: int = 0
    start_ts_sz: int = 0
    num_stop_ts: int = 0
    stop_ts_sz: int = 0
    num_start_txn: int = 0
    start_txn_sz: int = 0
    num_stop_txn: int = 0
    stop_txn_sz: int = 0

    @property
    def num_ts(self) -> int:
        return self.num_d_start_ts + self.num_d_stop_ts + self.num_start_ts + self.num_stop_ts
    
    @property
    def ts_sz(self) -> int:
        return self.d_start_ts_sz + self.d_stop_ts_sz + self.start_ts_sz + self.stop_ts_sz
    
    @property
    def num_txn(self) -> int:
        return self.num_start_txn + self.num_stop_txn
    
    @property
    def txn_sz(self) -> int:
        return self.start_txn_sz + self.stop_txn_sz
    
    @staticmethod
    def csv_cols() -> list[str]:
        return [
            'num keys',
            'keys size',
            'num durable start ts',
            'durable start ts size',
            'num durable stop ts',
            'durable stop ts size',
            'num start ts',
            'start ts size',
            'num stop ts',
            'stop ts size',
            'num ts',
            'ts size',
            'num start txn',
            'start txn size',
            'num stop txn',
            'stop txn size',
            'num txn',
            'txn size'
        ]

    def to_csv_cols(self) -> list[int]:
        return [
            self.num_keys,
            self.keys_sz,
            self.num_d_start_ts,
            self.d_start_ts_sz,
            self.num_d_stop_ts,
            self.d_stop_ts_sz,
            self.num_start_ts,
            self.start_ts_sz,
            self.num_stop_ts,
            self.stop_ts_sz,
            self.num_ts,
            self.ts_sz,
            self.num_start_txn,
            self.start_txn_sz,
            self.num_stop_txn,
            self.stop_txn_sz,
            self.num_txn,
            self.txn_sz,
        ]
        

################################################################
# Borrowed from intpacking.py, with small adjustments.
# Variable-length integer packing
# need: up to 64 bits, both signed and unsigned
#
# Try hard for small values (up to ~2 bytes), after that, just encode the
# length in the first byte.
#
#  First byte | Next |                        |
#  byte       | bytes| Min Value              | Max Value
# ------------+------+------------------------+--------------------------------
# [00 00xxxx] | free | N/A                    | N/A
# [00 01llll] | 8-l  | -2^64                  | -2^13 - 2^6
# [00 1xxxxx] | 1    | -2^13 - 2^6            | -2^6 - 1 
# [01 xxxxxx] | 0    | -2^6                   | -1
# [10 xxxxxx] | 0    | 0                      | 2^6 - 1 
# [11 0xxxxx] | 1    | 2^6                    | 2^13 + 2^6 - 1
# [11 10llll] | l    | 2^13 + 2^6             | 2^64 - 1
# [11 11xxxx] | free | N/A                    | N/A

NEG_MULTI_MARKER = 0x10
NEG_2BYTE_MARKER = 0x20
NEG_1BYTE_MARKER = 0x40
POS_1BYTE_MARKER = 0x80
POS_2BYTE_MARKER = 0xc0
POS_MULTI_MARKER = 0xe0

NEG_1BYTE_MIN = -2**6
NEG_2BYTE_MIN = -2**13 + NEG_1BYTE_MIN
POS_1BYTE_MAX = 2**6 - 1
POS_2BYTE_MAX = 2**13 + POS_1BYTE_MAX

_python3 = (sys.version_info >= (3, 0, 0))

if not _python3:
    raise Exception('This script requires Python 3')

def _ord(b):
    return b

def _chr(x, y=None):
    a = [x]
    if y != None:
        a.append(y)
    return bytes(a)

def _is_string(s):
    return type(s) is str

def _string_result(s):
    return s.decode()

def getbits(x, start, end=0):
    '''return the least significant bits of x, from start to end'''
    return (x & ((1 << start) - 1)) >> (end)

def get_int(b, size):
    r = 0
    for i in range(size):
        r = (r << 8) | _ord(b[i])
    return r

def unpack_int(b):
    marker = _ord(b[0])
    if marker < NEG_MULTI_MARKER or marker >= 0xf0:
        raise ValueError('Not a packed integer')
    elif marker < NEG_2BYTE_MARKER:
        sz = 8 - getbits(marker, 4)
        if sz < 0:
            raise ValueError('Not a valid packed integer')
        part1 = (-1 << (sz << 3))
        part2 = get_int(b[1:], sz)
        part3 = b[sz+1:]
        return (part1 | part2, part3)
    elif marker < NEG_1BYTE_MARKER:
        return (NEG_2BYTE_MIN + ((getbits(marker, 5) << 8) | _ord(b[1])), b[2:])
    elif marker < POS_1BYTE_MARKER:
        return (NEG_1BYTE_MIN + getbits(marker, 6), b[1:])
    elif marker < POS_2BYTE_MARKER:
        return (getbits(marker, 6), b[1:])
    elif marker < POS_MULTI_MARKER:
        return (POS_1BYTE_MAX + 1 +
               ((getbits(marker, 5) << 8) | _ord(b[1])), b[2:])
    else:
        sz = getbits(marker, 4)
        return (POS_2BYTE_MAX + 1 + get_int(b[1:], sz), b[sz+1:])

################################################################

# page types from btmem.h:
WT_PAGE_INVALID = 0
WT_PAGE_BLOCK_MANAGER = 1
WT_PAGE_COL_FIX = 2
WT_PAGE_COL_INT = 3
WT_PAGE_COL_VAR = 4
WT_PAGE_OVFL = 5
WT_PAGE_ROW_INT = 6
WT_PAGE_ROW_LEAF = 7

def pagetype_string(b):
    if b == WT_PAGE_INVALID:
        return 'WT_PAGE_INVALID'
    elif b == WT_PAGE_BLOCK_MANAGER:
        return 'WT_PAGE_BLOCK_MANAGER'
    elif b == WT_PAGE_COL_FIX:
        return 'WT_PAGE_COL_FIX'
    elif b == WT_PAGE_COL_INT:
        return 'WT_PAGE_COL_INT'
    elif b == WT_PAGE_COL_VAR:
        return 'WT_PAGE_COL_VAR'
    elif b == WT_PAGE_OVFL:
        return 'WT_PAGE_OVFL'
    elif b == WT_PAGE_ROW_INT:
        return 'WT_PAGE_ROW_INT'
    elif b == WT_PAGE_ROW_LEAF:
        return 'WT_PAGE_ROW_LEAF'
    else:
        return '*** unknown page type ***'

################################################################
class file_as_array(object):
    def __init__(self, fileobj):
        self.fileobj = fileobj
        self.pos = 0
        self.slicepos = 0

    def __getitem__(self, key):
        #print('POSITION ' + str(self.pos))
        if isinstance(key, slice):
            # We only handle limited slices, like this: x[n:]
            if key.stop != None and key.step != None:
                raise ValueError('slice error: ' + str(key))
            #print('SLICE start ' + str(key.start))
            #import pdb
            #pdb.set_trace()
            ret = file_as_array(self.fileobj)
            ret.slicepos = key.start
            self.pos = -1
            return ret
        elif isinstance(key, int):
            if self.pos != key:
                self.fileobj.seek(key + self.slicepos)
                self.pos = key
            ret = self.fileobj.read(1)[0]
            self.pos += 1
            #print('RETURN ' + str(ret) + ' pos=' + str(self.pos))
            return ret
        else:
            raise ValueError('not implemented: ' + str(key))

# An encapsulation around a file object that saves bytes read
# in increments so raw bytes can be shown.
class BinFile(object):
    def __init__(self, fileobj):
        self.fileobj = fileobj
        self.saved = bytearray()

    def read(self, n):
        if n < 0:
            raise Exception('The read length must be >= 0')
        result = self.fileobj.read(n)
        self.saved += result
        return result

    def seek(self, n):
        # Throw away previous saved when we seek
        self.saved = bytearray()
        return self.fileobj.seek(n)

    def tell(self):
        return self.fileobj.tell()

    # Return bytes read since last call to this function
    def saved_bytes(self):
        result = self.saved
        self.saved = bytearray()
        return result

# Manages printing to output.
# We keep track of cells, the first line printed for a new cell
# shows the cell number, subsequent lines are indented a little.
# If the split option is on, we show any bytes that were used
# in decoding before the regular decoding output appears.
# Those 'input bytes' are shown shifted to the right.
class Printer(object):
    def __init__(self, binfile, issplit, verbose = False):
        self.binfile = binfile
        self.issplit = issplit
        self.verbose = verbose
        self.cellpfx = ''
        self.in_cell = False

    def begin_cell(self, cell_number):
        self.cellpfx = f'{cell_number}: '
        self.in_cell = True
        ignore = self.binfile.saved_bytes()  # reset the saved position

    def end_cell(self):
        self.in_cell = False
        self.cellpfx = ''

    # This is the 'print' function, used as p.rint()
    def rint(self, s):
        if self.issplit:
            saved_bytes = self.binfile.saved_bytes()[:]
            # For the split view, we want to have the bytes related to
            # stuff to be normally printed to appear indented by 40 spaces,
            # with 10 more spaces to show a possibly abbreviated file position.
            # If we are beginning a cell, we want that to appear left justified,
            # within the 40 spaces of indentation.
            if len(saved_bytes) > 0:
                # create the 10 character file position
                # the current file position has actually advanced by
                # some number of bytes, so subtract that now.
                cur_pos = self.binfile.tell() - len(saved_bytes)
                file_pos = f'{cur_pos:x}'
                if len(file_pos) > 8:
                    file_pos = '...' + file_pos[-5:]
                elif len(file_pos) < 8:
                    file_pos = ' ' * (8 - len(file_pos)) + file_pos
                file_pos += ': '

                indentation = (self.cellpfx + ' ' * 40)[0:40]
                self.cellpfx = ''
                while len(saved_bytes) > 20:
                    print(indentation + file_pos + str(saved_bytes[:20].hex(' ')))
                    saved_bytes = saved_bytes[20:]
                    indentation = ' ' * 40
                    file_pos = ' ' * 10
                print(indentation + file_pos + str(saved_bytes.hex(' ')))

        pfx = self.cellpfx
        self.cellpfx = ''
        if pfx == '' and self.in_cell:
            pfx = '  '
        print(pfx + s)

    def rint_v(self, s):
        if self.verbose:
            self.rint(s)

# 'b' as an argument below indicates a binary file or io.BytesIO type
def uint8(b):
    return int.from_bytes(b.read(1), byteorder='little')

def uint16(b):
    return int.from_bytes(b.read(2), byteorder='little')

def uint32(b):
    return int.from_bytes(b.read(4), byteorder='little')

def uint64(b):
    return int.from_bytes(b.read(8), byteorder='little')

def ts(uint64):
    return hex(uint64)

def txn(uint64):
    return hex(uint64)

def unpack_uint64(b):
    arr = file_as_array(b)
    val, arr = unpack_int(arr)
    return val

def unpack_uint64_with_sz(b):
    start = b.tell()
    arr = file_as_array(b)
    val, arr = unpack_int(arr)
    sz = b.tell() - start
    return (val, sz)

# Print the bytes, which may be keys or values.
def raw_bytes(b):
    if type(b) != type(b''):
        # Not bytes, it's already a string.
        return b

    # If the high bit of the first byte is on, it's likely we have
    # a packed integer.  If the high bit is off, it's possible we have
    # a packed integer (it would be negative) but it's harder to guess,
    # we'll presume a string.  But if the byte is 0x7f, that's ASCII DEL,
    # very unlikely to be the beginning of a string, but it decodes as -1,
    # so seems more likely to be an int.  If the UTF-8 decoding of the
    # string fails, we probably just have binary data.

    # Try decoding as one or more packed ints
    result = ''
    s = b
    while len(s) > 0 and s[0] >= 0x7f:
        val, s = unpack_int(s)
        if result != '':
            result += ' '
        result += f'<packed {d_and_h(val)}>'
    if len(s) == 0:
        return result
    
    # See if the rest of the bytes can be decoded as a string
    try:
        if result != '':
            result += ' '
        return f'"{result + s.decode()}"'
    except:
        pass
    
    # The earlier steps failed, so it must be binary data
    return binary_to_pretty_string(b, start_with_line_prefix=False)

# Return a length as used in a cell that isn't a "short" cell.  Lengths that are
# less or equal to 64 (WT_CELL_SIZE_ADJUST) are packed in a short cell, so if a
# non-short cell is used, the length numbering starts at 64.
def long_length(p, b):
    l = unpack_uint64(b) + 64
    if l < 0:
        p.rint('? unexpected negative length: ' + str(l))
    return l
    
# Show an integer as decimal and hex
def d_and_h(n):
    return f'{n} (0x{n:x})'

# Convert binary data to a multi-line string with hex and printable characters
def binary_to_pretty_string(b, per_line=16, line_prefix='  ', start_with_line_prefix=True):
    printable = ''
    result = ''
    if start_with_line_prefix:
        result += line_prefix
    if len(b) == 0:
        return result
    for i in range(0, len(b)):
        if i > 0:
            if i % per_line == 0:
                result += '  ' + printable + '\n' + line_prefix
                printable = ''
            else:
                result += ' '
        result += '%02x' % b[i]
        if b[i] >= ord(' ') and b[i] < 0x7f:
            printable += chr(b[i])
        else:
            printable += '.'
    if i % per_line != per_line - 1:
        for j in range(i % per_line + 1, per_line):
            result += '   '
    result += '  ' + printable
    return result

def dumpraw(p, b, pos):
    savepos = b.tell()
    b.seek(pos)
    i = 0
    per_line = 16
    s = binary_to_pretty_string(b.read(256), per_line=per_line, line_prefix='')
    for line in s.splitlines():
        p.rint_v(hex(pos + i) + ':  ' + line)
        i += per_line
    b.seek(savepos)

def file_header_decode(p, b):
    # block.h
    #blockdesc = b.read(16)
    magic = uint32(b)
    major = uint16(b)
    minor = uint16(b)
    checksum = uint32(b)
    unused = uint32(b)
    p.rint('magic: ' + str(magic))
    p.rint('major: ' + str(major))
    p.rint('minor: ' + str(minor))
    p.rint('checksum: ' + str(checksum))
    if magic != 120897:
        p.rint('bad magic number')
        return
    if major != 1:
        p.rint('bad major number')
        return
    if minor != 0:
        p.rint('bad minor number')
        return
    if unused != 0:
        p.rint('garbage in unused bytes')
        return
    p.rint('')

def process_timestamps(p, b, extra, pagestats):
    # from cell.h
    WT_CELL_PREPARE = 0x01
    WT_CELL_TS_DURABLE_START = 0x02
    WT_CELL_TS_DURABLE_STOP = 0x04
    WT_CELL_TS_START = 0x08
    WT_CELL_TS_STOP = 0x10
    WT_CELL_TXN_START = 0x20
    WT_CELL_TXN_STOP = 0x40
    
    if extra != 0:
        p.rint_v('cell has timestamps:')
        if extra & WT_CELL_PREPARE != 0:
            p.rint_v(' prepared')
        if extra & WT_CELL_TS_DURABLE_START != 0:
            t, sz = unpack_uint64_with_sz(b)
            pagestats.d_start_ts_sz += sz
            pagestats.num_d_start_ts += 1
            p.rint_v(' durable start ts: ' + ts(t))
        if extra & WT_CELL_TS_DURABLE_STOP != 0:
            t, sz = unpack_uint64_with_sz(b)
            pagestats.d_stop_ts_sz += sz
            pagestats.num_d_stop_ts += 1
            p.rint_v(' durable stop ts: ' + ts(t))
        if extra & WT_CELL_TS_START != 0:
            t, sz = unpack_uint64_with_sz(b)
            pagestats.start_ts_sz += sz
            pagestats.num_start_ts += 1
            p.rint_v(' start ts: ' + ts(t))
        if extra & WT_CELL_TS_STOP != 0:
            t, sz = unpack_uint64_with_sz(b)
            pagestats.stop_ts_sz += sz
            pagestats.num_stop_ts += 1
            p.rint_v(' stop ts: ' + ts(t))
        if extra & WT_CELL_TXN_START != 0:
            t, sz = unpack_uint64_with_sz(b)
            pagestats.start_txn_sz += sz
            pagestats.num_start_txn += 1
            p.rint_v(' start txn: ' + txn(t))
        if extra & WT_CELL_TXN_STOP != 0:
            t, sz = unpack_uint64_with_sz(b)
            pagestats.stop_txn_sz += sz
            pagestats.num_stop_txn += 1
            p.rint_v(' stop txn: ' + txn(t))
        if extra & 0x80:
            p.rint_v(' *** JUNK in extra descriptor: ' + hex(extra))

def celltype_string(b):
    # from cell.h
    if b == 0:
        return 'WT_CELL_ADDR_DEL'
    elif b == 1:
        return 'WT_CELL_ADDR_INT'
    elif b == 2:
        return 'WT_CELL_ADDR_LEAF'
    elif b == 3:
        return 'WT_CELL_ADDR_LEAF_NO'
    elif b == 4:
        return 'WT_CELL_DEL'
    elif b == 5:
        return 'WT_CELL_KEY'
    elif b == 6:
        return 'WT_CELL_KEY_OVFL'
    elif b == 12:
        return 'WT_CELL_KEY_OVFL_RM'
    elif b == 7:
        return 'WT_CELL_KEY_PFX'
    elif b == 8:
        return 'WT_CELL_VALUE'
    elif b == 9:
        return 'WT_CELL_VALUE_COPY'
    elif b == 10:
        return 'WT_CELL_VALUE_OVFL'
    elif b == 11:
        return 'WT_CELL_VALUE_OVFL_RM'
    else:
        return '*** unknown cell type ***'

def block_decode(p, b, opts):
    disk_pos = b.tell()

    # Switch the printer and the binary stream to work on the page data as opposed to working on
    # the file itself. We need to do this to support compressed blocks. As a consequence, offsets
    # printed in the split mode are relative to a (potentially uncompressed) page, rather than
    # the file.
    page_data = bytearray(b.read(40))
    b.saved_bytes()
    b_page = BinFile(io.BytesIO(page_data))
    p = Printer(b_page, opts.split, opts.verbose)

    # WT_PAGE_HEADER in btmem.h (28 bytes)
    pagehead = PageHeader()
    pagehead.recno = uint64(b_page)
    pagehead.writegen = uint64(b_page)
    pagehead.memsize = uint32(b_page)
    pagehead.ncells = uint32(b_page)
    pagehead.type = uint8(b_page)
    pagehead.flags = uint8(b_page)
    unused = uint8(b_page)
    pagehead.version = uint8(b_page)

    if unused != 0:
        p.rint('? garbage in unused bytes')
        return
    if pagehead.type == 0:
        p.rint('? invalid page')
        return

    p.rint('Page Header:')
    p.rint('  recno: ' + str(pagehead.recno))
    p.rint('  writegen: ' + str(pagehead.writegen))
    p.rint('  memsize: ' + str(pagehead.memsize))
    p.rint('  ncells (oflow len): ' + str(pagehead.ncells))
    p.rint('  page type: ' + str(pagehead.type) + ' (' + pagetype_string(pagehead.type) + ')')
    p.rint('  page flags: ' + hex(pagehead.flags))
    p.rint('  version: ' + str(pagehead.version))

    # WT_BLOCK_HEADER in block.h (12 bytes)
    blockhead = BlockHeader(
        disk_size=uint32(b_page),
        checksum=uint32(b_page),
        flags = uint8(b_page),
    )
    if uint8(b_page) != 0 or uint8(b_page) != 0 or uint8(b_page) != 0:
        p.rint('garbage in unused bytes')
        return
    if blockhead.disk_size > 17 * 1024 * 1024:
        # The maximum document size in MongoDB is 16MB. Larger block sizes are suspect.
        p.rint('the block is too big')
        return
    if blockhead.disk_size < 40:
        # The disk size is too small
        return

    p.rint('Block Header:')
    p.rint('  disk_size: ' + str(blockhead.disk_size))
    p.rint('  checksum: ' + hex(blockhead.checksum))
    p.rint('  block flags: ' + hex(blockhead.flags))

    pagestats = PageStats()

    # Verify the checksum
    if have_crc32c:
        savepos = b.tell()
        b.seek(disk_pos)
        if blockhead.flags & 1 != 0:
            check_size = blockhead.disk_size
        else:
            check_size = 64
        data = bytearray(b.read(check_size))
        b.seek(savepos)
        # Zero-out the checksum field
        data[32] = data[33] = data[34] = data[35] = 0
        if len(data) < check_size:
            p.rint('? reached EOF before the end of the block')
            return
        checksum = crc32c.crc32c(data)
        if checksum != blockhead.checksum:
            p.rint('? the checksum does not match ' + hex(checksum))
            return

    # Skip the rest if we don't want to display the data
    if opts.skip_data:
        b.seek(disk_pos + blockhead.disk_size)
        return

    # Read the block contents
    payload_pos = b.tell()
    header_length = payload_pos - disk_pos
    if pagehead.flags & 1 != 0:
        if not have_snappy:
            p.rint('? the page is compressed (install python-snappy to parse)')
            return
        try:
            compress_skip = 64
            # The first few bytes are uncompressed
            payload_data = bytearray(b.read(compress_skip - header_length))
            # Read the length of the remaining data
            length = min(uint64(b), blockhead.disk_size - compress_skip - 8)
            # Read the compressed data, seek to the end of the block, and uncompress
            compressed_data = b.read(length)
            b.seek(disk_pos + blockhead.disk_size)
            payload_data.extend(snappy.uncompress(compressed_data))
        except:
            p.rint('? the page failed to uncompress')
            if opts.debug:
                traceback.print_exception(*sys.exc_info())
            return
    else:
        payload_data = b.read(pagehead.memsize - header_length)
        b.seek(disk_pos + blockhead.disk_size)

    # Add the payload to the page data & reinitialize the stream and the printer
    page_data.extend(payload_data)
    b_page = BinFile(io.BytesIO(page_data))
    b_page.seek(header_length)
    p = Printer(b_page, opts.split, opts.verbose)

    # Parse the block contents
    if pagehead.type == WT_PAGE_INVALID:
        pass    # a blank page: TODO maybe should check that it's all zeros?
    elif pagehead.type == WT_PAGE_BLOCK_MANAGER:
        p.rint_v('? unimplemented decode for page type WT_PAGE_BLOCK_MANAGER')
        p.rint_v(binary_to_pretty_string(payload_data))
    elif pagehead.type == WT_PAGE_COL_VAR:
        p.rint_v('? unimplemented decode for page type WT_PAGE_COLUMN_VARIABLE')
        p.rint_v(binary_to_pretty_string(payload_data))
    elif pagehead.type == WT_PAGE_ROW_INT or pagehead.type == WT_PAGE_ROW_LEAF:
        row_decode(p, b_page, pagehead, blockhead, pagestats)
    elif pagehead.type == WT_PAGE_OVFL:
        # Use b_page.read() so that we can also print the raw bytes in the split mode
        p.rint_v(raw_bytes(b_page.read(len(payload_data))))
    else:
        p.rint_v('? unimplemented decode for page type {}'.format(pagehead.type))
        p.rint_v(binary_to_pretty_string(payload_data))

    outfile_stats_end(opts, pagehead, blockhead, pagestats)

# Hacking this up so we count timestamps and txns
def row_decode(p, b, pagehead, blockhead, pagestats):
    # cell.h
    # Maximum of 71 bytes:
    #  1: cell descriptor byte
    #  1: prefix compression count
    #  1: secondary descriptor byte
    # 36: 4 timestamps (uint64_t encoding, max 9 bytes)
    # 18: 2 transaction IDs (uint64_t encoding, max 9 bytes)
    #  9: associated 64-bit value (uint64_t encoding, max 9 bytes)
    #  5: data length (uint32_t encoding, max 5 bytes)
    for cellnum in range(0, pagehead.ncells):
        cellpos = b.tell()
        if cellpos >= pagehead.memsize:
            p.rint_v('** OVERFLOW memsize **')
            return
        p.begin_cell(cellnum)

        try:
            desc = uint8(b)
            short = desc & 0x3
            s = '?'
            x = '?'
            txn_ts_start = False
            txn_start = False
            extra = 0
            have_run_length = False

            desc_str = f'desc: 0x{desc:x} '
            if short == 0:
                if desc & 0x8 != 0:
                    # Bit 4 marks a value with an additional descriptor byte. If this flag is set,
                    # the next byte after the initial cell byte is an additional description byte.
                    # The bottom bit in this additional byte indicates that the cell is part of a
                    # prepared, and not yet committed transaction. The next 6 bits describe a validity
                    # and durability window of timestamp/transaction IDs.  The top bit is currently unused.
                    extra = b.read(1)[0]
                    p.rint_v(desc_str + f'extra: 0x{extra:x}')
                    desc_str = ''
                    process_timestamps(p, b, extra, pagestats)

                if desc & 0x4 != 0:
                    # Bit 3 marks an 8B packed, uint64_t value following the cell description byte.
                    # (A run-length counter or a record number for variable-length column store.)
                    runlength = unpack_uint64(b)
                    p.rint_v(desc_str + f'runlength/addr: {d_and_h(runlength)}')
                    desc_str = ''

                # Bits 5-8 are cell "types".
                celltype_int = (desc & 0xf0) >> 4
                celltype = celltype_string(celltype_int)
                #print('DECODING: celltype = {} {}'.format(celltype_int, celltype))

                if celltype == 'WT_CELL_VALUE':
                    if extra != 0:
                        # If there is an extra descriptor byte, the length is a regular encoded int.
                        l = unpack_uint64(b)
                    else:
                        l = long_length(p, b)
                    s = 'val {} bytes'.format(l)
                    x = b.read(l)
                elif celltype == 'WT_CELL_KEY':
                    # 64 is WT_CELL_SIZE_ADJUST.  If the size was less than that,
                    # we would have used the "short" packing.
                    l = long_length(p, b)
                    s = 'key {} bytes'.format(l)
                    x = b.read(l)
                    pagestats.num_keys += 1
                    pagestats.keys_sz += l
                elif celltype == 'WT_CELL_ADDR_LEAF_NO':
                    l = long_length(p, b)
                    s = 'addr (leaf no-overflow) {} bytes'.format(l)
                    x = b.read(l)
                    pagestats.num_keys += 1
                    pagestats.keys_sz += l
                elif celltype == 'WT_CELL_KEY_PFX':
                    #TODO: not right...
                    prefix = uint8(b)
                    l = long_length(p, b)
                    s = 'key prefix={} {} bytes'.format(hex(prefix), l)
                    x = b.read(l)
                    pagestats.num_keys += 1
                    pagestats.keys_sz += l
                elif celltype == 'WT_CELL_KEY_OVFL':
                    #TODO: support RLE
                    #TODO: decode the address cookie
                    l = unpack_uint64(b)
                    s = 'overflow key {} bytes'.format(l)
                    x = b.read(l)
                    pagestats.num_keys += 1
                    pagestats.keys_sz += l
                elif celltype == 'WT_CELL_VALUE_OVFL':
                    #TODO: support SECOND DESC and RLE
                    #TODO: decode the address cookie
                    l = unpack_uint64(b)
                    s = 'overflow val {} bytes'.format(l)
                    x = b.read(l)
                else:
                    p.rint(desc_str + ', celltype = {} ({}) not implemented'.format(celltype_int, celltype))
                    desc_str = ''
            elif short == 2:
                l = (desc & 0xfc) >> 2
                pfx_compress_byte = b.read(1)[0]
                s = 'short key prefix={} {} bytes'.format(hex(pfx_compress_byte), l)
                x = b.read(l)
                pagestats.num_keys += 1
                pagestats.keys_sz += l
            else: # short is 1 or 3
                l = (desc & 0xfc) >> 2
                if short == 1:
                    s = 'short key {} bytes'.format(l)
                    pagestats.num_keys += 1
                    pagestats.keys_sz += l
                else:
                    s = 'short val {} bytes'.format(l)
                x = b.read(l)

            try:
                if s != '?':
                    p.rint_v(f'{desc_str}{s}:')
                    p.rint_v(raw_bytes(x))
                else:
                    dumpraw(p, b, cellpos)
            except (IndexError, ValueError):
                # FIXME-WT-13000 theres a bug in raw_bytes
                pass

        finally:
            p.end_cell()

def outfile_header(opts):
    if opts.output != None:
        fields = [
            "block id",
            
            # page head
            "writegen",
            "memsize",
            "ncells",
            "page type",
            
            # block head
            "disk size",

            # page stats
            *PageStats.csv_cols(),
        ]
        opts.output.write(",".join(fields))

def outfile_stats_start(opts, blockid):
    if opts.output != None:
        opts.output.write("\n" + blockid + ",")

def outfile_stats_end(opts, pagehead, blockhead, pagestats):
    if opts.output != None:
        line = [
            # page head
            pagehead.writegen,
            pagehead.ncells,
            pagetype_string(pagehead.type),

            # block header
            blockhead.disk_size,

            # page_stats
            *pagestats.to_csv_cols(),
        ]
        opts.output.write(",".join(str(x) for x in line))

def wtdecode_file_object(b, opts, nbytes):
    p = Printer(b, opts.split, opts.verbose)
    pagecount = 0
    if opts.offset == 0 and not opts.fragment:
        file_header_decode(p, b)
        startblock = (b.tell() + 0x1ff) & ~(0x1FF)
    else:
        startblock = opts.offset

    outfile_header(opts)

    while (nbytes == 0 or startblock < nbytes) and (opts.pages == 0 or pagecount < opts.pages):
        d_h = d_and_h(startblock)
        outfile_stats_start(opts, d_h)
        print('Decode at ' + d_h)
        b.seek(startblock)
        try:
            block_decode(p, b, opts)
        except BrokenPipeError:
            break
        except Exception:
            p.rint(f'ERROR decoding block at {d_and_h(startblock)}')
            if opts.debug:
                traceback.print_exception(*sys.exc_info())
        p.rint('')
        pos = b.tell()
        pos = (pos + 0x1FF) & ~(0x1FF)
        if startblock == pos:
            startblock += 0x200
        else:
            startblock = pos
        pagecount += 1
    p.rint('')
    
def encode_bytes(f):
    lines = f.readlines()
    allbytes = bytearray()
    for line in lines:
        if ':' in line:
            (_, _, line) = line.rpartition(':')
        nospace = line.replace(' ', '').replace('\n', '')
        print('LINE (len={}): {}'.format(len(nospace), nospace))
        if len(nospace) > 0:
            print('first={}, last={}'.format(nospace[0], nospace[-1]))
            b = codecs.decode(nospace, 'hex')
            #b = bytearray.fromhex(line).decode()
            allbytes += b
    return allbytes

def wtdecode(opts):
    if opts.dumpin:
        opts.fragment = True
        if opts.filename == '-':
            allbytes = encode_bytes(sys.stdin)
        else:
            with open(opts.filename, "r") as infile:
                allbytes = encode_bytes(infile)
        b = BinFile(io.BytesIO(allbytes))
        wtdecode_file_object(b, opts, len(allbytes))
    elif opts.filename == '-':
        nbytes = 0      # unknown length
        print('stdin, position ' + hex(opts.offset) + ', pagelimit ' +  str(opts.pages))
        wtdecode_file_object(BinFile(sys.stdin.buffer), opts, nbytes)
    else:
        nbytes = os.path.getsize(opts.filename)
        print(opts.filename + ', position ' + hex(opts.offset) + '/' + hex(nbytes) + ', pagelimit ' +  str(opts.pages))
        with open(opts.filename, "rb") as fileobj:
            wtdecode_file_object(BinFile(fileobj), opts, nbytes)
    
import argparse
parser = argparse.ArgumentParser()
parser.add_argument('-b', '--bytes', help="show bytes alongside decoding", action='store_true')
parser.add_argument('-D', '--debug', help="debug this tool", action='store_true')
parser.add_argument('-d', '--dumpin', help="input is hex dump (may be embedded in log messages)", action='store_true')
parser.add_argument('-f', '--fragment', help="input file is a fragment, does not have a WT file header", action='store_true')
parser.add_argument('-v', '--verbose', help="print things about data, not just the headers", action='store_true')
parser.add_argument('--skip-data', help="do not read/process data", action='store_true')
parser.add_argument('-o', '--offset', help="seek offset before decoding", type=int, default=0)
parser.add_argument('-p', '--pages', help="number of pages to decode", type=int, default=0)
parser.add_argument('-s', '--split', help="split output to also show raw bytes", action='store_true')
parser.add_argument('-V', '--version', help="print version number of this program", action='store_true')
parser.add_argument("-c", "--csv", action='store', type=argparse.FileType('w'), dest='output', help="Directs the output to a name of your choice")
parser.add_argument('filename', help="file name or '-' for stdin")
opts = parser.parse_args()

if opts.version:
    print('wt_binary_decode version "{}"'.format(decode_version))
    sys.exit(0)

try:
    wtdecode(opts)
except KeyboardInterrupt:
    pass
except BrokenPipeError:
    pass

sys.exit(0)
