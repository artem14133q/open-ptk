// ptk-unpack: gets the game's files out of its CD images, on any system,
// with nothing else installed.
//
// The chain is .isz -> .iso -> files: an ISZ image (UltraISO's compressed
// ISO) is read as the ISO it holds, block by block, without writing the ISO
// out; the ISO 9660 file system (with its Joliet names where there are any)
// is read for the files; and an InstallShield cabinet among them (data1.cab,
// where the installer keeps the game itself) is unpacked as well. Everything
// is in this one file and Go's standard library.
//
//	ptk-unpack game <out-dir> <image>...      a ready game folder from the CDs
//	ptk-unpack extract <image> <out-dir>      every file of one image
//	ptk-unpack isz2iso <image.isz> [out.iso]  the ISO out of an ISZ, checked
//	ptk-unpack cab <data1.cab> <out-dir>      an InstallShield cabinet
//	ptk-unpack ls <image>                     the files of an image
//
// An image is a .isz (the first part of a multi-part one) or a .iso.
package main

import (
	"bytes"
	"compress/bzip2"
	"compress/flate"
	"compress/zlib"
	"crypto/md5"
	"encoding/binary"
	"errors"
	"fmt"
	"hash/crc32"
	"io"
	"os"
	"path"
	"path/filepath"
	"sort"
	"strings"
	"unicode/utf16"
)

// ---- ISZ ----
//
// An ISZ file is a 64-byte header, a table of chunk pointers and the chunks.
// The ISO is cut into blocks of block_size bytes; each is stored as zeros
// (nothing stored), as is, zlib- or bzip2-compressed. The pointer table and
// the segment table are hidden under a 4-byte XOR. A big image is split into
// segments, files of their own (name.isz, name.i01, ...), and a chunk may
// run on from one segment into the next, past that one's 64-byte header.
// (After ISZ-tool by Olivier Serres.)

type iszChunk struct {
	kind   int // 0 zeros, 1 stored, 2 zlib, 3 bzip2
	size   int64
	seg    int   // the segment it starts in
	offset int64 // where in that segment
	tail   int64 // bytes of it in the next segment, past its header
}

type isz struct {
	files       []*os.File
	chunks      []iszChunk
	blockSize   int64
	size        int64
	checksum    uint32 // CRC-32 of the whole ISO
	cachedBlock int
	cache       []byte
}

var iszKey = [4]byte{0xb6, 0x8c, 0xa5, 0xde}

func unxor(b []byte) {
	for i := range b {
		b[i] ^= iszKey[i%4]
	}
}

func openISZ(name string) (*isz, error) {
	f, err := os.Open(name)
	if err != nil {
		return nil, err
	}
	h := make([]byte, 64)
	if _, err := io.ReadFull(f, h); err != nil {
		f.Close()
		return nil, fmt.Errorf("%s: too short for an ISZ header", name)
	}
	if string(h[0:4]) != "IsZ!" {
		f.Close()
		return nil, fmt.Errorf("%s: not an ISZ image", name)
	}
	le := binary.LittleEndian
	var (
		version      = h[5]
		sectorSize   = int64(le.Uint16(h[10:]))
		sectors      = int64(le.Uint32(h[12:]))
		encryption   = h[16]
		nblock       = int(le.Uint32(h[25:]))
		blockSize    = int64(le.Uint32(h[29:]))
		pointerLen   = int(h[33])
		segNumber    = int8(h[34])
		chunkPtrOff  = int64(le.Uint32(h[35:]))
		segPtrOff    = int64(le.Uint32(h[39:]))
		dataOff      = int64(le.Uint32(h[43:]))
		checksumData = le.Uint32(h[48:])
		size1        = int64(le.Uint32(h[52:]))
	)
	fail := func(format string, a ...any) (*isz, error) {
		f.Close()
		return nil, fmt.Errorf("%s: "+format, append([]any{name}, a...)...)
	}
	if version != 1 {
		return fail("ISZ version %d is not supported", version)
	}
	if encryption != 0 {
		return fail("the image is password-protected or encrypted")
	}
	if segNumber != 0 {
		return fail("not the first part of a multi-part image")
	}
	// The header keeps the CRC-32 of the ISO inverted.
	img := &isz{files: []*os.File{f}, blockSize: blockSize, size: sectorSize * sectors,
		checksum: ^checksumData, cachedBlock: -1}

	// The chunk table.
	type ptr struct {
		kind int
		size int64
	}
	var ptrs []ptr
	if chunkPtrOff == 0 {
		ptrs = []ptr{{1, size1}}
		img.blockSize = size1
	} else {
		if pointerLen != 3 && pointerLen != 4 {
			return fail("chunk pointers of %d bytes are not supported", pointerLen)
		}
		table := make([]byte, pointerLen*nblock)
		if _, err := f.ReadAt(table, chunkPtrOff); err != nil {
			return fail("reading the chunk table: %v", err)
		}
		unxor(table)
		for i := 0; i < nblock; i++ {
			var v uint32
			for k := pointerLen - 1; k >= 0; k-- {
				v = v<<8 | uint32(table[i*pointerLen+k])
			}
			shift := uint(pointerLen*8 - 2)
			ptrs = append(ptrs, ptr{int(v >> shift), int64(v & (1<<shift - 1))})
		}
	}

	// The segment table: where each segment's chunks start.
	type segment struct {
		chunks, first int
		offset, left  int64
	}
	var segs []segment
	if segPtrOff == 0 {
		segs = []segment{{chunks: len(ptrs), first: 0, offset: dataOff}}
	} else {
		for off := segPtrOff; ; off += 24 {
			b := make([]byte, 24)
			if _, err := f.ReadAt(b, off); err != nil {
				return fail("reading the segment table: %v", err)
			}
			unxor(b)
			if int64(le.Uint64(b)) == 0 {
				break
			}
			segs = append(segs, segment{int(int32(le.Uint32(b[8:]))), int(int32(le.Uint32(b[12:]))),
				int64(int32(le.Uint32(b[16:]))), int64(int32(le.Uint32(b[20:])))})
		}
	}
	if len(segs) > 1 {
		if err := img.openParts(name, len(segs)); err != nil {
			img.Close()
			return nil, err
		}
	}

	img.chunks = make([]iszChunk, len(ptrs))
	for s, seg := range segs {
		cur := seg.offset
		last := seg.first + seg.chunks - 1
		for i := seg.first; i <= last && i < len(ptrs); i++ {
			c := iszChunk{kind: ptrs[i].kind, size: ptrs[i].size, seg: s, offset: cur}
			if i == last && seg.left != 0 {
				c.tail = seg.left
			}
			img.chunks[i] = c
			if c.kind != 0 {
				cur += c.size
			}
		}
	}
	return img, nil
}

// The parts of a multi-part image: name.i01, name.part02.isz or
// name.part002.isz after name.isz / name.part01.isz / name.part001.isz.
func (img *isz) openParts(first string, count int) error {
	lower := strings.ToLower(first)
	if !strings.HasSuffix(lower, ".isz") {
		return fmt.Errorf("%s: the first part of a multi-part image must end in .isz", first)
	}
	base := first[:len(first)-4]
	conventions := []func(int) string{
		func(i int) string { return fmt.Sprintf("%s.i%02d", base, i) },
	}
	if strings.HasSuffix(strings.ToLower(base), ".part01") {
		b := base[:len(base)-7]
		conventions = append(conventions, func(i int) string { return fmt.Sprintf("%s.part%02d.isz", b, i+1) })
	}
	if strings.HasSuffix(strings.ToLower(base), ".part001") {
		b := base[:len(base)-8]
		conventions = append(conventions, func(i int) string { return fmt.Sprintf("%s.part%03d.isz", b, i+1) })
	}
	for _, name := range conventions {
		if _, err := os.Stat(name(1)); err != nil {
			continue
		}
		for i := 1; i < count; i++ {
			f, err := os.Open(name(i))
			if err != nil {
				return fmt.Errorf("part %d of the image: %v", i+1, err)
			}
			img.files = append(img.files, f)
		}
		return nil
	}
	return fmt.Errorf("%s: the other parts of the multi-part image are not next to it", first)
}

func (img *isz) Close() {
	for _, f := range img.files {
		f.Close()
	}
}

func (img *isz) Size() int64 { return img.size }

// block returns block i of the ISO, uncompressed.
func (img *isz) block(i int) ([]byte, error) {
	if i == img.cachedBlock {
		return img.cache, nil
	}
	c := img.chunks[i]
	var out []byte
	if c.kind == 0 {
		out = make([]byte, c.size)
	} else {
		raw := make([]byte, c.size)
		head := c.size - c.tail
		if _, err := img.files[c.seg].ReadAt(raw[:head], c.offset); err != nil {
			return nil, fmt.Errorf("block %d: %v", i, err)
		}
		if c.tail > 0 {
			if c.seg+1 >= len(img.files) {
				return nil, fmt.Errorf("block %d runs past the last part", i)
			}
			if _, err := img.files[c.seg+1].ReadAt(raw[head:], 64); err != nil {
				return nil, fmt.Errorf("block %d: %v", i, err)
			}
		}
		var err error
		switch c.kind {
		case 1:
			out = raw
		case 2:
			var r io.ReadCloser
			if r, err = zlib.NewReader(bytes.NewReader(raw)); err == nil {
				out, err = io.ReadAll(r)
			}
		case 3:
			if len(raw) >= 3 {
				raw[0], raw[1], raw[2] = 'B', 'Z', 'h' // the header is overwritten in the file
			}
			out, err = io.ReadAll(bzip2.NewReader(bytes.NewReader(raw)))
		default:
			err = fmt.Errorf("unknown kind %d", c.kind)
		}
		if err != nil {
			return nil, fmt.Errorf("block %d: %v", i, err)
		}
	}
	img.cachedBlock, img.cache = i, out
	return out, nil
}

func (img *isz) ReadAt(p []byte, off int64) (int, error) {
	n := 0
	for n < len(p) {
		pos := off + int64(n)
		if pos >= img.size {
			return n, io.EOF
		}
		i := int(pos / img.blockSize)
		if i >= len(img.chunks) {
			return n, io.EOF
		}
		b, err := img.block(i)
		if err != nil {
			return n, err
		}
		in := pos - int64(i)*img.blockSize
		if in >= int64(len(b)) {
			return n, io.ErrUnexpectedEOF
		}
		n += copy(p[n:], b[in:])
	}
	return n, nil
}

// writeISO writes the whole ISO out, checking its CRC as the header gives it.
func (img *isz) writeISO(w io.Writer) error {
	crc := crc32.NewIEEE()
	for i := range img.chunks {
		b, err := img.block(i)
		if err != nil {
			return err
		}
		crc.Write(b)
		if _, err := w.Write(b); err != nil {
			return err
		}
	}
	if crc.Sum32() != img.checksum {
		return errors.New("the ISO's checksum does not match the image's: the image is damaged")
	}
	return nil
}

// ---- images ----

type image interface {
	io.ReaderAt
	Size() int64
	Close()
}

type isoFile struct {
	*os.File
	size int64
}

func (f isoFile) Size() int64 { return f.size }
func (f isoFile) Close()      { f.File.Close() }

func openImage(name string) (image, error) {
	f, err := os.Open(name)
	if err != nil {
		return nil, err
	}
	magic := make([]byte, 4)
	f.ReadAt(magic, 0)
	if string(magic) == "IsZ!" {
		f.Close()
		return openISZ(name)
	}
	st, err := f.Stat()
	if err != nil {
		f.Close()
		return nil, err
	}
	return isoFile{f, st.Size()}, nil
}

// ---- ISO 9660 ----
//
// The volume descriptors start at sector 16. The primary one has the root
// directory with 8.3 names in upper case and ";1" versions; a Joliet one
// (supplementary, escape %/@, %/C or %/E) has the same tree with real names
// in UCS-2. The Joliet tree is used where there is one.

const sector = 2048

type isoEntry struct {
	path   string // with "/" between parts
	lba    int64
	size   int64
	isDir  bool
	parent string
}

func readISO(img image) ([]isoEntry, error) {
	var rootPrimary, rootJoliet []byte
	for s := int64(16); s < 64; s++ {
		vd := make([]byte, sector)
		if _, err := img.ReadAt(vd, s*sector); err != nil {
			return nil, fmt.Errorf("reading the volume descriptors: %v", err)
		}
		if string(vd[1:6]) != "CD001" {
			return nil, errors.New("not an ISO 9660 image")
		}
		switch vd[0] {
		case 1:
			rootPrimary = append([]byte(nil), vd[156:156+34]...)
		case 2:
			esc := vd[88:91]
			if esc[0] == '%' && esc[1] == '/' && (esc[2] == '@' || esc[2] == 'C' || esc[2] == 'E') {
				rootJoliet = append([]byte(nil), vd[156:156+34]...)
			}
		}
		if vd[0] == 255 {
			break
		}
	}
	root, joliet := rootPrimary, false
	if rootJoliet != nil {
		root, joliet = rootJoliet, true
	}
	if root == nil {
		return nil, errors.New("the image has no primary volume descriptor")
	}
	var out []isoEntry
	seen := map[int64]bool{}
	var walk func(lba, size int64, dir string, depth int) error
	walk = func(lba, size int64, dir string, depth int) error {
		if depth > 64 || seen[lba] {
			return nil
		}
		seen[lba] = true
		data := make([]byte, size)
		if _, err := img.ReadAt(data, lba*sector); err != nil {
			return fmt.Errorf("reading directory %q: %v", dir, err)
		}
		for pos := 0; pos < len(data); {
			n := int(data[pos])
			if n == 0 {
				// Records do not cross sectors: on to the next one.
				pos = (pos/sector + 1) * sector
				continue
			}
			if pos+n > len(data) || n < 34 {
				break
			}
			r := data[pos : pos+n]
			pos += n
			nameLen := int(r[32])
			if 33+nameLen > len(r) {
				continue
			}
			raw := r[33 : 33+nameLen]
			if nameLen == 1 && (raw[0] == 0 || raw[0] == 1) {
				continue // "." and ".."
			}
			name := isoName(raw, joliet)
			if name == "" || name == "." || name == ".." {
				continue
			}
			e := isoEntry{
				path:   path.Join(dir, name),
				lba:    int64(binary.LittleEndian.Uint32(r[2:])),
				size:   int64(binary.LittleEndian.Uint32(r[10:])),
				isDir:  r[25]&2 != 0,
				parent: dir,
			}
			out = append(out, e)
			if e.isDir {
				if err := walk(e.lba, e.size, e.path, depth+1); err != nil {
					return err
				}
			}
		}
		return nil
	}
	if err := walk(int64(binary.LittleEndian.Uint32(root[2:])), int64(binary.LittleEndian.Uint32(root[10:])), "", 0); err != nil {
		return nil, err
	}
	return out, nil
}

func isoName(raw []byte, joliet bool) string {
	var name string
	if joliet {
		u := make([]uint16, len(raw)/2)
		for i := range u {
			u[i] = binary.BigEndian.Uint16(raw[2*i:])
		}
		name = string(utf16.Decode(u))
	} else {
		name = string(raw)
	}
	if i := strings.IndexByte(name, ';'); i >= 0 {
		name = name[:i]
	}
	if !joliet {
		name = strings.TrimSuffix(name, ".")
	}
	return safeName(name)
}

// safeName keeps a name from leaving the directory it is written into.
func safeName(name string) string {
	name = strings.Map(func(r rune) rune {
		if r == '/' || r == '\\' || r == 0 || r == ':' {
			return '_'
		}
		return r
	}, name)
	if name == "." || name == ".." {
		return ""
	}
	return name
}

func copySection(img image, e isoEntry, dst string) error {
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	_, err = io.Copy(out, io.NewSectionReader(img, e.lba*sector, e.size))
	if cerr := out.Close(); err == nil {
		err = cerr
	}
	return err
}

// ---- InstallShield cabinets ----
//
// data1.cab of an InstallShield 5 (and later) setup: a 20-byte header
// ("ISc(", version, volume info, the cab descriptor's offset and size),
// the descriptor -- a file table of directory names and file descriptors,
// and the file groups, which name where files go -- and the files' data.
// A compressed file is a run of chunks, each a 16-bit length and that many
// bytes of raw deflate. The descriptor is in the .cab itself (the first
// game's discs) or, where the .cab's header leaves it out, in data1.hdr
// beside it, which starts with the same header (the second game's disc);
// the files' data is in the .cab either way. Each file's MD5 is checked
// where the cabinet has one. (After unshield.)

const (
	fileSplit      = 1
	fileObfuscated = 2
	fileCompressed = 4
	fileInvalid    = 8
)

type cabFile struct {
	name, dir, group string
	flags            uint16
	expanded         int64
	compressed       int64
	offset           int64
	md5              []byte
	volume           int
}

// readCab reads the file table of a cabinet: from the .cab, or from hdr
// (the data1.hdr beside it, nil where there is none) when the .cab has no
// descriptor of its own.
func readCab(cab, hdr io.ReaderAt) ([]cabFile, error) {
	le := binary.LittleEndian
	head := make([]byte, 20)
	readHead := func(r io.ReaderAt, what string) error {
		if _, err := r.ReadAt(head, 0); err != nil {
			return fmt.Errorf("reading the %s header: %v", what, err)
		}
		if le.Uint32(head) != 0x28635349 {
			return fmt.Errorf("the %s is not an InstallShield cabinet", what)
		}
		return nil
	}
	if err := readHead(cab, "cabinet"); err != nil {
		return nil, err
	}
	r := cab
	if le.Uint32(head[12:]) == 0 || le.Uint32(head[16:]) == 0 {
		if hdr == nil {
			return nil, errors.New("the cabinet's descriptor is in a separate .hdr file, and there is none beside it")
		}
		if err := readHead(hdr, ".hdr file"); err != nil {
			return nil, err
		}
		r = hdr
	}
	version := le.Uint32(head[4:])
	descOff := int64(le.Uint32(head[12:]))
	descSize := int64(le.Uint32(head[16:]))
	major := 0
	switch version >> 24 {
	case 1:
		major = int(version>>12) & 0xf
	case 2, 4:
		major = int(version & 0xffff)
		if major != 0 {
			major /= 100
		}
	}
	if descOff == 0 || descSize == 0 {
		return nil, errors.New("the cabinet has no descriptor")
	}
	// The descriptor's size does not cover all it points at: in these
	// cabinets the file table starts near its end and runs past it (unshield
	// reads the whole header file). So the read goes on to the end of the
	// file table, and a little beyond, as far as the cabinet allows.
	desc := make([]byte, descSize)
	if _, err := r.ReadAt(desc, descOff); err != nil {
		return nil, fmt.Errorf("reading the cabinet descriptor: %v", err)
	}
	if len(desc) >= 0x1c {
		end := int64(le.Uint32(desc[0x0c:])) + int64(le.Uint32(desc[0x14:])) + 64<<10
		if end > descSize {
			more := make([]byte, end)
			n, err := r.ReadAt(more, descOff)
			if err != nil && !errors.Is(err, io.EOF) {
				return nil, fmt.Errorf("reading the cabinet's file table: %v", err)
			}
			desc = more[:n]
		}
	}
	u32 := func(off int64) uint32 {
		if off < 0 || off+4 > int64(len(desc)) {
			return 0
		}
		return le.Uint32(desc[off:])
	}
	cstr := func(off int64) string {
		if off < 0 || off >= int64(len(desc)) {
			return ""
		}
		end := bytes.IndexByte(desc[off:], 0)
		if end < 0 {
			end = len(desc) - int(off)
		}
		return cp1251(desc[off : off+int64(end)])
	}
	tableOff := int64(u32(0x0c))
	dirCount := int64(u32(0x1c))
	fileCount := int64(u32(0x28))
	table2 := int64(u32(0x2c))
	table := make([]int64, dirCount+fileCount)
	for i := range table {
		table[i] = int64(u32(tableOff + int64(i)*4))
	}
	dirName := func(i int64) string {
		if i < 0 || i >= dirCount {
			return ""
		}
		return cstr(tableOff + table[i])
	}

	files := make([]cabFile, fileCount)
	for i := int64(0); i < fileCount; i++ {
		var f cabFile
		if major <= 5 {
			p := tableOff + table[dirCount+i]
			nameOff := int64(u32(p))
			f.dir = dirName(int64(u32(p + 4)))
			f.flags = uint16(u32(p+8) & 0xffff)
			f.expanded = int64(u32(p + 10))
			f.compressed = int64(u32(p + 14))
			f.offset = int64(u32(p + 18 + 0x14))
			if major == 5 && p+42+16 <= int64(len(desc)) {
				f.md5 = desc[p+42 : p+42+16]
			}
			f.name = cstr(tableOff + nameOff)
			f.volume = 1
		} else {
			p := tableOff + table2 + i*0x57
			if p+0x57 > int64(len(desc)) {
				return nil, errors.New("the cabinet's file table is cut short")
			}
			b := desc[p:]
			f.flags = le.Uint16(b)
			f.expanded = int64(le.Uint64(b[2:]))
			f.compressed = int64(le.Uint64(b[10:]))
			f.offset = int64(le.Uint64(b[18:]))
			f.md5 = b[26:42]
			nameOff := int64(le.Uint32(b[58:]))
			f.dir = dirName(int64(le.Uint16(b[62:])))
			f.volume = int(le.Uint16(b[85:]))
			f.name = cstr(tableOff + nameOff)
		}
		files[i] = f
	}

	// File groups: a linked list from each of 71 slots; each group names a
	// range of files.
	for slot := int64(0); slot < 71; slot++ {
		for off := int64(u32(0x3e + slot*4)); off != 0; {
			descGroup := int64(u32(off + 4))
			next := int64(u32(off + 8))
			name := cstr(int64(u32(descGroup)))
			skip := int64(0x48)
			if major > 5 {
				skip = 0x12
			}
			first := int64(u32(descGroup + 4 + skip))
			last := int64(u32(descGroup + 8 + skip))
			for i := first; i <= last && i < fileCount; i++ {
				if files[i].group == "" {
					files[i].group = name
				}
			}
			if next == off {
				break
			}
			off = next
		}
	}
	return files, nil
}

// cp1251 turns the cabinet's Windows-1251 names into UTF-8.
func cp1251(b []byte) string {
	const high = "ЂЃ‚ѓ„…†‡€‰Љ‹ЊЌЋЏђ‘’“”•–—�™љ›њќћџ ЎўЈ¤Ґ¦§Ё©Є«¬­®Ї°±Ііґµ¶·ё№є»јЅѕї"
	hi := []rune(high)
	var sb strings.Builder
	for _, c := range b {
		switch {
		case c < 0x80:
			sb.WriteByte(c)
		case c >= 0xC0:
			sb.WriteRune(rune(0x410 + int(c) - 0xC0))
		default:
			sb.WriteRune(hi[c-0x80])
		}
	}
	return sb.String()
}

func deobfuscate(b []byte, seed *uint32) {
	for i := range b {
		v := b[i] ^ 0xd5
		v = v>>2 | v<<6
		b[i] = v - byte(*seed%0x47)
		*seed++
	}
}

// extractCabFile writes one file of the cabinet.
func extractCabFile(r io.ReaderAt, f cabFile, dst string) error {
	if f.flags&fileInvalid != 0 || f.offset == 0 {
		return fmt.Errorf("%s: marked invalid in the cabinet", f.name)
	}
	if f.flags&fileSplit != 0 || f.volume > 1 {
		return fmt.Errorf("%s: spans cabinet volumes, which is not supported", f.name)
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()
	sum := md5.New()
	w := io.MultiWriter(out, sum)
	src := io.NewSectionReader(r, f.offset, f.compressed)
	var seed uint32
	written := int64(0)
	if f.flags&fileCompressed != 0 {
		left := f.compressed
		lenBuf := make([]byte, 2)
		for left > 0 && written < f.expanded {
			if _, err := io.ReadFull(src, lenBuf); err != nil {
				return fmt.Errorf("%s: %v", f.name, err)
			}
			if f.flags&fileObfuscated != 0 {
				deobfuscate(lenBuf, &seed)
			}
			n := int64(binary.LittleEndian.Uint16(lenBuf))
			chunk := make([]byte, n)
			if _, err := io.ReadFull(src, chunk); err != nil {
				return fmt.Errorf("%s: %v", f.name, err)
			}
			if f.flags&fileObfuscated != 0 {
				deobfuscate(chunk, &seed)
			}
			left -= 2 + n
			plain, err := io.ReadAll(flate.NewReader(bytes.NewReader(chunk)))
			if err != nil && !errors.Is(err, io.ErrUnexpectedEOF) {
				return fmt.Errorf("%s: %v", f.name, err)
			}
			if _, err := w.Write(plain); err != nil {
				return err
			}
			written += int64(len(plain))
		}
	} else {
		buf := make([]byte, 64<<10)
		for written < f.expanded {
			n, err := src.Read(buf)
			if n > 0 {
				if f.flags&fileObfuscated != 0 {
					deobfuscate(buf[:n], &seed)
				}
				w.Write(buf[:n])
				written += int64(n)
			}
			if err != nil {
				break
			}
		}
	}
	if written != f.expanded {
		return fmt.Errorf("%s: %d bytes out of %d", f.name, written, f.expanded)
	}
	if len(f.md5) == 16 && !bytes.Equal(f.md5, make([]byte, 16)) && !bytes.Equal(sum.Sum(nil), f.md5) {
		return fmt.Errorf("%s: MD5 does not match the cabinet's", f.name)
	}
	return out.Close()
}

// extractCab unpacks a whole cabinet as unshield does: <group>/<dir>/<file>.
// hdr is the data1.hdr beside the .cab, or nil.
func extractCab(r, hdr io.ReaderAt, outDir string, report func(string)) (int, error) {
	files, err := readCab(r, hdr)
	if err != nil {
		return 0, err
	}
	count := 0
	for _, f := range files {
		if f.name == "" || f.flags&fileInvalid != 0 || f.offset == 0 {
			continue
		}
		parts := []string{outDir}
		for _, p := range []string{f.group} {
			if p = safeName(p); p != "" {
				parts = append(parts, p)
			}
		}
		for _, p := range strings.FieldsFunc(f.dir, func(r rune) bool { return r == '\\' || r == '/' }) {
			if p = safeName(p); p != "" {
				parts = append(parts, p)
			}
		}
		parts = append(parts, safeName(f.name))
		dst := filepath.Join(parts...)
		if report != nil {
			report(dst)
		}
		if err := extractCabFile(r, f, dst); err != nil {
			return count, err
		}
		count++
	}
	return count, nil
}

// ---- the game ----

// gameFile says whether a file from a CD root belongs in the game folder,
// and under which name: the resource stores, the films (the first game's
// intro; the second's intro, outro and the flight between its parts), the
// disc marks (as the game looks for them, in lower case).
func gameFile(name string) (string, bool) {
	upper := strings.ToUpper(name)
	switch {
	case strings.HasSuffix(upper, ".STR"):
		return name, true
	case strings.HasSuffix(upper, ".AVI"):
		return name, true
	case strings.HasPrefix(upper, "DISK") && strings.HasSuffix(upper, ".ID"):
		return strings.ToLower(name), true
	}
	return "", false
}

func cmdGame(outDir string, images []string) error {
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return err
	}
	cabs := 0
	for _, name := range images {
		img, err := openImage(name)
		if err != nil {
			return err
		}
		entries, err := readISO(img)
		if err != nil {
			img.Close()
			return fmt.Errorf("%s: %v", name, err)
		}
		fmt.Printf("%s\n", filepath.Base(name))
		// data1.cab, and the data1.hdr that may hold its descriptor, are
		// unpacked once the disc's other files are copied.
		var cab, hdr *isoEntry
		for i, e := range entries {
			if e.isDir || e.parent != "" {
				continue
			}
			if strings.EqualFold(e.path, "data1.cab") {
				cab = &entries[i]
				continue
			}
			if strings.EqualFold(e.path, "data1.hdr") {
				hdr = &entries[i]
				continue
			}
			target, ok := gameFile(e.path)
			if !ok {
				continue
			}
			fmt.Printf("  %s\n", target)
			if err := copySection(img, e, filepath.Join(outDir, target)); err != nil {
				img.Close()
				return err
			}
		}
		if cab != nil {
			dst := filepath.Join(outDir, "data1_out")
			fmt.Printf("  %s -> data1_out/\n", cab.path)
			var hdrR io.ReaderAt
			if hdr != nil {
				hdrR = io.NewSectionReader(img, hdr.lba*sector, hdr.size)
			}
			n, err := extractCab(io.NewSectionReader(img, cab.lba*sector, cab.size), hdrR, dst, func(p string) {
				rel, _ := filepath.Rel(outDir, p)
				fmt.Printf("    %s\n", filepath.ToSlash(rel))
			})
			if err != nil {
				img.Close()
				return fmt.Errorf("%s: %v", cab.path, err)
			}
			cabs += n
		}
		img.Close()
	}
	if cabs == 0 {
		fmt.Println("note: no data1.cab was found -- the game's program and main store come from the installer's disc")
	}
	fmt.Printf("done: %s\n", outDir)
	return nil
}

func cmdExtract(name, outDir string) error {
	img, err := openImage(name)
	if err != nil {
		return err
	}
	defer img.Close()
	entries, err := readISO(img)
	if err != nil {
		return err
	}
	for _, e := range entries {
		dst := filepath.Join(outDir, filepath.FromSlash(e.path))
		if e.isDir {
			if err := os.MkdirAll(dst, 0o755); err != nil {
				return err
			}
			continue
		}
		fmt.Println(e.path)
		if err := copySection(img, e, dst); err != nil {
			return err
		}
	}
	return nil
}

func cmdLs(name string) error {
	img, err := openImage(name)
	if err != nil {
		return err
	}
	defer img.Close()
	entries, err := readISO(img)
	if err != nil {
		return err
	}
	sort.Slice(entries, func(i, j int) bool { return entries[i].path < entries[j].path })
	for _, e := range entries {
		if e.isDir {
			fmt.Printf("%12s  %s/\n", "", e.path)
		} else {
			fmt.Printf("%12d  %s\n", e.size, e.path)
		}
	}
	return nil
}

func cmdISZ2ISO(src, dst string) error {
	img, err := openISZ(src)
	if err != nil {
		return err
	}
	defer img.Close()
	if dst == "" {
		dst = strings.TrimSuffix(src, filepath.Ext(src)) + ".iso"
	}
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	if err := img.writeISO(out); err != nil {
		out.Close()
		os.Remove(dst)
		return err
	}
	if err := out.Close(); err != nil {
		return err
	}
	fmt.Printf("%s: %d bytes, checksum correct\n", dst, img.Size())
	return nil
}

func cmdCab(src, outDir string) error {
	f, err := os.Open(src)
	if err != nil {
		return err
	}
	defer f.Close()
	// The .hdr beside it, under the same name, if there is one.
	var hdr io.ReaderAt
	base := strings.TrimSuffix(src, filepath.Ext(src))
	for _, ext := range []string{".hdr", ".HDR", ".Hdr"} {
		if h, err := os.Open(base + ext); err == nil {
			defer h.Close()
			hdr = h
			break
		}
	}
	n, err := extractCab(f, hdr, outDir, func(p string) { fmt.Println(p) })
	if err != nil {
		return err
	}
	fmt.Printf("%d files\n", n)
	return nil
}

const usage = `ptk-unpack: the game's files out of its CD images (.isz or .iso)

  ptk-unpack game <out-dir> <image>...      a ready game folder from all the CDs:
                                            the .STR stores, the .avi films, diskN.id,
                                            and data1.cab unpacked to data1_out/
  ptk-unpack extract <image> <out-dir>      every file of one image
  ptk-unpack isz2iso <image.isz> [out.iso]  the ISO out of an ISZ image, checked
  ptk-unpack cab <data1.cab> <out-dir>      unpack an InstallShield cabinet
                                            (and data1.hdr beside it, if any)
  ptk-unpack ls <image>                     list the files of an image

A multi-part ISZ image is given by its first part.
`

func main() {
	args := os.Args[1:]
	var err error
	switch {
	case len(args) >= 3 && args[0] == "game":
		err = cmdGame(args[1], args[2:])
	case len(args) == 3 && args[0] == "extract":
		err = cmdExtract(args[1], args[2])
	case (len(args) == 2 || len(args) == 3) && (args[0] == "isz2iso" || args[0] == "2iso"):
		dst := ""
		if len(args) == 3 {
			dst = args[2]
		}
		err = cmdISZ2ISO(args[1], dst)
	case len(args) == 3 && args[0] == "cab":
		err = cmdCab(args[1], args[2])
	case len(args) == 2 && args[0] == "ls":
		err = cmdLs(args[1])
	default:
		fmt.Fprint(os.Stderr, usage)
		os.Exit(2)
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}
